#include <catch2/catch_all.hpp>

#include "platform/terminal/GothicGbUiRenderer.h"

using namespace avantgarde;

TEST_CASE("GothicGbUiRenderer: frame contains compact transport and track cards") {
    UiState state{};
    state.transport.playing = true;
    state.transport.bpm = 123.0f;
    state.transport.tsNum = 7;
    state.transport.tsDen = 8;
    state.transport.quant = QuantizeMode::Beat;
    state.transport.activeTrack = 1;
    state.telemetry.xruns = 2;
    state.telemetry.rtQueueOverflow = true;

    state.tracks[0].id = 0;
    state.tracks[0].state = UiTrackState::Playing;
    state.tracks[0].clipName = "kick_very_long_name.wav";
    state.tracks[0].bars = 8;
    state.tracks[0].fxCount = 1;
    state.tracks[0].loop = true;
    state.tracks[0].stretchRatio = 1.4f;
    state.tracks[0].gain01 = 0.8f;

    state.tracks[1].id = 1;
    state.tracks[1].state = UiTrackState::Stopped;
    state.tracks[1].clipName = "snare.wav";
    state.tracks[1].bars = 4;
    state.tracks[1].fxCount = 0;
    state.tracks[1].loop = false;
    state.tracks[1].stretchRatio = 0.9f;
    state.tracks[1].gain01 = 0.5f;

    const std::string frame = GothicGbUiRenderer::buildMonochromeFrame(state, 68);

    REQUIRE(frame.find("AVANTGARDE") != std::string::npos);
    REQUIRE(frame.find("TRN:PLAY BPM:123.0 TS:7/8 Q:BEAT OVF:Y") != std::string::npos);
    REQUIRE(frame.find("ACTIVE:T2 XRUN:2") != std::string::npos);
    REQUIRE(frame.find("T1 PLAY") != std::string::npos);
    REQUIRE(frame.find("▶ T2 STOP") != std::string::npos);
    REQUIRE(frame.find("bars:8  fx:1  loop:Y") != std::string::npos);
    REQUIRE(frame.find("bars:4  fx:0  loop:N") != std::string::npos);
    REQUIRE(frame.find("keys [1/2] [p/s] [-/=] [z/x/c] [[/]] [q]") != std::string::npos);
}
