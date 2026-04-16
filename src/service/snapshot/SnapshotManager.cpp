#include "service/snapshot/SnapshotManager.h"

#include <algorithm>
#include <cstddef>

namespace avantgarde {

uint8_t SnapshotManager::clampTrack_(uint8_t track, const std::vector<UiTrackStateView>& tracks) noexcept {
    if (tracks.empty()) {
        return 0U;
    }
    return (track >= tracks.size()) ? static_cast<uint8_t>(tracks.size() - 1U) : track;
}

uint64_t SnapshotManager::makeFxParamMirrorKey_(uint8_t track,
                                                uint8_t fxSlot,
                                                uint16_t paramIndex) noexcept {
    return (static_cast<uint64_t>(track) << 24U) |
           (static_cast<uint64_t>(fxSlot) << 16U) |
           static_cast<uint64_t>(paramIndex);
}

UiIntent SnapshotManager::makeSnapshotNoticeIntent_(UiIntentType type, uint8_t slot) {
    UiIntent notice{};
    notice.type = type;
    notice.snapshotSlot = static_cast<uint8_t>(std::min<uint8_t>(slot, 3U));
    return notice;
}

UiIntent SnapshotManager::makeHudTextIntent_(std::string text, UiHudIntentLevel level) {
    UiIntent hud{};
    hud.type = UiIntentType::HudNotify;
    hud.hudEvent = UiHudIntentEvent::None;
    hud.hudLevel = level;
    hud.hudText = std::move(text);
    return hud;
}

bool SnapshotManager::buildCaptureIntents_(uint8_t track,
                                           uint8_t fxSlot,
                                           const std::vector<UiTrackStateView>& tracks,
                                           const std::unordered_map<uint64_t, float>& fxParamMirror,
                                           std::vector<UiIntent>& outIntents) const {
    outIntents.clear();
    if (tracks.empty()) {
        return false;
    }
    const uint8_t safeTrack = clampTrack_(track, tracks);
    const UiTrackStateView& tr = tracks[safeTrack];
    if (tr.fxCount == 0U) {
        return false;
    }

    const uint8_t safeFxSlot = static_cast<uint8_t>(
        std::min<uint16_t>(fxSlot, static_cast<uint16_t>(tr.fxCount - 1U)));

    // 1) Состояние bypass слота.
    UiIntent enabled{};
    enabled.type = UiIntentType::SetFxEnabled;
    enabled.track = safeTrack;
    enabled.fxSlot = safeFxSlot;
    enabled.value =
        (safeFxSlot < tr.fxEnabled.size() && tr.fxEnabled[safeFxSlot] == 0U) ? 0.0f : 1.0f;
    outIntents.push_back(std::move(enabled));

    // 2) Все известные param mirror для (track, slot), отсортированные по paramIndex.
    std::vector<std::pair<uint16_t, float>> params{};
    params.reserve(16U);
    for (const auto& kv : fxParamMirror) {
        const uint8_t keyTrack = static_cast<uint8_t>((kv.first >> 24U) & 0xFFU);
        const uint8_t keySlot = static_cast<uint8_t>((kv.first >> 16U) & 0xFFU);
        if (keyTrack != safeTrack || keySlot != safeFxSlot) {
            continue;
        }
        const uint16_t paramIndex = static_cast<uint16_t>(kv.first & 0xFFFFU);
        params.emplace_back(paramIndex, kv.second);
    }
    std::sort(params.begin(), params.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [paramIndex, value] : params) {
        UiIntent fx{};
        fx.type = UiIntentType::SetFxParam;
        fx.track = safeTrack;
        fx.fxSlot = safeFxSlot;
        fx.paramIndex = paramIndex;
        fx.value = value;
        outIntents.push_back(std::move(fx));
    }
    return !outIntents.empty();
}

SnapshotManager::GestureResult SnapshotManager::buildRecallResult(uint8_t slotIndex) const {
    GestureResult out{};
    out.handled = true;
    if (slotIndex >= kSlotCount) {
        return out;
    }
    const SlotState& slot = slots_[slotIndex];
    if (!slot.occupied || slot.intents.empty()) {
        return out;
    }

    out.changed = true;
    out.recallRequested = true;
    out.recallSlot = slotIndex;
    out.recallTrack = slot.track;
    out.recallFxSlot = slot.fxSlot;
    out.applyIntents = slot.intents;
    out.sideIntents.push_back(makeSnapshotNoticeIntent_(UiIntentType::SnapshotApplied, slotIndex));
    return out;
}

SnapshotManager::GestureResult SnapshotManager::captureSlot(
    uint8_t slotIndex,
    uint8_t track,
    uint8_t fxSlot,
    const std::vector<UiTrackStateView>& tracks,
    const std::unordered_map<uint64_t, float>& fxParamMirror) {
    GestureResult out{};
    out.handled = true;
    if (slotIndex >= kSlotCount || tracks.empty()) {
        return out;
    }
    const uint8_t safeTrack = clampTrack_(track, tracks);
    const UiTrackStateView& tr = tracks[safeTrack];
    if (tr.fxCount == 0U) {
        out.sideIntents.push_back(
            makeHudTextIntent_("SNAPSHOT: NO ACTIVE FX", UiHudIntentLevel::Info));
        return out;
    }
    const uint8_t safeFxSlot = static_cast<uint8_t>(
        std::min<uint16_t>(fxSlot, static_cast<uint16_t>(tr.fxCount - 1U)));

    std::vector<UiIntent> captureIntents{};
    if (!buildCaptureIntents_(safeTrack, safeFxSlot, tracks, fxParamMirror, captureIntents)) {
        return out;
    }
    SlotState& dst = slots_[slotIndex];
    dst.occupied = true;
    dst.track = safeTrack;
    dst.fxSlot = safeFxSlot;
    dst.intents = std::move(captureIntents);

    out.changed = true;
    out.sideIntents.push_back(makeSnapshotNoticeIntent_(UiIntentType::SnapshotCaptured, slotIndex));
    return out;
}

SnapshotManager::GestureResult SnapshotManager::handleSlotGesture(uint8_t slotIndex,
                                                                  const GestureContext& ctx) {
    GestureResult out{};
    out.handled = true;
    if (slotIndex >= kSlotCount || ctx.tracks == nullptr) {
        return out;
    }
    const std::vector<UiTrackStateView>& tracks = *ctx.tracks;
    if (tracks.empty()) {
        return out;
    }
    const uint8_t safeTrack = clampTrack_(ctx.selectedTrack, tracks);
    const SlotState& slot = slots_[slotIndex];

    // REC+PLAY+occupied: режим recall.
    if (ctx.recordEnabled && ctx.transportPlaying && slot.occupied) {
        return buildRecallResult(slotIndex);
    }

    // Capture из текущего выбранного FX-контекста.
    const std::unordered_map<uint64_t, float> emptyMirror{};
    const auto& mirror = (ctx.fxParamMirror == nullptr) ? emptyMirror : *ctx.fxParamMirror;
    return captureSlot(slotIndex, safeTrack, ctx.selectedFx, tracks, mirror);
}

} // namespace avantgarde
