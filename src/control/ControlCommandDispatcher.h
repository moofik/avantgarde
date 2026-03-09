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
    bool sendPlay(int16_t track, int16_t slot) noexcept;
    bool sendStop(int16_t track, int16_t slot) noexcept;
    bool sendParamSet(int16_t track, int16_t slot, uint16_t index, float value) noexcept;

private:
    bool dispatch(CmdId id, int16_t track, int16_t slot, uint16_t index, float value) noexcept;

    IRtCommandQueue* rtQueue_{nullptr};
};

} // namespace avantgarde
