#include "control/ControlCommandDispatcher.h"

#include <algorithm>

namespace avantgarde {

ControlCommandDispatcher::ControlCommandDispatcher(IRtCommandQueue* rtQueue) noexcept
        : rtQueue_(rtQueue) {}

bool ControlCommandDispatcher::setQuantizeMode(QuantizeMode mode) noexcept {
    return dispatch(CmdId::QuantizeMode,
                    /*track=*/kRtTrackGlobal,
                    /*slot=*/kRtSlotTrackParams,
                    /*index=*/kRtIndexUnused,
                    static_cast<float>(static_cast<uint8_t>(mode)));
}

bool ControlCommandDispatcher::setTempoBpm(float bpm) noexcept {
    return dispatch(CmdId::SetTempoBpm, /*track=*/kRtTrackGlobal, /*slot=*/kRtSlotTrackParams, /*index=*/kRtIndexUnused, bpm);
}

bool ControlCommandDispatcher::setTimeSignature(uint8_t num, uint8_t den) noexcept {
    return dispatch(CmdId::SetTimeSig,
                    /*track=*/kRtTrackGlobal,
                    /*slot=*/kRtSlotTrackParams,
                    /*index=*/static_cast<uint16_t>(den),
                    static_cast<float>(num));
}

bool ControlCommandDispatcher::sendPlay(int16_t track, int16_t slot) noexcept {
    return dispatch(CmdId::Play, track, slot, /*index=*/kRtIndexUnused, kRtValueOn);
}

bool ControlCommandDispatcher::sendStop(int16_t track, int16_t slot) noexcept {
    return dispatch(CmdId::Stop, track, slot, /*index=*/kRtIndexUnused, kRtValueOff);
}

bool ControlCommandDispatcher::sendNoteOn(int16_t track, uint8_t key, float velocity01) noexcept {
    const uint16_t note = std::clamp<uint16_t>(key, kRtMidiNoteMin, kRtMidiNoteMax);
    const float velocity = std::clamp(velocity01, 0.0f, 1.0f);
    return dispatch(CmdId::NoteOn, track, kRtSlotTrackParams, note, velocity);
}

bool ControlCommandDispatcher::sendNoteOff(int16_t track, uint8_t key) noexcept {
    const uint16_t note = std::clamp<uint16_t>(key, kRtMidiNoteMin, kRtMidiNoteMax);
    return dispatch(CmdId::NoteOff, track, kRtSlotTrackParams, note, kRtValueOff);
}

bool ControlCommandDispatcher::sendNoteDetune(int16_t track, uint8_t key, float detuneNorm) noexcept {
    const uint16_t note = std::clamp<uint16_t>(key, kRtMidiNoteMin, kRtMidiNoteMax);
    const float detune = std::clamp(detuneNorm, -1.0f, 1.0f);
    return dispatch(CmdId::NoteDetune, track, kRtSlotTrackParams, note, detune);
}

bool ControlCommandDispatcher::sendTrackParamSet(int16_t track, TrackParamId param, float value) noexcept {
    return dispatch(CmdId::ParamSet, track, kRtSlotTrackParams, toParamIndex(param), value);
}

bool ControlCommandDispatcher::sendParamSet(int16_t track, int16_t slot, uint16_t index, float value) noexcept {
    return dispatch(CmdId::ParamSet, track, slot, index, value);
}

bool ControlCommandDispatcher::dispatch(CmdId id, int16_t track, int16_t slot, uint16_t index, float value) noexcept {
    RtCommand cmd{};
    cmd.id = toWireCmdId(id);
    cmd.track = track;
    cmd.slot = slot;
    cmd.index = index;
    cmd.value = value;

    if (!rtQueue_) {
        return false;
    }
    return rtQueue_->push(cmd);
}

} // namespace avantgarde
