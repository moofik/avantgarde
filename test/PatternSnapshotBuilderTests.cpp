#include <catch2/catch_all.hpp>

#include <array>

#include "contracts/ITrack.h"
#include "service/pattern/PatternEngine.h"
#include "service/pattern/PatternSnapshotBuilder.h"
#include "service/pattern/PatternSnapshotOrchestrator.h"

using namespace avantgarde;

namespace {

class FakeSnapshotable final : public ISnapshotable {
public:
    FakeSnapshotable(uint8_t trackId, const TrackSnapshot& snap)
        : trackId_(trackId),
          snap_(snap) {}

    bool getSnapshot(SnapshotRecord& out) const noexcept override {
        out = SnapshotRecord{};
        out.domain = SnapshotDomain::Track;
        out.entityId = static_cast<int32_t>(trackId_);
        out.track = snap_;
        return true;
    }

private:
    uint8_t trackId_{0};
    TrackSnapshot snap_{};
};

class FakeTransportSnapshotable final : public ISnapshotable {
public:
    explicit FakeTransportSnapshotable(const PatternTransportSnapshot& snap)
        : snap_(snap) {}

    bool getSnapshot(SnapshotRecord& out) const noexcept override {
        out = SnapshotRecord{};
        out.domain = SnapshotDomain::Transport;
        out.entityId = kSnapshotEntityTransport;
        out.transport.playing = false;
        out.transport.bpm = snap_.bpm;
        out.transport.tsNum = snap_.tsNum;
        out.transport.tsDen = snap_.tsDen;
        out.transport.quant = snap_.quant;
        out.transport.swing01 = snap_.swing01;
        out.transport.sampleTime = 0;
        return true;
    }

private:
    PatternTransportSnapshot snap_{};
};

class FakeInvalidTrackSnapshotable final : public ISnapshotable {
public:
    explicit FakeInvalidTrackSnapshotable(const TrackSnapshot& snap)
        : snap_(snap) {}

    bool getSnapshot(SnapshotRecord& out) const noexcept override {
        out = SnapshotRecord{};
        out.domain = SnapshotDomain::Track;
        // Намеренно некорректный entityId для проверки strict-политики.
        out.entityId = kSnapshotEntityUnset;
        out.track = snap_;
        return true;
    }

private:
    TrackSnapshot snap_{};
};

PatternTransportSnapshot makeTransport(float bpm = 120.0f) {
    PatternTransportSnapshot s{};
    s.bpm = bpm;
    s.tsNum = 4;
    s.tsDen = 4;
    s.quant = QuantizeMode::Beat;
    s.swing01 = 0.15f;
    return s;
}

} // namespace

TEST_CASE("PatternSnapshotBuilder: makeDefaultPattern creates default track snapshots") {
    PatternSnapshotBuilder builder{};
    const PatternState state = builder.makeDefaultPattern(7, 3, makeTransport());

    REQUIRE(state.id == 7);
    REQUIRE(state.tracks.size() == 3);
    CHECK(state.transport.bpm == Catch::Approx(120.0f));
    CHECK(state.lengthBars == 64u);
    CHECK(state.lengthInSteps == 64u);
    CHECK(state.stepsPerBeat == 4u);

    for (std::size_t i = 0; i < state.tracks.size(); ++i) {
        const PatternTrackSnapshot& tr = state.tracks[i];
        CHECK(tr.trackId == i);
        CHECK(tr.clipRefId == 0u);
        CHECK(tr.trackParams.size() >= 5u);
    }
}

TEST_CASE("PatternSnapshotBuilder: buildFromSnapshotables maps live track state") {
    PatternSnapshotBuilder builder{};

    TrackSnapshot t0{};
    t0.muted = true;
    t0.armed = true;
    t0.gain01 = 0.42f;
    t0.playbackInc = 1.25f;
    t0.bars = 16u;
    t0.clipRefId = 77u;
    t0.playbackMode = TrackPlaybackModeValue::Note;
    t0.loopEnabled = false;
    t0.tempoSync = false;
    t0.trimStart01 = 0.1f;
    t0.trimEnd01 = 0.8f;

    TrackSnapshot t1{};
    t1.clipRefId = 11u;
    t1.bars = 8u;

    FakeSnapshotable s0{0, t0};
    FakeSnapshotable s1{1, t1};
    FakeTransportSnapshotable transport{makeTransport(128.0f)};
    // Проверяем strict pipeline по entityId:
    // порядок в списке sources не должен влиять на маппинг track snapshots.
    std::array<ISnapshotable*, 3> sources{{&s1, &transport, &s0}};

    PatternState out{};
    REQUIRE(builder.buildFromSnapshotables(3, sources, out));
    REQUIRE(out.tracks.size() == 2u);
    CHECK(out.transport.bpm == Catch::Approx(128.0f));

    const PatternTrackSnapshot& tr0 = out.tracks[0];
    CHECK(tr0.muted);
    CHECK(tr0.armed);
    CHECK(tr0.gain01 == Catch::Approx(0.42f));
    CHECK(tr0.playbackInc == Catch::Approx(1.25f));
    CHECK(tr0.bars == 16u);
    CHECK(tr0.clipRefId == 77u);

    const PatternTrackSnapshot& tr1 = out.tracks[1];
    CHECK(tr1.clipRefId == 11u);
    CHECK(tr1.bars == 8u);
}

TEST_CASE("PatternSnapshotBuilder: strict mode rejects missing transport snapshot") {
    PatternSnapshotBuilder builder{};
    TrackSnapshot t0{};
    t0.clipRefId = 77u;
    FakeSnapshotable s0{0, t0};
    std::array<ISnapshotable*, 1> sources{{&s0}};
    PatternState out{};
    REQUIRE_FALSE(builder.buildFromSnapshotables(9, sources, out));
}

TEST_CASE("PatternSnapshotBuilder: strict mode rejects invalid track entityId") {
    PatternSnapshotBuilder builder{};
    TrackSnapshot t0{};
    t0.clipRefId = 99u;
    FakeInvalidTrackSnapshotable badTrack{t0};
    FakeTransportSnapshotable transport{makeTransport(123.0f)};
    std::array<ISnapshotable*, 2> sources{{&transport, &badTrack}};
    PatternState out{};
    REQUIRE_FALSE(builder.buildFromSnapshotables(10, sources, out));
}

TEST_CASE("PatternSnapshotOrchestrator: captures active pattern into engine bank") {
    PatternEngine engine{48000.0};
    PatternSnapshotOrchestrator orchestrator{engine};

    REQUIRE(orchestrator.putDefaultPattern(1, makeTransport(120.0f), 2));
    REQUIRE(engine.setActivePattern(1));

    TrackSnapshot t0{};
    t0.clipRefId = 100u;
    t0.bars = 12u;
    t0.gain01 = 0.75f;

    TrackSnapshot t1{};
    t1.clipRefId = 200u;
    t1.bars = 6u;
    t1.muted = true;

    FakeSnapshotable s0{0, t0};
    FakeSnapshotable s1{1, t1};
    FakeTransportSnapshotable transport{makeTransport(132.0f)};
    std::array<ISnapshotable*, 3> sources{{&transport, &s0, &s1}};

    REQUIRE(orchestrator.captureActivePattern(sources));

    PatternState stored{};
    REQUIRE(engine.bank().get(1, stored));
    REQUIRE(stored.tracks.size() == 2u);
    CHECK(stored.transport.bpm == Catch::Approx(132.0f));
    CHECK(stored.tracks[0].clipRefId == 100u);
    CHECK(stored.tracks[0].bars == 12u);
    CHECK(stored.tracks[0].gain01 == Catch::Approx(0.75f));
    CHECK(stored.tracks[1].clipRefId == 200u);
    CHECK(stored.tracks[1].bars == 6u);
    CHECK(stored.tracks[1].muted);
}
