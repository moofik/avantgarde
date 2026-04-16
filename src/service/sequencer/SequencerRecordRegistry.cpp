#include "service/sequencer/SequencerRecordRegistry.h"

#include <array>

#include "contracts/ids.h"

namespace avantgarde {
namespace {

// ВАЖНО:
// Это главный allow-list для automation-записи.
// Любой intent вне списка игнорируется секвенсором.
constexpr std::array<UiIntentType, 5> kAutomationIntents{
    UiIntentType::SetTrackSpeed,
    UiIntentType::SetTrackGain,
    UiIntentType::SetTrackTrimStart,
    UiIntentType::SetTrackTrimEnd,
    UiIntentType::SetFxParam,
};

// ВАЖНО:
// Это главный allow-list для event-записи.
// ARM и TrackMode intentionally НЕ включены.
constexpr std::array<UiIntentType, 4> kEventIntents{
    UiIntentType::SetTrackMuted,
    UiIntentType::SetFxEnabled,
    UiIntentType::EnginePlayTrack,
    UiIntentType::EngineStopTrack,
};

template <std::size_t N>
bool containsIntent(const std::array<UiIntentType, N>& arr, UiIntentType type) noexcept {
    for (const UiIntentType x : arr) {
        if (x == type) {
            return true;
        }
    }
    return false;
}

} // namespace

bool SequencerRecordRegistry::isAutomationIntent(UiIntentType type) noexcept {
    return containsIntent(kAutomationIntents, type);
}

bool SequencerRecordRegistry::isEventIntent(UiIntentType type) noexcept {
    return containsIntent(kEventIntents, type);
}

bool SequencerRecordRegistry::mapAutomationTarget(const UiIntent& intent,
                                                  SequencerParamTarget& outTarget) noexcept {
    outTarget = SequencerParamTarget{};
    if (!isAutomationIntent(intent.type)) {
        return false;
    }
    switch (intent.type) {
        case UiIntentType::SetTrackSpeed:
            outTarget.track = static_cast<int16_t>(intent.track);
            outTarget.slot = kRtSlotTrackParams;
            outTarget.param = toParamIndex(TrackParamId::PlaybackInc);
            return true;
        case UiIntentType::SetTrackGain:
            outTarget.track = static_cast<int16_t>(intent.track);
            outTarget.slot = kRtSlotTrackParams;
            outTarget.param = toParamIndex(TrackParamId::Gain01);
            return true;
        case UiIntentType::SetTrackTrimStart:
            outTarget.track = static_cast<int16_t>(intent.track);
            outTarget.slot = kRtSlotTrackParams;
            outTarget.param = toParamIndex(TrackParamId::StartNorm);
            return true;
        case UiIntentType::SetTrackTrimEnd:
            outTarget.track = static_cast<int16_t>(intent.track);
            outTarget.slot = kRtSlotTrackParams;
            outTarget.param = toParamIndex(TrackParamId::EndNorm);
            return true;
        case UiIntentType::SetFxParam:
            outTarget.track = static_cast<int16_t>(intent.track);
            outTarget.slot = static_cast<int16_t>(intent.fxSlot);
            outTarget.param = intent.paramIndex;
            return true;
        default:
            return false;
    }
}

bool SequencerRecordRegistry::mapEvent(const UiIntent& intent,
                                       uint64_t sampleTime,
                                       EventLaneEvent& outEvent) noexcept {
    outEvent = EventLaneEvent{};
    if (!isEventIntent(intent.type)) {
        return false;
    }
    outEvent.sampleTime = sampleTime;
    switch (intent.type) {
        case UiIntentType::SetTrackMuted:
            outEvent.op = EventLaneOp::TrackMuteSet;
            outEvent.target.track = static_cast<int16_t>(intent.track);
            outEvent.value = (intent.value >= 0.5f) ? 1.0f : 0.0f;
            outEvent.payload = EventTrackMutePayload{intent.value >= 0.5f};
            return true;
        case UiIntentType::SetFxEnabled:
            outEvent.op = EventLaneOp::FxBypassSet;
            outEvent.target.track = static_cast<int16_t>(intent.track);
            outEvent.target.slot = static_cast<int16_t>(intent.fxSlot);
            outEvent.value = (intent.value >= 0.5f) ? 1.0f : 0.0f;
            outEvent.payload = EventFxBypassPayload{intent.value >= 0.5f};
            return true;
        case UiIntentType::EnginePlayTrack:
            outEvent.op = EventLaneOp::NoteOn;
            outEvent.target.track = static_cast<int16_t>(intent.track);
            outEvent.value = 100.0f / 127.0f;
            outEvent.payload = EventNoteOnPayload{.note = 60, .velocity = 100, .detune = 0.0f};
            return true;
        case UiIntentType::EngineStopTrack:
            outEvent.op = EventLaneOp::NoteOff;
            outEvent.target.track = static_cast<int16_t>(intent.track);
            outEvent.value = 0.0f;
            outEvent.payload = EventNoteOffPayload{.note = 60};
            return true;
        default:
            return false;
    }
}

} // namespace avantgarde

