#pragma once

#include <cstdint>

#include "contracts/IRtCommandQueue.h"
#include "contracts/ITransport.h"
#include "contracts/ids.h"

namespace avantgarde {

class ControlCommandDispatcher {
public:
    explicit ControlCommandDispatcher(IRtCommandQueue* rtQueue) noexcept;

    bool setQuantizeMode(QuantizeMode mode) noexcept;
    bool setTempoBpm(float bpm) noexcept;
    bool setTimeSignature(uint8_t num, uint8_t den) noexcept;
    bool sendPlay(int16_t track, int16_t slot) noexcept;
    bool sendStop(int16_t track, int16_t slot) noexcept;
    bool sendNoteOn(int16_t track, uint8_t key, float velocity01) noexcept;
    bool sendNoteOff(int16_t track, uint8_t key) noexcept;
    bool sendNoteDetune(int16_t track, uint8_t key, float detuneNorm) noexcept;
    bool sendTrackParamSet(int16_t track, TrackParamId param, float value) noexcept;
    bool sendParamSet(int16_t track, int16_t slot, uint16_t index, float value) noexcept;

private:
    bool dispatch(CmdId id, int16_t track, int16_t slot, uint16_t index, float value) noexcept;

    IRtCommandQueue* rtQueue_{nullptr};
};

} // namespace avantgarde
