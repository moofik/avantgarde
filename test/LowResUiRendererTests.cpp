#include <catch2/catch_all.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "contracts/IDisplay.h"
#include "platform/lowres/LowResUiRenderer.h"

using namespace avantgarde;

namespace {

class FakeDisplay final : public IDisplay {
public:
    explicit FakeDisplay(uint16_t w, uint16_t h)
        : w_(w), h_(h), rows_(h_, std::string(w_, ' ')) {}

    uint16_t width() const noexcept override { return w_; }
    uint16_t height() const noexcept override { return h_; }

    void beginFrame() noexcept override {}

    void clear(char fill = ' ') noexcept override {
        for (auto& r : rows_) {
            std::fill(r.begin(), r.end(), fill);
        }
    }

    void drawText(uint16_t x, uint16_t y, std::string_view text) noexcept override {
        if (y >= h_ || x >= w_) {
            return;
        }
        const std::size_t len = std::min<std::size_t>(text.size(), static_cast<std::size_t>(w_ - x));
        rows_[y].replace(static_cast<std::size_t>(x), len, text.substr(0, len));
    }

    void drawBar(uint16_t x, uint16_t y, uint16_t width, float value01) noexcept override {
        if (y >= h_ || x >= w_) {
            return;
        }
        const uint16_t w = std::min<uint16_t>(width, static_cast<uint16_t>(w_ - x));
        const float clamped = std::clamp(value01, 0.0f, 1.0f);
        const uint16_t filled = static_cast<uint16_t>(clamped * static_cast<float>(w));
        for (uint16_t i = 0; i < w; ++i) {
            rows_[y][x + i] = (i < filled) ? '#' : '.';
        }
    }

    void present() noexcept override {}

    std::string snapshot() const {
        std::string out;
        for (const auto& r : rows_) {
            out += r;
            out.push_back('\n');
        }
        return out;
    }

private:
    uint16_t w_{0};
    uint16_t h_{0};
    std::vector<std::string> rows_;
};

} // namespace

TEST_CASE("LowResUiRenderer: renders compact transport and active track only") {
    FakeDisplay display(64, 16);
    LowResUiRenderer renderer(display);

    UiState state{};
    state.tracks.resize(2);
    state.transport.playing = true;
    state.transport.bpm = 128.0f;
    state.transport.tsNum = 4;
    state.transport.tsDen = 4;
    state.transport.quant = QuantizeMode::Bar;
    state.transport.activeTrack = 0;
    state.transport.sampleTime = 2048;

    state.telemetry.totalCallbacks = 99;
    state.telemetry.xruns = 1;
    state.telemetry.rtQueueOverflow = false;

    state.tracks[0].id = 0;
    state.tracks[0].state = UiTrackState::Playing;
    state.tracks[0].bars = 8;
    state.tracks[0].fxCount = 2;
    state.tracks[0].loop = true;
    state.tracks[0].muted = false;
    state.tracks[0].armed = true;
    state.tracks[0].stretchRatio = 1.5f;
    state.tracks[0].gain01 = 0.75f;
    state.tracks[0].clipName = "kick.wav";

    state.tracks[1].id = 1;
    state.tracks[1].state = UiTrackState::Stopped;
    state.tracks[1].bars = 4;
    state.tracks[1].fxCount = 1;
    state.tracks[1].loop = false;
    state.tracks[1].muted = true;
    state.tracks[1].armed = false;
    state.tracks[1].stretchRatio = 0.9f;
    state.tracks[1].gain01 = 0.5f;
    state.tracks[1].clipName = "snare.wav";

    renderer.render(state);

    const std::string frame = display.snapshot();
    REQUIRE(frame.find("TRN PLAY 128.0 4/4 QR") != std::string::npos);
    REQUIRE(frame.find("ACT:T1 XR:1") != std::string::npos);
    REQUIRE(frame.find("OVF:N") != std::string::npos);
    REQUIRE(frame.find("T1* P kick.wav") != std::string::npos);
    REQUIRE(frame.find("T2  S snare.wav") == std::string::npos);
    REQUIRE(frame.find("bars:8 fx:2 loop:Y") != std::string::npos);
    REQUIRE(frame.find("bars:8 fx:2 loop:Y m:N a:Y") != std::string::npos);
    REQUIRE(frame.find("gn :0.75") != std::string::npos);
}

TEST_CASE("LowResUiRenderer: long clip name is shortened") {
    FakeDisplay display(64, 16);
    LowResUiRenderer renderer(display);

    UiState state{};
    state.tracks.resize(2);
    state.tracks[0].id = 0;
    state.tracks[0].state = UiTrackState::Stopped;
    state.tracks[0].clipName = "very_very_long_clip_filename.wav";

    renderer.render(state);

    const std::string frame = display.snapshot();
    REQUIRE(frame.find("very_very_long_...") != std::string::npos);
}
