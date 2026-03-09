#include "control/ControlCommandDispatcher.h"

namespace avantgarde {

ControlCommandDispatcher::ControlCommandDispatcher(IRtCommandQueue* rtQueue) noexcept
        : rtQueue_(rtQueue) {}

bool ControlCommandDispatcher::setQuantizeMode(QuantizeMode mode) noexcept {
    return dispatch(CmdId::QuantizeMode,
                    /*track=*/-1,
                    /*slot=*/-1,
                    /*index=*/0,
                    static_cast<float>(static_cast<uint8_t>(mode)));
}

bool ControlCommandDispatcher::sendPlay(int16_t track, int16_t slot) noexcept {
    return dispatch(CmdId::Play, track, slot, /*index=*/0, 1.0f);
}

bool ControlCommandDispatcher::sendStop(int16_t track, int16_t slot) noexcept {
    return dispatch(CmdId::Stop, track, slot, /*index=*/0, 0.0f);
}

bool ControlCommandDispatcher::sendParamSet(int16_t track, int16_t slot, uint16_t index, float value) noexcept {
    return dispatch(CmdId::ParamSet, track, slot, index, value);
}

bool ControlCommandDispatcher::dispatch(CmdId id, int16_t track, int16_t slot, uint16_t index, float value) noexcept {
    RtCommand cmd{};
    cmd.id = static_cast<uint16_t>(id);
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
