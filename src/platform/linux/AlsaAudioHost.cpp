#include "contracts/IPlatform.h"

#if defined(__linux__)
#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace avantgarde {
namespace {

static int16_t floatToS16(float x) noexcept {
    const float clamped = std::clamp(x, -1.0f, 1.0f);
    const float scaled = clamped * 32767.0f;
    return static_cast<int16_t>(scaled);
}

class AlsaAudioStream final : public IAudioStream {
public:
    AlsaAudioStream(const StreamConfig& cfg,
                    std::string outputDeviceId,
                    NonRtNotifyCb onNotify,
                    void* notifyUser) noexcept
        : cfg_(cfg),
          outDeviceId_(std::move(outputDeviceId)),
          onNotify_(onNotify),
          notifyUser_(notifyUser) {
        cfg_.numOutput = std::clamp(cfg_.numOutput, 1, 2);
        if (cfg_.sampleRate <= 0) {
            cfg_.sampleRate = 48000;
        }
        if (cfg_.blockFrames <= 0) {
            cfg_.blockFrames = 256;
        }
        outL_.resize(static_cast<std::size_t>(cfg_.blockFrames), 0.0f);
        outR_.resize(static_cast<std::size_t>(cfg_.blockFrames), 0.0f);
        interleavedS16_.resize(static_cast<std::size_t>(cfg_.blockFrames) * static_cast<std::size_t>(cfg_.numOutput), 0);
    }

    ~AlsaAudioStream() override {
        close();
    }

    bool start(AudioRenderCb render, void* user) noexcept override {
        if (running_.load(std::memory_order_acquire)) {
            return true;
        }
        render_ = render;
        user_ = user;

        if (!openPcm_()) {
            notify_(1001, "ALSA open failed");
            return false;
        }

        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { this->runLoop_(); });
        return true;
    }

    void stop() noexcept override {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void close() noexcept override {
        stop();
        if (pcm_) {
            snd_pcm_drop(pcm_);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    int sampleRate() const noexcept override { return cfg_.sampleRate; }
    int blockFrames() const noexcept override { return cfg_.blockFrames; }
    int numInput() const noexcept override { return 0; }
    int numOutput() const noexcept override { return cfg_.numOutput; }
    uint64_t totalCallbacks() const noexcept override { return totalCallbacks_.load(std::memory_order_relaxed); }
    uint64_t xruns() const noexcept override { return xruns_.load(std::memory_order_relaxed); }

private:
    bool openPcm_() noexcept {
        const char* dev = outDeviceId_.empty() ? "default" : outDeviceId_.c_str();
        if (snd_pcm_open(&pcm_, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
            return false;
        }

        const unsigned int sr = static_cast<unsigned int>(cfg_.sampleRate);
        const unsigned int latencyUs = static_cast<unsigned int>(
            std::max(1000, (cfg_.blockFrames * 1000000) / std::max(1, cfg_.sampleRate) * 2));

        const int rc = snd_pcm_set_params(pcm_,
                                          SND_PCM_FORMAT_S16_LE,
                                          SND_PCM_ACCESS_RW_INTERLEAVED,
                                          static_cast<unsigned int>(cfg_.numOutput),
                                          sr,
                                          1, // soft_resample
                                          latencyUs);
        if (rc < 0) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }
        return true;
    }

    void runLoop_() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            const std::size_t frames = static_cast<std::size_t>(cfg_.blockFrames);
            std::fill(outL_.begin(), outL_.end(), 0.0f);
            std::fill(outR_.begin(), outR_.end(), 0.0f);

            float* outPtrs[2]{outL_.data(), outR_.data()};
            AudioProcessContext ctx{};
            ctx.in = nullptr;
            ctx.out = outPtrs;
            ctx.numOut = static_cast<uint32_t>(cfg_.numOutput);
            ctx.nframes = frames;

            if (render_) {
                render_(ctx, user_);
            }
            totalCallbacks_.fetch_add(1, std::memory_order_relaxed);

            if (cfg_.numOutput == 1) {
                for (std::size_t i = 0; i < frames; ++i) {
                    interleavedS16_[i] = floatToS16(outL_[i]);
                }
            } else {
                for (std::size_t i = 0; i < frames; ++i) {
                    interleavedS16_[2 * i + 0] = floatToS16(outL_[i]);
                    interleavedS16_[2 * i + 1] = floatToS16(outR_[i]);
                }
            }

            std::size_t offset = 0;
            while (offset < frames && running_.load(std::memory_order_acquire)) {
                const snd_pcm_sframes_t wr = snd_pcm_writei(
                    pcm_,
                    interleavedS16_.data() + offset * static_cast<std::size_t>(cfg_.numOutput),
                    static_cast<snd_pcm_uframes_t>(frames - offset));

                if (wr > 0) {
                    offset += static_cast<std::size_t>(wr);
                    continue;
                }
                if (wr == -EPIPE) {
                    xruns_.fetch_add(1, std::memory_order_relaxed);
                    snd_pcm_prepare(pcm_);
                    continue;
                }
                if (wr < 0) {
                    const int rr = snd_pcm_recover(pcm_, static_cast<int>(wr), 1);
                    if (rr < 0) {
                        notify_(1002, "ALSA write/recover failed");
                        running_.store(false, std::memory_order_release);
                        break;
                    }
                }
            }
        }
    }

    void notify_(int code, const char* msg) const noexcept {
        if (onNotify_) {
            onNotify_(code, msg, notifyUser_);
        }
    }

private:
    StreamConfig cfg_{};
    std::string outDeviceId_{};
    NonRtNotifyCb onNotify_{nullptr};
    void* notifyUser_{nullptr};

    snd_pcm_t* pcm_{nullptr};
    AudioRenderCb render_{nullptr};
    void* user_{nullptr};
    std::thread worker_{};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> totalCallbacks_{0};
    std::atomic<uint64_t> xruns_{0};

    std::vector<float> outL_{};
    std::vector<float> outR_{};
    std::vector<int16_t> interleavedS16_{};
};

class AlsaAudioHost final : public IAudioHost {
public:
    std::vector<AudioDeviceInfo> enumerate() override {
        std::vector<AudioDeviceInfo> out{};
        AudioDeviceInfo d{};
        d.id = "default";
        d.name = "ALSA Default";
        d.maxInput = 0;
        d.maxOutput = 2;
        d.defaultSampleRate = 48000;
        d.isDefault = true;
        out.push_back(std::move(d));
        return out;
    }

    std::unique_ptr<IAudioStream> openStream(const StreamConfig& cfg,
                                             const std::string& /*inputDeviceId*/,
                                             const std::string& outputDeviceId,
                                             NonRtNotifyCb onNotify = nullptr,
                                             void* notifyUser = nullptr) override {
        return std::make_unique<AlsaAudioStream>(cfg, outputDeviceId, onNotify, notifyUser);
    }
};

} // namespace

std::shared_ptr<IAudioHost> createDefaultAudioHost() {
    return std::make_shared<AlsaAudioHost>();
}

} // namespace avantgarde

#endif // defined(__linux__)
