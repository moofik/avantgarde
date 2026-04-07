#include <catch2/catch_all.hpp>

#include "service/pattern/PatternSnapshotManager.h"

using namespace avantgarde;

namespace {

PatternState makeBasePattern(PatternId id) {
    PatternState p{};
    p.id = id;
    p.transport.bpm = 120.0f;
    p.transport.tsNum = 4;
    p.transport.tsDen = 4;
    p.transport.quant = QuantizeMode::Bar;
    p.transport.swing01 = 0.0f;

    PatternTrackSnapshot t0{};
    t0.trackId = 0;
    t0.muted = false;
    t0.armed = false;
    t0.gain01 = 1.0f;
    t0.playbackInc = 1.0f;
    t0.bars = 4;
    t0.clipRefId = 101;
    p.tracks.push_back(t0);
    return p;
}

bool hasOp(const CompiledSwitchPlan& plan, PatternApplyOpKind kind) {
    for (const auto& op : plan.ops) {
        if (op.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("PatternSnapshotManager: upsert normalizes and deduplicates params") {
    PatternSnapshotManager mgr{};

    PatternState p = makeBasePattern(1);
    REQUIRE(p.tracks.size() == 1);
    auto& t0 = p.tracks[0];

    // Нарочно неотсортированный и с дублями список.
    t0.trackParams = {
        ParamKV{.index = 7, .value = 0.3f},
        ParamKV{.index = 2, .value = 0.1f},
        ParamKV{.index = 7, .value = 0.8f},
        ParamKV{.index = 1, .value = 0.2f}
    };
    t0.fxParams = {
        PatternFxParam{.slot = 2, .index = 5, .value = 0.4f},
        PatternFxParam{.slot = 1, .index = 3, .value = 0.1f},
        PatternFxParam{.slot = 2, .index = 5, .value = 0.9f}
    };

    REQUIRE(mgr.upsert(p));

    const CompiledPatternSnapshot* snap = nullptr;
    REQUIRE(mgr.get(1, snap));
    REQUIRE(snap != nullptr);
    REQUIRE(snap->tracks.size() == 1);
    const auto& tr = snap->tracks[0];

    REQUIRE(tr.trackParams.size() == 3);
    CHECK(tr.trackParams[0].index == 1);
    CHECK(tr.trackParams[1].index == 2);
    CHECK(tr.trackParams[2].index == 7);
    CHECK(tr.trackParams[2].value == Catch::Approx(0.8f)); // last-wins

    REQUIRE(tr.fxParams.size() == 2);
    CHECK(tr.fxParams[0].slot == 1);
    CHECK(tr.fxParams[0].index == 3);
    CHECK(tr.fxParams[1].slot == 2);
    CHECK(tr.fxParams[1].index == 5);
    CHECK(tr.fxParams[1].value == Catch::Approx(0.9f)); // last-wins
}

TEST_CASE("PatternSnapshotManager: buildSwitchPlan returns only changed operations") {
    PatternSnapshotManager mgr{};

    PatternState a = makeBasePattern(10);
    a.transport.bpm = 120.0f;
    a.tracks[0].muted = false;
    a.tracks[0].gain01 = 1.0f;
    a.tracks[0].trackParams = {ParamKV{.index = 2, .value = 0.2f}};
    a.tracks[0].fxParams = {PatternFxParam{.slot = 0, .index = 1, .value = 0.1f}};

    PatternState b = makeBasePattern(11);
    b.transport.bpm = 128.0f;              // changed
    b.tracks[0].muted = true;              // changed
    b.tracks[0].gain01 = 1.0f;             // same
    b.tracks[0].trackParams = {ParamKV{.index = 2, .value = 0.6f}}; // changed
    b.tracks[0].fxParams = {PatternFxParam{.slot = 0, .index = 1, .value = 0.1f}}; // same

    REQUIRE(mgr.upsert(a));
    REQUIRE(mgr.upsert(b));

    CompiledSwitchPlan plan{};
    REQUIRE(mgr.buildSwitchPlan(10, 11, plan));
    CHECK(plan.from == 10);
    CHECK(plan.to == 11);
    CHECK(plan.ops.size() > 0);

    CHECK(hasOp(plan, PatternApplyOpKind::TransportSetTempo));
    CHECK(hasOp(plan, PatternApplyOpKind::TrackSetMuted));
    CHECK(hasOp(plan, PatternApplyOpKind::TrackParamSet));

    // Gain не менялся -> лишней операции быть не должно.
    CHECK_FALSE(hasOp(plan, PatternApplyOpKind::TrackSetGain));
}

TEST_CASE("PatternSnapshotManager: full apply from invalid source includes target state ops") {
    PatternSnapshotManager mgr{};
    PatternState b = makeBasePattern(30);
    b.transport.bpm = 135.0f;
    b.tracks[0].muted = true;
    b.tracks[0].trackParams = {ParamKV{.index = 4, .value = 1.0f}};

    REQUIRE(mgr.upsert(b));

    CompiledSwitchPlan plan{};
    REQUIRE(mgr.buildSwitchPlan(kInvalidPatternId, 30, plan));
    CHECK(plan.from == kInvalidPatternId);
    CHECK(plan.to == 30);
    CHECK(plan.ops.size() >= 3);
    CHECK(hasOp(plan, PatternApplyOpKind::TransportSetTempo));
    CHECK(hasOp(plan, PatternApplyOpKind::TrackSetMuted));
    CHECK(hasOp(plan, PatternApplyOpKind::TrackParamSet));
}

