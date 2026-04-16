#include "contracts/ISamplePreviewEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace avantgarde {
namespace {

float clamp01(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

float clampSpeed(float v) noexcept {
    if (!std::isfinite(v)) return 1.0f;
    return std::clamp(v, 0.25f, 4.0f);
}

SampleRegion sanitizeRegion(const SampleRegion& in, int32_t totalFrames) noexcept {
    SampleRegion out{};
    if (totalFrames <= 1) {
        out.startFrame = 0;
        out.endFrame = 1;
        return out;
    }
    out.startFrame = std::clamp<int32_t>(in.startFrame, 0, totalFrames - 1);
    out.endFrame = std::clamp<int32_t>(in.endFrame, out.startFrame + 1, totalFrames);
    return out;
}

class SamplePreviewEngine final : public ISamplePreviewEngine {
public:
    void play(const SharedClipBuffer& sample,
              const SampleRegion& region,
              float speed,
              SamplePreviewLoopMode loopMode,
              const PreviewOptions& options) noexcept override {
        if (!sample.valid()) {
            stop();
            return;
        }

        clipCtl_ = std::make_shared<SharedClipBuffer>(sample);
        pendingClip_.store(clipCtl_.get(), std::memory_order_release);

        const SampleRegion safe = sanitizeRegion(region, static_cast<int32_t>(sample.frames));
        regionStart_.store(safe.startFrame, std::memory_order_release);
        regionEnd_.store(safe.endFrame, std::memory_order_release);
        loopMode_.store(loopMode, std::memory_order_release);

        const float effectiveSpeed = clampSpeed(speed * clampSpeed(options.speed));
        speed_.store(effectiveSpeed, std::memory_order_release);
        gain_.store(clamp01(options.gain), std::memory_order_release);

        routeActive_.store(true, std::memory_order_release);
        requestStop_.store(false, std::memory_order_release);
        requestPlay_.store(true, std::memory_order_release);
    }

    void stop() noexcept override {
        routeActive_.store(true, std::memory_order_release);
        requestPlay_.store(false, std::memory_order_release);
        requestStop_.store(true, std::memory_order_release);
    }

    void setLoop(const SampleRegion& region,
                 SamplePreviewLoopMode loopMode) noexcept override {
        const SharedClipBuffer* clip = clipRt_;
        const int32_t total = (clip && clip->valid()) ? static_cast<int32_t>(clip->frames) : 2;
        const SampleRegion safe = sanitizeRegion(region, total);
        regionStart_.store(safe.startFrame, std::memory_order_release);
        regionEnd_.store(safe.endFrame, std::memory_order_release);
        loopMode_.store(loopMode, std::memory_order_release);
    }

    void process(const AudioProcessContext& ctx) noexcept override {
        float* out0 = (ctx.out ? ctx.out[0] : nullptr);
        if (!out0 || ctx.nframes == 0) {
            return;
        }
        float* out1 = out0;
        if (ctx.out[1]) {
            out1 = ctx.out[1];
        }

        // Preview-движок работает в эксклюзивном режиме (renderThunk route):
        // когда активен preview, основной engine-блок не рендерится.
        // Поэтому здесь обязаны каждый блок начинать с "чистого" выхода,
        // иначе на некоторых хостах можно получить гул/мусор из неинициализированного буфера.
        if (out0 == out1) {
            for (std::size_t i = 0; i < ctx.nframes; ++i) {
                out0[i] = 0.0f;
            }
        } else {
            for (std::size_t i = 0; i < ctx.nframes; ++i) {
                out0[i] = 0.0f;
                out1[i] = 0.0f;
            }
        }

        if (requestStop_.exchange(false, std::memory_order_acq_rel)) {
            runningRt_ = false;
            uiPlaying_.store(false, std::memory_order_relaxed);
            uiPlayhead01_.store(0.0f, std::memory_order_relaxed);
            routeActive_.store(false, std::memory_order_release);
        }

        if (const SharedClipBuffer* p = pendingClip_.exchange(nullptr, std::memory_order_acq_rel)) {
            clipRt_ = p;
            runningRt_ = false;
            readPosRt_ = 0.0;
            uiPlayhead01_.store(0.0f, std::memory_order_relaxed);
        }

        if (requestPlay_.exchange(false, std::memory_order_acq_rel)) {
            const SharedClipBuffer* clip = clipRt_;
            if (clip && clip->valid()) {
                const SampleRegion safe = sanitizeRegion(
                    SampleRegion{
                        regionStart_.load(std::memory_order_acquire),
                        regionEnd_.load(std::memory_order_acquire),
                    },
                    static_cast<int32_t>(clip->frames));
                regionStartRt_ = safe.startFrame;
                regionEndRt_ = safe.endFrame;
                loopRt_ = (loopMode_.load(std::memory_order_acquire) == SamplePreviewLoopMode::On);
                gainRt_ = clamp01(gain_.load(std::memory_order_acquire));
                speedRt_ = clampSpeed(speed_.load(std::memory_order_acquire));
                readPosRt_ = static_cast<double>(regionStartRt_);
                runningRt_ = true;
                routeActive_.store(true, std::memory_order_release);
            }
        }

        if (!runningRt_ || !clipRt_ || !clipRt_->valid()) {
            uiPlaying_.store(false, std::memory_order_relaxed);
            routeActive_.store(false, std::memory_order_release);
            return;
        }

        const SharedClipBuffer* clip = clipRt_;
        const float* ch0 = clip->ch0.get();
        const float* ch1 = (clip->channels == 2 && clip->ch1) ? clip->ch1.get() : clip->ch0.get();
        if (!ch0 || !ch1) {
            runningRt_ = false;
            uiPlaying_.store(false, std::memory_order_relaxed);
            return;
        }

        // Runtime-параметры допускают "живое" изменение на лету.
        {
            const SampleRegion safe = sanitizeRegion(
                SampleRegion{
                    regionStart_.load(std::memory_order_acquire),
                    regionEnd_.load(std::memory_order_acquire),
                },
                static_cast<int32_t>(clip->frames));
            regionStartRt_ = safe.startFrame;
            regionEndRt_ = safe.endFrame;
            loopRt_ = (loopMode_.load(std::memory_order_acquire) == SamplePreviewLoopMode::On);
            gainRt_ = clamp01(gain_.load(std::memory_order_acquire));
            speedRt_ = clampSpeed(speed_.load(std::memory_order_acquire));
            if (readPosRt_ < static_cast<double>(regionStartRt_) ||
                readPosRt_ >= static_cast<double>(regionEndRt_)) {
                readPosRt_ = static_cast<double>(regionStartRt_);
            }
        }

        const int32_t start = regionStartRt_;
        const int32_t end = regionEndRt_;
        const int32_t span = std::max<int32_t>(1, end - start);

        for (std::size_t i = 0; i < ctx.nframes; ++i) {
            if (!runningRt_) {
                break;
            }

            const int32_t i0 = std::clamp<int32_t>(
                static_cast<int32_t>(std::floor(readPosRt_)),
                start,
                end - 1);
            int32_t i1 = i0 + 1;
            if (i1 >= end) {
                i1 = loopRt_ ? start : (end - 1);
            }
            const float frac = clamp01(static_cast<float>(readPosRt_ - static_cast<double>(i0)));
            const float l = ch0[i0] + (ch0[i1] - ch0[i0]) * frac;
            const float r = ch1[i0] + (ch1[i1] - ch1[i0]) * frac;

            if (out0 == out1) {
                // Mono out: чтобы не удваивать уровень, сводим L/R в mono-среднее.
                out0[i] += 0.5f * (l + r) * gainRt_;
            } else {
                out0[i] += l * gainRt_;
                out1[i] += r * gainRt_;
            }

            readPosRt_ += static_cast<double>(speedRt_);
            if (readPosRt_ >= static_cast<double>(end)) {
                if (loopRt_) {
                    while (readPosRt_ >= static_cast<double>(end)) {
                        readPosRt_ -= static_cast<double>(span);
                    }
                    if (readPosRt_ < static_cast<double>(start)) {
                        readPosRt_ = static_cast<double>(start);
                    }
                } else {
                    runningRt_ = false;
                }
            }
        }

        if (runningRt_) {
            const float ph = static_cast<float>(
                (readPosRt_ - static_cast<double>(start)) / static_cast<double>(std::max<int32_t>(1, span)));
            uiPlayhead01_.store(clamp01(ph), std::memory_order_relaxed);
        } else {
            uiPlayhead01_.store(0.0f, std::memory_order_relaxed);
            routeActive_.store(false, std::memory_order_release);
        }
        uiPlaying_.store(runningRt_, std::memory_order_relaxed);
    }

    bool isActive() const noexcept override {
        return routeActive_.load(std::memory_order_acquire);
    }

    SamplePreviewState state() const noexcept override {
        SamplePreviewState out{};
        out.playing = uiPlaying_.load(std::memory_order_relaxed);
        out.playhead01 = clamp01(uiPlayhead01_.load(std::memory_order_relaxed));
        return out;
    }

private:
    // Control-side ресурс preview (держит lifetime буфера).
    std::shared_ptr<SharedClipBuffer> clipCtl_{};
    // RT-side публикация нового буфера без блокировок.
    std::atomic<const SharedClipBuffer*> pendingClip_{nullptr};

    std::atomic<bool> requestPlay_{false};
    std::atomic<bool> requestStop_{false};
    // true -> render callback маршрутизируется в preview, false -> в основной engine.
    std::atomic<bool> routeActive_{false};

    std::atomic<int32_t> regionStart_{0};
    std::atomic<int32_t> regionEnd_{1};
    std::atomic<SamplePreviewLoopMode> loopMode_{SamplePreviewLoopMode::Off};
    std::atomic<float> gain_{0.25f};
    std::atomic<float> speed_{1.0f};

    // RT-state.
    const SharedClipBuffer* clipRt_{nullptr};
    bool runningRt_{false};
    bool loopRt_{false};
    float gainRt_{0.25f};
    float speedRt_{1.0f};
    int32_t regionStartRt_{0};
    int32_t regionEndRt_{1};
    double readPosRt_{0.0};

    // UI telemetry (atomic snapshot).
    std::atomic<bool> uiPlaying_{false};
    std::atomic<float> uiPlayhead01_{0.0f};
};

} // namespace

std::unique_ptr<ISamplePreviewEngine> MakeSamplePreviewEngine() noexcept {
    return std::make_unique<SamplePreviewEngine>();
}

} // namespace avantgarde
