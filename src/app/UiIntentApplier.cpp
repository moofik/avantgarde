#include "app/UiIntentApplier.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "contracts/FxRegistry.h"
#include "contracts/ids.h"
#include "module/BufferFxModule.h"
#include "module/SchroederReverbModule.h"
#include "module/StutterModule.h"
#include "service/audio/BpmDetectorService.h"
#include "service/ui/hud/HudNotificationsLayer.h"

namespace avantgarde {
namespace {

// Фабрика встроенных FX-модулей для MVP.
// source-of-truth по доступным профилям лежит в FxRegistry.
std::unique_ptr<IAudioModule> createBuiltinFxByCanonicalId(std::string_view canonicalId) {
    if (canonicalId == FxRegistry::kReverbSchroederId) {
        return std::make_unique<SchroederReverbModule>();
    }
    if (canonicalId == FxRegistry::kStutterId) {
        return std::make_unique<StutterModule>();
    }
    if (canonicalId == FxRegistry::kBufferFxId) {
        return std::make_unique<BufferFxModule>();
    }
    // Остальные профили можно добавить сюда по мере реализации модулей.
    return nullptr;
}

UiTrackPlaybackProfile profileFromTrackView(const UiTrackStateView& track) noexcept {
    if (track.playbackMode == UiTrackPlaybackMode::Note) {
        return track.loop ? UiTrackPlaybackProfile::Pattern : UiTrackPlaybackProfile::PatternOnce;
    }
    return track.loop ? UiTrackPlaybackProfile::Loop : UiTrackPlaybackProfile::OneShot;
}

UiTrackPlaybackProfile profileFromIntentValue(float raw) noexcept {
    const int v = std::clamp(static_cast<int>(std::lround(raw)), 0, 3);
    switch (v) {
        case 0: return UiTrackPlaybackProfile::Pattern;
        case 1: return UiTrackPlaybackProfile::PatternOnce;
        case 2: return UiTrackPlaybackProfile::Loop;
        case 3:
        default:
            return UiTrackPlaybackProfile::OneShot;
    }
}

TrackPlaybackProfileValue toEngineProfile(UiTrackPlaybackProfile profile) noexcept {
    switch (profile) {
        case UiTrackPlaybackProfile::Pattern: return TrackPlaybackProfileValue::Pattern;
        case UiTrackPlaybackProfile::PatternOnce: return TrackPlaybackProfileValue::PatternOnce;
        case UiTrackPlaybackProfile::Loop: return TrackPlaybackProfileValue::Loop;
        case UiTrackPlaybackProfile::OneShot:
        default:
            return TrackPlaybackProfileValue::OneShot;
    }
}

} // namespace

uint8_t UiIntentApplier::clampTrack_(uint8_t track, const std::vector<UiTrackStateView>& tracks) noexcept {
    if (tracks.empty()) {
        return 0;
    }
    return (track >= tracks.size()) ? static_cast<uint8_t>(tracks.size() - 1U) : track;
}

float UiIntentApplier::quantModeToIntentValue_(QuantizeMode mode) noexcept {
    switch (mode) {
        case QuantizeMode::None: return 0.0f;
        case QuantizeMode::Beat: return 1.0f;
        case QuantizeMode::Bar: return 2.0f;
        default: return 2.0f;
    }
}

QuantizeMode UiIntentApplier::quantModeFromIntentValue_(float value) noexcept {
    const int q = static_cast<int>(std::lround(value));
    if (q <= 0) {
        return QuantizeMode::None;
    }
    if (q == 1) {
        return QuantizeMode::Beat;
    }
    return QuantizeMode::Bar;
}

void UiIntentApplier::refreshTrackViewState_(uint8_t track,
                                             const UiTransportState& transport,
                                             std::vector<UiTrackStateView>& tracks) noexcept {
    if (tracks.empty()) {
        return;
    }
    const uint8_t t = clampTrack_(track, tracks);
    UiTrackStateView& tr = tracks[t];
    if (tr.clipName.empty()) {
        tr.state = UiTrackState::Empty;
    } else if (transport.playing) {
        tr.state = UiTrackState::Playing;
    } else {
        tr.state = UiTrackState::Stopped;
    }
}

bool UiIntentApplier::buildUndoIntent(const UiIntent& forward,
                                      const UiTransportState& transport,
                                      const std::vector<UiTrackStateView>& tracks,
                                      UiIntent& undoOut) const {
    undoOut = UiIntent{};
    switch (forward.type) {
        case UiIntentType::SetActiveTrack: {
            if (tracks.empty()) {
                return false;
            }
            undoOut = forward;
            undoOut.track = clampTrack_(transport.activeTrack, tracks);
            return true;
        }
        case UiIntentType::SetTrackMuted: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].muted ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackArmed: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].armed ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackLooperMode: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = (tracks[t].playbackMode == UiTrackPlaybackMode::Looper) ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackPlaybackProfile: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = static_cast<float>(static_cast<uint8_t>(profileFromTrackView(tracks[t])));
            return true;
        }
        case UiIntentType::SetTrackSpeed: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].stretchRatio;
            return true;
        }
        case UiIntentType::SetTrackTempoSync: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].tempoSync ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackGain: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].gain01;
            return true;
        }
        case UiIntentType::SetTrackTrimStart: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].trimStart01;
            return true;
        }
        case UiIntentType::SetTrackTrimEnd: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracks[t].trimEnd01;
            return true;
        }
        case UiIntentType::SetTransportQuant:
            undoOut = forward;
            undoOut.value = quantModeToIntentValue_(transport.quant);
            return true;
        case UiIntentType::SetTransportBpm:
            undoOut = forward;
            undoOut.value = transport.bpm;
            return true;
        case UiIntentType::SetMetronomeEnabled:
            undoOut = forward;
            undoOut.value = transport.metronomeEnabled ? 1.0f : 0.0f;
            return true;
        case UiIntentType::SetTransportPlaying:
            undoOut = forward;
            undoOut.value = transport.playing ? 1.0f : 0.0f;
            return true;
        case UiIntentType::SetFxEnabled: {
            if (tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(forward.track, tracks);
            if (tracks[t].fxCount == 0U) {
                return false;
            }
            const uint8_t slot = (forward.fxSlot >= tracks[t].fxCount)
                                     ? static_cast<uint8_t>(tracks[t].fxCount - 1U)
                                     : forward.fxSlot;
            const bool enabled =
                (slot < tracks[t].fxEnabled.size()) ? (tracks[t].fxEnabled[slot] != 0U) : true;
            undoOut = forward;
            undoOut.track = t;
            undoOut.fxSlot = slot;
            undoOut.value = enabled ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::ClearTrackSample:
        case UiIntentType::SnapshotTriggerSlot:
        case UiIntentType::SnapshotCaptureSlot:
        case UiIntentType::SnapshotRecallSlot:
        case UiIntentType::SnapshotCaptured:
        case UiIntentType::SnapshotApplied:
        case UiIntentType::HudNotify:
        default:
            return false;
    }
}

bool UiIntentApplier::apply(const UiIntent& intent, Context& ctx) const {
    switch (intent.type) {
        case UiIntentType::LoadSampleToTrack: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            std::string clipName;
            if (!ctx.engine.loadSampleToTrack(t, intent.path, clipName)) {
                return false;
            }
            ctx.tracks[t].clipName = clipName;
            ctx.tracks[t].clipPath = intent.path;
            ctx.tracks[t].muted = false;
            const UiTrackPlaybackProfile profile = profileFromTrackView(ctx.tracks[t]);
            // После загрузки клипа повторно применяем профиль трека,
            // чтобы mode/loop/policy оставались консистентными.
            (void)ctx.engine.setTrackPlaybackProfile(t, toEngineProfile(profile));
            refreshTrackViewState_(t, ctx.transport, ctx.tracks);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::ClearTrackSample: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const bool hasClip = !ctx.tracks[t].clipPath.empty() || !ctx.tracks[t].clipName.empty();
            if (!hasClip) {
                return false;
            }
            if (!ctx.engine.clearTrackSample(t)) {
                return false;
            }
            ctx.tracks[t].clipName.clear();
            ctx.tracks[t].clipPath.clear();
            ctx.tracks[t].loop = false;
            ctx.tracks[t].trimStart01 = 0.0f;
            ctx.tracks[t].trimEnd01 = 1.0f;
            refreshTrackViewState_(t, ctx.transport, ctx.tracks);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::PreviewRequest:
            // Preview и global transport разделены:
            // preview-запрос не переключает transport-состояние.
            ctx.engine.previewRequest(intent.path,
                                      intent.previewSpeed,
                                      intent.previewStart01,
                                      intent.previewEnd01,
                                      intent.previewGain);
            if (ctx.nav) {
                ctx.nav->previewPlaying = !intent.path.empty();
                ctx.nav->previewTrack = intent.track;
            }
            return true;
        case UiIntentType::PreviewStop:
            ctx.engine.previewStop();
            if (ctx.nav) {
                ctx.nav->previewPlaying = false;
            }
            return true;
        case UiIntentType::SetActiveTrack: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t next = clampTrack_(intent.track, ctx.tracks);
            if (next == ctx.transport.activeTrack) {
                return false;
            }
            ctx.transport.activeTrack = next;
            ctx.uiStore.setTransport(ctx.transport);
            return true;
        }
        case UiIntentType::SetTrackMuted: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const bool muted = (intent.value >= 0.5f);
            if (ctx.tracks[t].muted == muted) {
                return false;
            }
            ctx.tracks[t].muted = muted;
            (void)ctx.engine.setTrackMuted(t, muted);
            refreshTrackViewState_(t, ctx.transport, ctx.tracks);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackArmed: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const bool armed = (intent.value >= 0.5f);
            if (ctx.tracks[t].armed == armed) {
                return false;
            }
            ctx.tracks[t].armed = armed;
            (void)ctx.engine.setTrackArmed(t, armed);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackLooperMode: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const bool looperEnabled = (intent.value >= 0.5f);
            const UiTrackPlaybackProfile nextProfile =
                looperEnabled ? UiTrackPlaybackProfile::Loop : UiTrackPlaybackProfile::PatternOnce;
            if (profileFromTrackView(ctx.tracks[t]) == nextProfile) {
                return false;
            }
            if (!ctx.engine.setTrackPlaybackProfile(t, toEngineProfile(nextProfile))) {
                return false;
            }
            ctx.tracks[t].playbackProfile = nextProfile;
            ctx.tracks[t].playbackMode =
                (nextProfile == UiTrackPlaybackProfile::Pattern ||
                 nextProfile == UiTrackPlaybackProfile::PatternOnce)
                    ? UiTrackPlaybackMode::Note
                    : UiTrackPlaybackMode::Looper;
            ctx.tracks[t].loop =
                (nextProfile == UiTrackPlaybackProfile::Pattern ||
                 nextProfile == UiTrackPlaybackProfile::Loop);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackPlaybackProfile: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const UiTrackPlaybackProfile nextProfile = profileFromIntentValue(intent.value);
            if (profileFromTrackView(ctx.tracks[t]) == nextProfile) {
                return false;
            }
            if (!ctx.engine.setTrackPlaybackProfile(t, toEngineProfile(nextProfile))) {
                return false;
            }
            ctx.tracks[t].playbackProfile = nextProfile;
            ctx.tracks[t].playbackMode =
                (nextProfile == UiTrackPlaybackProfile::Pattern ||
                 nextProfile == UiTrackPlaybackProfile::PatternOnce)
                    ? UiTrackPlaybackMode::Note
                    : UiTrackPlaybackMode::Looper;
            ctx.tracks[t].loop =
                (nextProfile == UiTrackPlaybackProfile::Pattern ||
                 nextProfile == UiTrackPlaybackProfile::Loop);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackSpeed: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const float next = std::clamp(intent.value, 0.25f, 4.0f);
            if (std::fabs(ctx.tracks[t].stretchRatio - next) < 1e-6f) {
                return false;
            }
            ctx.tracks[t].stretchRatio = next;
            (void)ctx.engine.setTrackSpeed(t, ctx.tracks[t].stretchRatio);
            // Ручной speed отключает tempo-sync по контракту.
            ctx.tracks[t].tempoSync = false;
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackTempoSync: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const bool enabled = (intent.value >= 0.5f);
            if (ctx.tracks[t].tempoSync == enabled) {
                return false;
            }
            if (enabled) {
                if (!ctx.engine.setTrackTempoSync(t, true)) {
                    return false;
                }
                ctx.tracks[t].tempoSync = true;
                if (ctx.hud) {
                    ctx.hud->notifyText("SYNC ON -> SPEED AUTO", HudNotificationLevel::Action);
                }
            } else {
                // UX-политика:
                // SYNC OFF не просто отключает привязку к BPM, но и сбрасывает SPEED в 1.00,
                // чтобы получить предсказуемое "нейтральное" поведение.
                if (!ctx.engine.setTrackSpeed(t, 1.0f)) {
                    return false;
                }
                ctx.tracks[t].tempoSync = false;
                ctx.tracks[t].stretchRatio = 1.0f;
                if (ctx.hud) {
                    ctx.hud->notifyText("SYNC OFF -> SPEED 1.00", HudNotificationLevel::Action);
                }
            }
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackGain: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const float next = std::clamp(intent.value, 0.0f, 1.0f);
            if (std::fabs(ctx.tracks[t].gain01 - next) < 1e-6f) {
                return false;
            }
            ctx.tracks[t].gain01 = next;
            (void)ctx.engine.setTrackParam(t, toParamIndex(TrackParamId::Gain01), ctx.tracks[t].gain01);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackTrimStart: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            float next = std::clamp(intent.value, 0.0f, 0.99f);
            if (next >= ctx.tracks[t].trimEnd01 - 0.01f) {
                next = std::max(0.0f, ctx.tracks[t].trimEnd01 - 0.01f);
            }
            if (std::fabs(ctx.tracks[t].trimStart01 - next) < 1e-6f) {
                return false;
            }
            ctx.tracks[t].trimStart01 = next;
            (void)ctx.engine.setTrackParam(t, toParamIndex(TrackParamId::StartNorm), next);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTrackTrimEnd: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            float next = std::clamp(intent.value, 0.01f, 1.0f);
            if (next <= ctx.tracks[t].trimStart01 + 0.01f) {
                next = std::min(1.0f, ctx.tracks[t].trimStart01 + 0.01f);
            }
            if (std::fabs(ctx.tracks[t].trimEnd01 - next) < 1e-6f) {
                return false;
            }
            ctx.tracks[t].trimEnd01 = next;
            (void)ctx.engine.setTrackParam(t, toParamIndex(TrackParamId::EndNorm), next);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetTransportQuant: {
            const QuantizeMode next = quantModeFromIntentValue_(intent.value);
            if (ctx.transport.quant == next) {
                return false;
            }
            ctx.transport.quant = next;
            ctx.engine.setQuantize(ctx.transport.quant);
            ctx.uiStore.setTransport(ctx.transport);
            return true;
        }
        case UiIntentType::SetTransportBpm: {
            const float next = std::clamp(intent.value, 20.0f, 300.0f);
            if (std::fabs(ctx.transport.bpm - next) < 1e-6f) {
                return false;
            }
            ctx.transport.bpm = next;
            ctx.engine.setTempo(ctx.transport.bpm);
            ctx.uiStore.setTransport(ctx.transport);
            return true;
        }
        case UiIntentType::SetMetronomeEnabled: {
            const bool enabled = (intent.value >= 0.5f);
            if (ctx.transport.metronomeEnabled == enabled) {
                return false;
            }
            ctx.transport.metronomeEnabled = enabled;
            ctx.engine.setMetronomeEnabled(enabled);
            ctx.uiStore.setTransport(ctx.transport);
            return true;
        }
        case UiIntentType::DetectProjectBpmFromTrack: {
            if (ctx.hud == nullptr) {
                return false;
            }
            if (ctx.tracks.empty()) {
                ctx.hud->notifyText("can't detect", HudNotificationLevel::Action);
                return true;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            if (ctx.tracks[t].clipPath.empty()) {
                ctx.hud->notifyText("can't detect", HudNotificationLevel::Action);
                return true;
            }

            // Важно: берем speed не из UI-кэша, а из актуального engine snapshot.
            // Это гарантирует, что BPM detect учитывает текущий pitch/speed трека
            // даже сразу после tempo-sync пересчета.
            float effectiveTrackSpeed = ctx.tracks[t].stretchRatio;
            {
                UiTransportState transportSnapshot = ctx.transport;
                std::vector<UiTrackStateView> tracksSnapshot = ctx.tracks;
                if (ctx.engine.syncUiCache(transportSnapshot, tracksSnapshot) &&
                    t < tracksSnapshot.size()) {
                    effectiveTrackSpeed = tracksSnapshot[t].stretchRatio;
                }
            }

            BpmDetectorService detector{};
            const BpmDetectionResult det = detector.detectFromFile(
                ctx.tracks[t].clipPath,
                effectiveTrackSpeed);
            if (!det.ok) {
                ctx.hud->notifyText("can't detect", HudNotificationLevel::Action);
                return true;
            }

            const float detected = std::clamp(
                (det.effectiveBpm > 0.0f) ? det.effectiveBpm : det.sourceBpm,
                20.0f,
                300.0f);
            char msg[32]{};
            std::snprintf(msg, sizeof(msg), "BPM: %.1f", detected);
            ctx.hud->notifyText(msg, HudNotificationLevel::Action);
            // Важно: больше не меняем transport/tempo и не останавливаем play.
            return true;
        }
        case UiIntentType::SetTransportPlaying: {
            const bool playing = (intent.value >= 0.5f);
            if (ctx.transport.playing == playing) {
                return false;
            }
            ctx.transport.playing = playing;
            ctx.engine.setTransportPlaying(playing);
            for (std::size_t i = 0; i < ctx.tracks.size(); ++i) {
                refreshTrackViewState_(static_cast<uint8_t>(i), ctx.transport, ctx.tracks);
                ctx.uiStore.setTrack(i, ctx.tracks[i]);
            }
            ctx.uiStore.setTransport(ctx.transport);
            return true;
        }
        case UiIntentType::SwitchPatternPrev: {
            if (!ctx.engine.requestPatternSwitchRelative(-1)) {
                return false;
            }
            ctx.uiStore.setPattern(ctx.engine.patternUiState());
            return true;
        }
        case UiIntentType::SwitchPatternNext: {
            if (!ctx.engine.requestPatternSwitchRelative(1)) {
                return false;
            }
            ctx.uiStore.setPattern(ctx.engine.patternUiState());
            return true;
        }
        case UiIntentType::SwitchPatternSet: {
            const int raw = static_cast<int>(std::lround(intent.value));
            if (raw < 1) {
                return false;
            }
            if (!ctx.engine.requestPatternSwitchTo(static_cast<PatternId>(raw))) {
                return false;
            }
            ctx.uiStore.setPattern(ctx.engine.patternUiState());
            return true;
        }
        case UiIntentType::RemoveFxFromTrack: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            if (ctx.tracks[t].fxCount == 0U) {
                return false;
            }
            const uint8_t slot = (intent.fxSlot >= ctx.tracks[t].fxCount)
                                     ? static_cast<uint8_t>(ctx.tracks[t].fxCount - 1U)
                                     : intent.fxSlot;
            if (!ctx.engine.removeFxFromTrack(t, slot)) {
                return false;
            }

            // Приводим UI-модель цепочки в консистентное состояние после удаления слота.
            const std::size_t oldCount = ctx.tracks[t].fxCount;
            if (ctx.tracks[t].fxChainIds.size() < oldCount) {
                ctx.tracks[t].fxChainIds.resize(oldCount, std::string(FxRegistry::kUnknownFxId));
            }
            if (slot < ctx.tracks[t].fxChainIds.size()) {
                ctx.tracks[t].fxChainIds.erase(
                    ctx.tracks[t].fxChainIds.begin() + static_cast<std::ptrdiff_t>(slot));
            }
            if (slot < ctx.tracks[t].fxEnabled.size()) {
                ctx.tracks[t].fxEnabled.erase(
                    ctx.tracks[t].fxEnabled.begin() + static_cast<std::ptrdiff_t>(slot));
            }
            const std::size_t nextCount = oldCount > 0U ? oldCount - 1U : 0U;
            if (ctx.tracks[t].fxChainIds.size() > nextCount) {
                ctx.tracks[t].fxChainIds.resize(nextCount);
            }
            if (ctx.tracks[t].fxEnabled.size() > nextCount) {
                ctx.tracks[t].fxEnabled.resize(nextCount, 1U);
            }
            ctx.tracks[t].fxCount = static_cast<uint8_t>(std::min<std::size_t>(nextCount, 255U));
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::OpenScene: {
            if (ctx.nav == nullptr) {
                return false;
            }
            ctx.nav->scene = intent.scene;
            if (intent.resetCursor) {
                ctx.nav->cursor = 0;
            }
            if (intent.resetScroll) {
                ctx.nav->scroll = 0;
            }
            if (intent.resetSceneActionIndex) {
                ctx.nav->sceneActionIndex = 0;
            }
            if (intent.resetSelectedFx) {
                ctx.nav->selectedFx = 0;
            }
            if (intent.closeFxAddPopup) {
                ctx.nav->fxAddPopupOpen = false;
            }
            // Навигационный intent не влияет на undo/redo и model-layer.
            return false;
        }
        case UiIntentType::Back: {
            if (ctx.nav == nullptr) {
                return false;
            }
            // Новый контракт: Back может нести целевую сцену явно.
            if (intent.scene != UiScene::Tracks || intent.resetCursor || intent.resetScroll || intent.resetSceneActionIndex) {
                ctx.nav->scene = intent.scene;
                if (intent.resetCursor) {
                    ctx.nav->cursor = 0;
                }
                if (intent.resetScroll) {
                    ctx.nav->scroll = 0;
                }
                if (intent.resetSceneActionIndex) {
                    ctx.nav->sceneActionIndex = 0;
                }
                if (intent.resetSelectedFx) {
                    ctx.nav->selectedFx = 0;
                }
                if (intent.closeFxAddPopup) {
                    ctx.nav->fxAddPopupOpen = false;
                }
                return false;
            }
            // Legacy fallback: FxEditor -> FxList, иначе -> Tracks.
            if (ctx.nav->scene == UiScene::FxEditor) {
                ctx.nav->scene = UiScene::FxList;
            } else {
                ctx.nav->scene = UiScene::Tracks;
            }
            ctx.nav->sceneActionIndex = 0;
            return false;
        }
        case UiIntentType::HudNotify: {
            if (ctx.hud == nullptr) {
                return false;
            }
            if (intent.hudEvent != UiHudIntentEvent::None) {
                switch (intent.hudEvent) {
                    case UiHudIntentEvent::SnapshotCaptured:
                        ctx.hud->notify(
                            HudEventId::SnapshotCaptured,
                            HudEventPayload{
                                .slot = static_cast<int>(intent.hudSlot),
                                .text = {}
                            });
                        return true;
                    case UiHudIntentEvent::SnapshotApplied:
                        ctx.hud->notify(
                            HudEventId::SnapshotApplied,
                            HudEventPayload{
                                .slot = static_cast<int>(intent.hudSlot),
                                .text = {}
                            });
                        return true;
                    case UiHudIntentEvent::None:
                    default:
                        break;
                }
            }
            if (intent.hudText.empty()) {
                return false;
            }
            HudNotificationLevel level = HudNotificationLevel::Info;
            switch (intent.hudLevel) {
                case UiHudIntentLevel::Action: level = HudNotificationLevel::Action; break;
                case UiHudIntentLevel::Critical: level = HudNotificationLevel::Critical; break;
                case UiHudIntentLevel::Info:
                default:
                    level = HudNotificationLevel::Info;
                    break;
            }
            ctx.hud->notifyText(intent.hudText, level);
            return true;
        }
        case UiIntentType::SnapshotCaptured: {
            if (ctx.hud == nullptr) {
                return false;
            }
            ctx.hud->notify(
                HudEventId::SnapshotCaptured,
                HudEventPayload{
                    .slot = static_cast<int>(intent.snapshotSlot + 1U),
                    .text = {}
                });
            return true;
        }
        case UiIntentType::SnapshotApplied: {
            if (ctx.hud == nullptr) {
                return false;
            }
            ctx.hud->notify(
                HudEventId::SnapshotApplied,
                HudEventPayload{
                    .slot = static_cast<int>(intent.snapshotSlot + 1U),
                    .text = {}
                });
            return true;
        }
        case UiIntentType::None:
        case UiIntentType::SnapshotTriggerSlot:
        case UiIntentType::SnapshotCaptureSlot:
        case UiIntentType::SnapshotRecallSlot:
        case UiIntentType::EnginePlayTrack:
        case UiIntentType::EngineStopTrack:
        case UiIntentType::EngineSetQuant:
        case UiIntentType::EngineSetBpm:
        case UiIntentType::EngineSetTrackSpeed:
        default:
            return false;
        case UiIntentType::AddFxToTrack: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            const FxDescriptor* descriptor = FxRegistry::find(intent.path);
            if (!descriptor) {
                return false;
            }
            std::unique_ptr<IAudioModule> fx = createBuiltinFxByCanonicalId(descriptor->id);
            if (!fx) {
                return false;
            }
            if (!ctx.engine.addFxToTrack(t, std::move(fx))) {
                return false;
            }
            // Синхронизируем UI-модель слотов:
            // добиваем пропущенные позиции "unknown", если fxCount уже был больше длины списка.
            if (ctx.tracks[t].fxChainIds.size() < ctx.tracks[t].fxCount) {
                ctx.tracks[t].fxChainIds.resize(ctx.tracks[t].fxCount, std::string(FxRegistry::kUnknownFxId));
            }
            if (ctx.tracks[t].fxEnabled.size() < ctx.tracks[t].fxCount) {
                ctx.tracks[t].fxEnabled.resize(ctx.tracks[t].fxCount, 1U);
            }
            ctx.tracks[t].fxChainIds.push_back(std::string(descriptor->id));
            ctx.tracks[t].fxEnabled.push_back(1U);
            const std::size_t visibleCount = std::min<std::size_t>(ctx.tracks[t].fxChainIds.size(), 255U);
            ctx.tracks[t].fxCount = static_cast<uint8_t>(visibleCount);
            if (ctx.tracks[t].fxEnabled.size() > visibleCount) {
                ctx.tracks[t].fxEnabled.resize(visibleCount, 1U);
            }
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetFxEnabled: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            if (ctx.tracks[t].fxCount == 0U) {
                return false;
            }
            const uint8_t slot = (intent.fxSlot >= ctx.tracks[t].fxCount)
                                     ? static_cast<uint8_t>(ctx.tracks[t].fxCount - 1U)
                                     : intent.fxSlot;
            const bool enabled = (intent.value >= 0.5f);
            if (ctx.tracks[t].fxEnabled.size() < ctx.tracks[t].fxCount) {
                ctx.tracks[t].fxEnabled.resize(ctx.tracks[t].fxCount, 1U);
            }
            const bool oldEnabled = (ctx.tracks[t].fxEnabled[slot] != 0U);
            if (oldEnabled == enabled) {
                return false;
            }
            if (!ctx.engine.setFxEnabled(t, slot, enabled)) {
                return false;
            }
            ctx.tracks[t].fxEnabled[slot] = enabled ? 1U : 0U;
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::SetFxParam: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            return ctx.engine.setFxParam(t, intent.fxSlot, intent.paramIndex, intent.value);
        }
    }
}

} // namespace avantgarde
