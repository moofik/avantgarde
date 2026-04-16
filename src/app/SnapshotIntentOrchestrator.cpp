#include "app/SnapshotIntentOrchestrator.h"

namespace avantgarde {

bool SnapshotIntentOrchestrator::supports(const UiIntent& intent) const noexcept {
    return intent.type == UiIntentType::SnapshotTriggerSlot ||
           intent.type == UiIntentType::SnapshotCaptureSlot ||
           intent.type == UiIntentType::SnapshotRecallSlot;
}

SnapshotIntentOrchestrator::Result SnapshotIntentOrchestrator::dispatch(const UiIntent& intent,
                                                                        const Context& ctx) {
    Result out{};
    if (!supports(intent)) {
        return out;
    }

    if (ctx.tracks == nullptr || ctx.intentApplier == nullptr || ctx.applierContext == nullptr) {
        return out;
    }

    const UiScene scene = (ctx.nav != nullptr) ? ctx.nav->scene : UiScene::Tracks;
    const auto selectedTrack = [&]() -> uint8_t {
        if (ctx.tracks->empty()) {
            return 0U;
        }
        const uint8_t raw = (ctx.nav != nullptr) ? ctx.nav->selectedTrack : 0U;
        return (raw >= ctx.tracks->size()) ? static_cast<uint8_t>(ctx.tracks->size() - 1U) : raw;
    }();
    const bool trackArmed =
        (!ctx.tracks->empty() && selectedTrack < ctx.tracks->size()) ? (*ctx.tracks)[selectedTrack].armed : false;

    SnapshotManager::GestureResult snapshot{};
    if (intent.type == UiIntentType::SnapshotRecallSlot) {
        snapshot = snapshotManager_.buildRecallResult(intent.snapshotSlot);
    } else if (intent.type == UiIntentType::SnapshotCaptureSlot) {
        static const std::unordered_map<uint64_t, float> kEmptyMirror{};
        const auto& mirror = (ctx.fxParamMirror != nullptr) ? *ctx.fxParamMirror : kEmptyMirror;
        snapshot = snapshotManager_.captureSlot(
            intent.snapshotSlot,
            intent.track,
            intent.fxSlot,
            *ctx.tracks,
            mirror);
    } else {
        // Trigger policy (централизовано в orchestration):
        // 1) SnapshotCaptured допускается только в FxEditor.
        // 2) В Tracks при ARM+REC trigger делает recall (SnapshotApplied).
        // 3) Во всех остальных случаях trigger показывает OPEN FX EDITOR.
        if (scene == UiScene::FxEditor) {
            SnapshotManager::GestureContext triggerCtx{};
            triggerCtx.recordEnabled = ctx.recordEnabled;
            triggerCtx.transportPlaying = ctx.transportPlaying;
            if (ctx.nav != nullptr) {
                triggerCtx.selectedTrack = ctx.nav->selectedTrack;
                triggerCtx.selectedFx = ctx.nav->selectedFx;
            }
            triggerCtx.tracks = ctx.tracks;
            triggerCtx.fxParamMirror = ctx.fxParamMirror;
            snapshot = snapshotManager_.handleSlotGesture(intent.snapshotSlot, triggerCtx);
        } else if (scene == UiScene::Tracks && ctx.recordEnabled && trackArmed) {
            snapshot = snapshotManager_.buildRecallResult(intent.snapshotSlot);
        } else {
            UiIntent hud{};
            hud.type = UiIntentType::HudNotify;
            hud.hudLevel = UiHudIntentLevel::Info;
            hud.hudText = "OPEN FX EDITOR";
            (void)ctx.intentApplier->apply(hud, *ctx.applierContext);
            out.handled = true;
            out.changed = false;
            return out;
        }
    }

    out.handled = snapshot.handled;
    if (!snapshot.handled) {
        return out;
    }

    bool appliedAny = false;
    const bool restoreRecallGuard = (ctx.snapshotRecallDispatchFlag != nullptr);
    bool prevRecallGuard = false;
    if (restoreRecallGuard) {
        prevRecallGuard = *ctx.snapshotRecallDispatchFlag;
        *ctx.snapshotRecallDispatchFlag = snapshot.recallRequested;
    }

    for (const UiIntent& applyIntent : snapshot.applyIntents) {
        const bool applied = ctx.intentApplier->apply(applyIntent, *ctx.applierContext);
        if (applied) {
            appliedAny = true;
            if (ctx.onIntentApplied) {
                ctx.onIntentApplied(applyIntent);
            }
        }
    }

    if (restoreRecallGuard) {
        *ctx.snapshotRecallDispatchFlag = prevRecallGuard;
    }

    // Side-intent-ы (HUD и подобные) применяем тем же пайплайном.
    // Это keeps behavior consistent: все побочные реакции остаются intent-driven.
    if (!snapshot.sideIntents.empty() &&
        (snapshot.applyIntents.empty() || appliedAny || snapshot.recallRequested)) {
        for (const UiIntent& sideIntent : snapshot.sideIntents) {
            (void)ctx.intentApplier->apply(sideIntent, *ctx.applierContext);
        }
    }

    out.changed = appliedAny || snapshot.changed;
    out.recallRequested = snapshot.recallRequested;
    out.recallSlot = snapshot.recallSlot;
    out.recallTrack = snapshot.recallTrack;
    out.recallFxSlot = snapshot.recallFxSlot;
    return out;
}

} // namespace avantgarde
