#include <catch2/catch_all.hpp>

#include "service/sequencer/AutomationLane.h"
#include "service/sequencer/EventLane.h"
#include "service/sequencer/SequencerDispatchPlanner.h"

using namespace avantgarde;

namespace {

TransportRtSnapshot makeTransport() {
    TransportRtSnapshot s{};
    s.playing = true;
    s.tsNum = 4;
    s.tsDen = 4;
    s.ppq = 96;
    s.bpm = 120.0f;
    s.quant = QuantizeMode::None;
    s.swing = 0.0f;
    s.sampleTime = 0;
    return s;
}

} // namespace

TEST_CASE("SequencerDispatchPlanner: Event lane wins order on same sampleTime") {
    AutomationLane automation{};
    EventLane events{};

    // automation at sample 48000
    SequencerParamTarget target{};
    target.track = 0;
    target.slot = -1;
    target.module = 0;
    target.param = 2;
    REQUIRE(automation.beginGesture(target, AutomationInterpolationMode::Linear));
    REQUIRE(automation.pushGesturePoint(48000, 0.6f));
    AutomationGestureCommitResult commit{};
    REQUIRE(automation.commitGesture(makeTransport(), QuantizeMode::None, commit));

    // event at same sample 48000
    EventLaneEvent ev{};
    ev.sampleTime = 48000;
    ev.op = EventLaneOp::SnapshotRecall;
    ev.snapshotId = 1;
    const uint64_t evId = events.addEvent(ev);
    REQUIRE(evId != 0);

    std::vector<SequencerDispatchItem> plan{};
    SequencerDispatchPlanner::buildRange(events, automation, 0, 96000, plan);
    REQUIRE(plan.size() == 2);
    CHECK(plan[0].sampleTime == 48000);
    CHECK(plan[1].sampleTime == 48000);
    CHECK(plan[0].source == SequencerDispatchItem::Source::Event);
    CHECK(plan[1].source == SequencerDispatchItem::Source::Automation);
}

