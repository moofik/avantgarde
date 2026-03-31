#include "control/ControlCommandDispatcher.h"

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
