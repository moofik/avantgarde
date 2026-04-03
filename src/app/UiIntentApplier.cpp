#include "app/UiIntentApplier.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "contracts/FxRegistry.h"
#include "module/SchroederReverbModule.h"
#include "module/StutterModule.h"
#include "service/audio/BpmDetectorService.h"

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
    // Остальные профили можно добавить сюда по мере реализации модулей.
    return nullptr;
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
        case UiIntentType::SetTransportQuant:
            undoOut = forward;
            undoOut.value = quantModeToIntentValue_(transport.quant);
            return true;
        case UiIntentType::SetTransportBpm:
            undoOut = forward;
            undoOut.value = transport.bpm;
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
            ctx.tracks[t].loop = true;
            refreshTrackViewState_(t, ctx.transport, ctx.tracks);
            ctx.uiStore.setTrack(t, ctx.tracks[t]);
            return true;
        }
        case UiIntentType::PreviewRequest:
            ctx.engine.previewRequest(intent.path);
            return true;
        case UiIntentType::PreviewStop:
            ctx.engine.previewStop();
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
        case UiIntentType::DetectProjectBpmFromTrack: {
            if (ctx.tracks.empty()) {
                return false;
            }
            const uint8_t t = clampTrack_(intent.track, ctx.tracks);
            if (ctx.tracks[t].clipPath.empty()) {
                return false;
            }

            BpmDetectorService detector{};
            const BpmDetectionResult det = detector.detectFromFile(
                ctx.tracks[t].clipPath,
                ctx.tracks[t].stretchRatio);
            if (!det.ok) {
                return false;
            }

            bool changed = false;
            if (ctx.transport.playing) {
                ctx.transport.playing = false;
                ctx.engine.setTransportPlaying(false);
                for (std::size_t i = 0; i < ctx.tracks.size(); ++i) {
                    refreshTrackViewState_(static_cast<uint8_t>(i), ctx.transport, ctx.tracks);
                    ctx.uiStore.setTrack(i, ctx.tracks[i]);
                }
                changed = true;
            }

            const float next = std::clamp(det.effectiveBpm, 20.0f, 300.0f);
            if (std::fabs(ctx.transport.bpm - next) >= 1e-6f) {
                ctx.transport.bpm = next;
                ctx.engine.setTempo(ctx.transport.bpm);
                changed = true;
            }

            if (changed) {
                ctx.uiStore.setTransport(ctx.transport);
            }
            return changed;
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
        case UiIntentType::OpenScene:
        case UiIntentType::Back:
        case UiIntentType::None:
        case UiIntentType::OpenFxEditor:
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
