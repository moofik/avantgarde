#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "contracts/IUiGestureInput.h"
#include "platform/raspi/RpiUiConfig.h"

namespace avantgarde::raspi {

// Источник низкоуровневого input для Linux/Raspberry.
// Аналог роли MacPrimitiveWindowInput: собирает платформенные события
// и отдает унифицированные PrimitiveInputEvent.
class RpiPrimitiveWindowInput final {
public:
    RpiPrimitiveWindowInput() = default;
    ~RpiPrimitiveWindowInput();

    bool init(const RpiUiConfig& config, std::string& errorOut);
    void shutdown() noexcept;

    bool pollPlatformEvents() noexcept;
    bool readNextInputEvent(PrimitiveInputEvent& out) noexcept;

private:
#if defined(__linux__)
    bool handleLinuxKeyEvent_(uint16_t code, int32_t value, uint64_t timestampMs) noexcept;
#endif

    std::mutex mutex_{};
    std::deque<PrimitiveInputEvent> queue_{};

#if defined(__linux__)
    int inputFd_{-1};
    bool shiftHeld_{false};
#endif
};

} // namespace avantgarde::raspi
