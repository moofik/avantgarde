#include <catch2/catch_all.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "contracts/IPlatform.h"
#include "contracts/IUi.h"

// Тестируем реальный SamplerEngineLayer (как в app), поэтому подключаем concrete TU.
#include "app/SamplerEnginePatternApplyTarget.cpp"
#include "app/SamplerEngineLayer.cpp"

namespace fs = std::filesystem;

namespace {

void writeU16Le(std::ofstream& f, uint16_t v) {
    const uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xFFu),
        static_cast<uint8_t>((v >> 8) & 0xFFu)
    };
    f.write(reinterpret_cast<const char*>(b), 2);
}

void writeU32Le(std::ofstream& f, uint32_t v) {
    const uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFFu),
        static_cast<uint8_t>((v >> 8) & 0xFFu),
        static_cast<uint8_t>((v >> 16) & 0xFFu),
        static_cast<uint8_t>((v >> 24) & 0xFFu)
    };
    f.write(reinterpret_cast<const char*>(b), 4);
}

fs::path writeTestWav(const fs::path& path, int sampleRate, float hz) {
    constexpr int channels = 1;
    constexpr int bitsPerSample = 16;
    constexpr int frames = 1024;
    std::vector<int16_t> pcm(frames * channels, 0);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float s = std::sin(2.0f * 3.14159265359f * hz * t);
        pcm[static_cast<std::size_t>(i)] = static_cast<int16_t>(std::round(s * 12000.0f));
    }

    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t fmtChunkSize = 16u;
    const uint32_t riffChunkSize = 4u + 8u + fmtChunkSize + 8u + dataSize;

    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f.write("RIFF", 4);
    writeU32Le(f, riffChunkSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32Le(f, fmtChunkSize);
    writeU16Le(f, 1u);
    writeU16Le(f, static_cast<uint16_t>(channels));
    writeU32Le(f, static_cast<uint32_t>(sampleRate));
    writeU32Le(f, byteRate);
    writeU16Le(f, blockAlign);
    writeU16Le(f, static_cast<uint16_t>(bitsPerSample));
    f.write("data", 4);
    writeU32Le(f, dataSize);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(dataSize));
    f.flush();
    REQUIRE(f.good());
    return path;
}

class MockStream final : public avantgarde::IAudioStream {
public:
    explicit MockStream(const avantgarde::StreamConfig& cfg)
        : cfg_(cfg),
          outL_(static_cast<std::size_t>(cfg.blockFrames), 0.0f),
          outR_(static_cast<std::size_t>(cfg.blockFrames), 0.0f) {
        outPtrs_[0] = outL_.data();
        outPtrs_[1] = outR_.data();
    }

    bool start(avantgarde::AudioRenderCb render, void* user) noexcept override {
        render_ = render;
        user_ = user;
        running_ = true;
        return true;
    }

    void stop() noexcept override {
        running_ = false;
    }

    void close() noexcept override {}

    int sampleRate() const noexcept override { return cfg_.sampleRate; }
    int blockFrames() const noexcept override { return cfg_.blockFrames; }
    int numInput() const noexcept override { return cfg_.numInput; }
    int numOutput() const noexcept override { return cfg_.numOutput; }
    uint64_t totalCallbacks() const noexcept override { return totalCallbacks_; }
    uint64_t xruns() const noexcept override { return 0; }

    void pump(int blocks) {
        if (!running_ || !render_) {
            return;
        }
        for (int i = 0; i < blocks; ++i) {
            std::fill(outL_.begin(), outL_.end(), 0.0f);
            std::fill(outR_.begin(), outR_.end(), 0.0f);
            avantgarde::AudioProcessContext ctx{};
            ctx.in = nullptr;
            ctx.out = outPtrs_;
            ctx.nframes = static_cast<std::size_t>(cfg_.blockFrames);
            render_(ctx, user_);
            ++totalCallbacks_;
        }
    }

private:
    avantgarde::StreamConfig cfg_{};
    avantgarde::AudioRenderCb render_{nullptr};
    void* user_{nullptr};
    bool running_{false};
    uint64_t totalCallbacks_{0};
    std::vector<float> outL_{};
    std::vector<float> outR_{};
    float* outPtrs_[2]{};
};

class MockAudioHost final : public avantgarde::IAudioHost {
public:
    std::vector<avantgarde::AudioDeviceInfo> enumerate() override {
        return {};
    }

    std::unique_ptr<avantgarde::IAudioStream> openStream(const avantgarde::StreamConfig& cfg,
                                                         const std::string&,
                                                         const std::string&,
                                                         avantgarde::NonRtNotifyCb = nullptr,
                                                         void* = nullptr) override {
        auto s = std::make_unique<MockStream>(cfg);
        stream_ = s.get();
        return s;
    }

    void pump(int blocks) {
        if (stream_) {
            stream_->pump(blocks);
        }
    }

private:
    MockStream* stream_{nullptr};
};

bool waitPatternSwitch(avantgarde::SamplerEngineLayer& engine, MockAudioHost& host, int maxBlocks = 128) {
    for (int i = 0; i < maxBlocks; ++i) {
        host.pump(1);
        if (engine.processPendingPatternSwitches()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Pattern switch: UI cache sync reflects new transport/track state") {
    auto host = std::make_shared<MockAudioHost>();

    avantgarde::SamplerEngineLayer engine{};
    avantgarde::SamplerEngineConfig cfg{};
    cfg.trackCount = 2;
    cfg.sampleRate = 48000.0;
    cfg.blockFrames = 128;
    cfg.numInput = 0;
    cfg.numOutput = 2;

    avantgarde::UiState bootstrap{};
    std::string err{};
    REQUIRE(engine.init(cfg, host, bootstrap, err));
    REQUIRE(engine.start(err));

    const fs::path wavA = writeTestWav(fs::temp_directory_path() / "ag_pattern_ui_sync_A.wav", 48000, 110.0f);
    const fs::path wavB = writeTestWav(fs::temp_directory_path() / "ag_pattern_ui_sync_B.wav", 48000, 220.0f);

    std::string clipName{};
    REQUIRE(engine.loadSampleToTrack(0, wavA.string(), clipName));
    engine.setTempo(123.0f);
    REQUIRE(engine.setTrackSpeed(0, 0.75f));
    REQUIRE(engine.setTrackMuted(0, true));

    avantgarde::UiTransportState uiTransport = bootstrap.transport;
    std::vector<avantgarde::UiTrackStateView> uiTracks = bootstrap.tracks;
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.bpm == Catch::Approx(123.0f));
    REQUIRE(uiTracks[0].clipPath == wavA.string());
    REQUIRE(uiTracks[0].stretchRatio == Catch::Approx(0.75f));
    REQUIRE(uiTracks[0].muted);

    // Switch -> Pattern 2 (по умолчанию пустой/120/скорость 1.0).
    REQUIRE(engine.requestPatternSwitchTo(2));
    REQUIRE(waitPatternSwitch(engine, *host));
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.bpm == Catch::Approx(120.0f));
    REQUIRE(uiTracks[0].clipName.empty());
    REQUIRE(uiTracks[0].clipPath.empty());
    REQUIRE(uiTracks[0].stretchRatio == Catch::Approx(1.0f));
    REQUIRE_FALSE(uiTracks[0].muted);

    // Меняем состояние Pattern 2.
    REQUIRE(engine.loadSampleToTrack(0, wavB.string(), clipName));
    engine.setTempo(140.0f);
    REQUIRE(engine.setTrackSpeed(0, 1.5f));
    (void)engine.setTrackMuted(0, false);
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.bpm == Catch::Approx(140.0f));
    REQUIRE(uiTracks[0].clipPath == wavB.string());
    REQUIRE(uiTracks[0].stretchRatio == Catch::Approx(1.5f));
    REQUIRE_FALSE(uiTracks[0].muted);

    // Назад в Pattern 1: должны вернуться clipA/bpm123/speed0.75/mute=true.
    REQUIRE(engine.requestPatternSwitchTo(1));
    REQUIRE(waitPatternSwitch(engine, *host));
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.bpm == Catch::Approx(123.0f));
    REQUIRE(uiTracks[0].clipPath == wavA.string());
    REQUIRE(uiTracks[0].stretchRatio == Catch::Approx(0.75f));
    REQUIRE(uiTracks[0].muted);

    // И снова в Pattern 2: должны вернуться clipB/bpm140/speed1.5/mute=false.
    REQUIRE(engine.requestPatternSwitchTo(2));
    REQUIRE(waitPatternSwitch(engine, *host));
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.bpm == Catch::Approx(140.0f));
    REQUIRE(uiTracks[0].clipPath == wavB.string());
    REQUIRE(uiTracks[0].stretchRatio == Catch::Approx(1.5f));
    REQUIRE_FALSE(uiTracks[0].muted);

    engine.stop();
}

TEST_CASE("Transport play/stop is reflected in UI cache without extra key press") {
    auto host = std::make_shared<MockAudioHost>();

    avantgarde::SamplerEngineLayer engine{};
    avantgarde::SamplerEngineConfig cfg{};
    cfg.trackCount = 1;
    cfg.sampleRate = 48000.0;
    cfg.blockFrames = 128;
    cfg.numInput = 0;
    cfg.numOutput = 2;

    avantgarde::UiState bootstrap{};
    std::string err{};
    REQUIRE(engine.init(cfg, host, bootstrap, err));
    REQUIRE(engine.start(err));

    avantgarde::UiTransportState uiTransport = bootstrap.transport;
    std::vector<avantgarde::UiTrackStateView> uiTracks = bootstrap.tracks;

    engine.setTransportPlaying(true);
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE(uiTransport.playing);

    engine.setTransportPlaying(false);
    REQUIRE(engine.syncUiCache(uiTransport, uiTracks));
    REQUIRE_FALSE(uiTransport.playing);

    engine.stop();
}
