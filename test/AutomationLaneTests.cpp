#include <catch2/catch_all.hpp>

#include "service/sequencer/AutomationLane.h"

using namespace avantgarde;

namespace {

TransportRtSnapshot makeTransport(float bpm, uint8_t tsNum, uint8_t tsDen) {
    TransportRtSnapshot s{};
    s.playing = true;
    s.tsNum = tsNum;
    s.tsDen = tsDen;
    s.ppq = 96;
    s.bpm = bpm;
    s.quant = QuantizeMode::Beat;
    s.swing = 0.0f;
    s.sampleTime = 0;
    return s;
}

SequencerParamTarget makeTarget(uint16_t param) {
    SequencerParamTarget t{};
    t.track = 0;
    t.slot = -1;
    t.module = 0;
    t.param = param;
    return t;
}

} // namespace

TEST_CASE("AutomationLane: quantized commit shifts full gesture to next beat") {
    AutomationLane lane{};
    REQUIRE(lane.beginGesture(makeTarget(2), AutomationInterpolationMode::Linear));
    REQUIRE(lane.pushGesturePoint(1000, 0.25f));
    REQUIRE(lane.pushGesturePoint(1300, 0.75f));

    AutomationGestureCommitResult commit{};
    const TransportRtSnapshot tr = makeTransport(120.0f, 4, 4);
    REQUIRE(lane.commitGesture(tr, QuantizeMode::Beat, commit));
    CHECK(commit.insertedPoints == 2);
    CHECK(commit.quantizedStartSample == 24000); // 120 BPM @ 48k -> 24000 samples per beat

    std::vector<AutomationPointEvent> out{};
    lane.collectEventsInRange(0, 50000, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].point.sampleTime == 24000);
    CHECK(out[1].point.sampleTime == 24300);
    CHECK(out[0].point.value == Catch::Approx(0.25f));
    CHECK(out[1].point.value == Catch::Approx(0.75f));
}

TEST_CASE("AutomationLane: undo/redo works by gesture batch") {
    AutomationLane lane{};
    const TransportRtSnapshot tr = makeTransport(120.0f, 4, 4);

    REQUIRE(lane.beginGesture(makeTarget(1), AutomationInterpolationMode::Linear));
    REQUIRE(lane.pushGesturePoint(100, 0.1f));
    AutomationGestureCommitResult c1{};
    REQUIRE(lane.commitGesture(tr, QuantizeMode::None, c1));

    REQUIRE(lane.beginGesture(makeTarget(1), AutomationInterpolationMode::Linear));
    REQUIRE(lane.pushGesturePoint(200, 0.2f));
    AutomationGestureCommitResult c2{};
    REQUIRE(lane.commitGesture(tr, QuantizeMode::None, c2));

    std::vector<AutomationPointEvent> out{};
    lane.collectEventsInRange(0, 1000, out);
    REQUIRE(out.size() == 2);

    REQUIRE(lane.undoLastGesture());
    out.clear();
    lane.collectEventsInRange(0, 1000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].point.sampleTime == 100);

    REQUIRE(lane.undoLastGesture());
    out.clear();
    lane.collectEventsInRange(0, 1000, out);
    CHECK(out.empty());

    REQUIRE(lane.redoLastGesture());
    out.clear();
    lane.collectEventsInRange(0, 1000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].point.sampleTime == 100);

    REQUIRE(lane.redoLastGesture());
    out.clear();
    lane.collectEventsInRange(0, 1000, out);
    REQUIRE(out.size() == 2);
}

TEST_CASE("AutomationLane: direct edit API add/remove/nudge/value works") {
    AutomationLane lane{};
    const SequencerParamTarget target = makeTarget(3);

    const uint64_t idA = lane.addPoint(target, AutomationInterpolationMode::Linear, 1000, 0.25f);
    const uint64_t idB = lane.addPoint(target, AutomationInterpolationMode::Linear, 2000, 0.75f);
    REQUIRE(idA != 0);
    REQUIRE(idB != 0);

    REQUIRE(lane.nudgeEventTime(idB, -750));
    REQUIRE(lane.setEventValue(idA, 0.5f));
    REQUIRE(lane.setEventTime(idA, 1500));

    std::vector<AutomationPointEvent> out{};
    lane.collectEventsInRange(0, 5000, out);
    REQUIRE(out.size() == 2);

    // После смещения/перестановки порядок должен оставаться отсортированным по времени.
    CHECK(out[0].point.sampleTime == 1250);
    CHECK(out[1].point.sampleTime == 1500);
    CHECK(out[1].point.value == Catch::Approx(0.5f));

    REQUIRE(lane.removeEvent(idB));
    out.clear();
    lane.collectEventsInRange(0, 5000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].eventId == idA);
}
