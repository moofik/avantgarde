#include <catch2/catch_all.hpp>
#include <variant>

#include "service/sequencer/EventLane.h"

using namespace avantgarde;

TEST_CASE("EventLane: stores and returns snapshot recall events in time range") {
    EventLane lane{};

    EventLaneEvent ev{};
    ev.sampleTime = 96000;
    ev.op = EventLaneOp::SnapshotRecall;
    ev.target.track = 1;
    ev.target.slot = 2;
    ev.target.module = 0;
    ev.target.param = 0;
    ev.snapshotId = 3;
    const uint64_t id = lane.addEvent(ev);
    REQUIRE(id != 0);

    std::vector<EventLaneEvent> out{};
    lane.collectEventsInRange(90000, 100000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].eventId == id);
    CHECK(out[0].op == EventLaneOp::SnapshotRecall);
    CHECK(out[0].snapshotId == 3);
    CHECK(out[0].target.track == 1);
    CHECK(out[0].target.slot == 2);

    REQUIRE(lane.removeEvent(id));
    out.clear();
    lane.collectEventsInRange(0, 200000, out);
    CHECK(out.empty());
}

TEST_CASE("EventLane: keeps typed payload in stored events") {
    EventLane lane{};

    EventLaneEvent evA{};
    evA.tick = 24;
    evA.sampleTime = 1200;
    evA.op = EventLaneOp::SnapshotRecall;
    evA.payload = EventSnapshotRecallPayload{.snapshotId = 2};
    const uint64_t idA = lane.addEvent(evA);
    REQUIRE(idA != 0);

    EventLaneEvent evB{};
    evB.tick = 48;
    evB.sampleTime = 2400;
    evB.op = EventLaneOp::TrackMuteSet;
    evB.payload = EventTrackMutePayload{.muted = true};
    const uint64_t idB = lane.addEvent(evB);
    REQUIRE(idB != 0);

    std::vector<EventLaneEvent> out{};
    lane.collectEventsInRange(0, 5000, out);
    REQUIRE(out.size() == 2);

    REQUIRE(std::holds_alternative<EventSnapshotRecallPayload>(out[0].payload));
    CHECK(std::get<EventSnapshotRecallPayload>(out[0].payload).snapshotId == 2);

    REQUIRE(std::holds_alternative<EventTrackMutePayload>(out[1].payload));
    CHECK(std::get<EventTrackMutePayload>(out[1].payload).muted);
}

TEST_CASE("EventLane: updateEvent and nudgeEventTime keep deterministic order") {
    EventLane lane{};

    EventLaneEvent a{};
    a.sampleTime = 1000;
    a.op = EventLaneOp::TrackMuteSet;
    a.payload = EventTrackMutePayload{.muted = false};
    const uint64_t idA = lane.addEvent(a);
    REQUIRE(idA != 0);

    EventLaneEvent b{};
    b.sampleTime = 2000;
    b.op = EventLaneOp::SnapshotRecall;
    b.payload = EventSnapshotRecallPayload{.snapshotId = 3};
    const uint64_t idB = lane.addEvent(b);
    REQUIRE(idB != 0);

    EventLaneEvent nextB = b;
    nextB.sampleTime = 900;
    nextB.op = EventLaneOp::TrackArmSet;
    nextB.payload = EventTrackArmPayload{.armed = true};
    REQUIRE(lane.updateEvent(idB, nextB));
    REQUIRE(lane.nudgeEventTime(idA, 300));

    std::vector<EventLaneEvent> out{};
    lane.collectEventsInRange(0, 5000, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].eventId == idB);
    CHECK(out[0].sampleTime == 900);
    CHECK(out[0].op == EventLaneOp::TrackArmSet);
    CHECK(out[1].eventId == idA);
    CHECK(out[1].sampleTime == 1300);
}
