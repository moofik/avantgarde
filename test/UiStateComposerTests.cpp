#include <catch2/catch_all.hpp>

#include "service/UiStateComposer.h"

using namespace avantgarde;

TEST_CASE("UiStateComposer: merges runtime telemetry with UiState snapshot") {
    UiStateComposer composer;

    UiState base{};
    base.tracks.resize(2);
    base.transport.playing = true;
    base.transport.bpm = 127.0f;
    base.transport.tsNum = 7;
    base.transport.tsDen = 8;
    base.transport.quant = QuantizeMode::Beat;
    base.transport.activeTrack = 1;
    base.transport.sampleTime = 42;

    base.tracks[0].id = 0;
    base.tracks[0].state = UiTrackState::Playing;
    base.tracks[0].clipName = "kick.wav";
    base.tracks[1].id = 1;
    base.tracks[1].state = UiTrackState::Stopped;

    UiRuntimeTelemetryView rt{};
    rt.totalCallbacks = 100;
    rt.xruns = 3;
    rt.rtQueueOverflow = true;
    rt.blockFrames = 256;

    const UiState out = composer.compose(base, rt);

    REQUIRE(out.transport.playing == true);
    REQUIRE(out.transport.bpm == Catch::Approx(127.0f));
    REQUIRE(out.transport.tsNum == 7);
    REQUIRE(out.transport.tsDen == 8);
    REQUIRE(out.transport.quant == QuantizeMode::Beat);
    REQUIRE(out.transport.activeTrack == 1);
    REQUIRE(out.transport.sampleTime == 25600);

    REQUIRE(out.tracks[0].id == 0);
    REQUIRE(out.tracks[0].state == UiTrackState::Playing);
    REQUIRE(out.tracks[0].clipName == "kick.wav");
    REQUIRE(out.tracks[1].id == 1);
    REQUIRE(out.tracks[1].state == UiTrackState::Stopped);

    REQUIRE(out.telemetry.totalCallbacks == 100);
    REQUIRE(out.telemetry.xruns == 3);
    REQUIRE(out.telemetry.rtQueueOverflow == true);
}
