#include <catch2/catch_all.hpp>

#include "service/sequencer/SequencerRecordRegistry.h"

using namespace avantgarde;

TEST_CASE("SequencerRecordRegistry: disallows ARM and mode-like intents") {
    CHECK_FALSE(SequencerRecordRegistry::isEventIntent(UiIntentType::SetTrackArmed));
    CHECK_FALSE(SequencerRecordRegistry::isAutomationIntent(UiIntentType::SetTrackPlaybackProfile));
}

TEST_CASE("SequencerRecordRegistry: maps allowed automation and event intents") {
    UiIntent fx{};
    fx.type = UiIntentType::SetFxParam;
    fx.track = 1;
    fx.fxSlot = 2;
    fx.paramIndex = 7;
    fx.value = 0.42f;

    SequencerParamTarget target{};
    REQUIRE(SequencerRecordRegistry::mapAutomationTarget(fx, target));
    CHECK(target.track == 1);
    CHECK(target.slot == 2);
    CHECK(target.param == 7);

    UiIntent mute{};
    mute.type = UiIntentType::SetTrackMuted;
    mute.track = 3;
    mute.value = 1.0f;
    EventLaneEvent ev{};
    REQUIRE(SequencerRecordRegistry::mapEvent(mute, 1234U, ev));
    CHECK(ev.sampleTime == 1234U);
    CHECK(ev.op == EventLaneOp::TrackMuteSet);
    CHECK(ev.target.track == 3);
}

