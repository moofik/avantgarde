#include <catch2/catch_all.hpp>

#include <unordered_map>

#include "service/snapshot/SnapshotManager.h"

using namespace avantgarde;

namespace {

uint64_t fxMirrorKey(uint8_t track, uint8_t slot, uint16_t param) {
    return (static_cast<uint64_t>(track) << 24U) |
           (static_cast<uint64_t>(slot) << 16U) |
           static_cast<uint64_t>(param);
}

} // namespace

TEST_CASE("SnapshotManager: trigger captures slot in live mode and emits SnapshotCaptured intent") {
    SnapshotManager manager{};

    UiTrackStateView tr{};
    tr.id = 0;
    tr.fxCount = 1;
    tr.fxEnabled = {1U};
    std::vector<UiTrackStateView> tracks{tr};

    std::unordered_map<uint64_t, float> mirror{};
    mirror.emplace(fxMirrorKey(0, 0, 1), 0.25f);
    mirror.emplace(fxMirrorKey(0, 0, 2), 0.75f);

    SnapshotManager::GestureContext ctx{};
    ctx.recordEnabled = false;
    ctx.transportPlaying = true;
    ctx.selectedTrack = 0;
    ctx.selectedFx = 0;
    ctx.tracks = &tracks;
    ctx.fxParamMirror = &mirror;

    const SnapshotManager::GestureResult res = manager.handleSlotGesture(0, ctx);
    REQUIRE(res.handled);
    REQUIRE(res.changed);
    REQUIRE_FALSE(res.recallRequested);
    REQUIRE(res.applyIntents.empty());
    REQUIRE(res.sideIntents.size() == 1U);
    CHECK(res.sideIntents[0].type == UiIntentType::SnapshotCaptured);
    CHECK(res.sideIntents[0].snapshotSlot == 0U);

    const auto& slots = manager.slots();
    REQUIRE(slots[0].occupied);
    CHECK(slots[0].track == 0U);
    CHECK(slots[0].fxSlot == 0U);
    REQUIRE(slots[0].intents.size() == 3U); // SetFxEnabled + 2 params
}

TEST_CASE("SnapshotManager: trigger recalls occupied slot in rec+play mode") {
    SnapshotManager manager{};

    UiTrackStateView tr{};
    tr.id = 0;
    tr.fxCount = 1;
    tr.fxEnabled = {1U};
    std::vector<UiTrackStateView> tracks{tr};

    std::unordered_map<uint64_t, float> mirror{};
    mirror.emplace(fxMirrorKey(0, 0, 9), 0.5f);

    SnapshotManager::GestureContext captureCtx{};
    captureCtx.recordEnabled = false;
    captureCtx.transportPlaying = true;
    captureCtx.selectedTrack = 0;
    captureCtx.selectedFx = 0;
    captureCtx.tracks = &tracks;
    captureCtx.fxParamMirror = &mirror;
    (void)manager.handleSlotGesture(1, captureCtx);

    SnapshotManager::GestureContext recallCtx = captureCtx;
    recallCtx.recordEnabled = true;
    recallCtx.transportPlaying = true;

    const SnapshotManager::GestureResult res = manager.handleSlotGesture(1, recallCtx);
    REQUIRE(res.handled);
    REQUIRE(res.changed);
    REQUIRE(res.recallRequested);
    CHECK(res.recallSlot == 1U);
    CHECK(res.recallTrack == 0U);
    CHECK(res.recallFxSlot == 0U);
    REQUIRE_FALSE(res.applyIntents.empty());
    REQUIRE(res.sideIntents.size() == 1U);
    CHECK(res.sideIntents[0].type == UiIntentType::SnapshotApplied);
    CHECK(res.sideIntents[0].snapshotSlot == 1U);
}

TEST_CASE("SnapshotManager: no active fx emits ad-hoc hud info intent") {
    SnapshotManager manager{};
    std::vector<UiTrackStateView> tracks(1);

    SnapshotManager::GestureContext ctx{};
    ctx.recordEnabled = false;
    ctx.transportPlaying = false;
    ctx.selectedTrack = 0;
    ctx.selectedFx = 0;
    ctx.tracks = &tracks;

    const SnapshotManager::GestureResult res = manager.handleSlotGesture(0, ctx);
    REQUIRE(res.handled);
    REQUIRE_FALSE(res.changed);
    REQUIRE_FALSE(res.recallRequested);
    REQUIRE(res.sideIntents.size() == 1U);
    CHECK(res.sideIntents[0].type == UiIntentType::HudNotify);
    CHECK(res.sideIntents[0].hudLevel == UiHudIntentLevel::Info);
    CHECK(res.sideIntents[0].hudText == "SNAPSHOT: NO ACTIVE FX");
}

