#include "platform/raspi/RpiPrimitiveWindowInput.h"

#include "app/AppDiagnostics.h"
#include "platform/raspi/RpiPrimitiveInputMapper.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace avantgarde::raspi {

RpiPrimitiveWindowInput::~RpiPrimitiveWindowInput() {
    shutdown();
}

bool RpiPrimitiveWindowInput::init(const RpiUiConfig& config, std::string& errorOut) {
    errorOut.clear();
#if defined(__linux__)
    shutdown();
    shiftHeld_ = false;

    if (config.inputDevice.empty()) {
        AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveWindowInput: input disabled (empty device path)");
        return true;
    }

    const int fd = ::open(config.inputDevice.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        AppDiagnostics::logf(AppLogLevel::Warn,
                             "RpiPrimitiveWindowInput: cannot open input device '%s': %s",
                             config.inputDevice.c_str(),
                             std::strerror(errno));
        // Не фейлим init всего приложения: UI может работать без физического input.
        return true;
    }
    inputFd_ = fd;
    AppDiagnostics::logf(AppLogLevel::Info,
                         "RpiPrimitiveWindowInput: attached to '%s'",
                         config.inputDevice.c_str());
    return true;
#else
    (void)config;
    AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveWindowInput: non-linux platform, stub backend");
    return true;
#endif
}

void RpiPrimitiveWindowInput::shutdown() noexcept {
#if defined(__linux__)
    if (inputFd_ >= 0) {
        ::close(inputFd_);
        inputFd_ = -1;
    }
    shiftHeld_ = false;
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

bool RpiPrimitiveWindowInput::pollPlatformEvents() noexcept {
#if defined(__linux__)
    if (inputFd_ < 0) {
        return false;
    }

    bool anyEvents = false;
    for (;;) {
        input_event ev{};
        const ssize_t n = ::read(inputFd_, &ev, sizeof(ev));
        if (n == static_cast<ssize_t>(sizeof(ev))) {
            if (ev.type != EV_KEY) {
                continue;
            }
            const uint64_t tsMs = static_cast<uint64_t>(ev.time.tv_sec) * 1000ULL +
                                  static_cast<uint64_t>(ev.time.tv_usec / 1000ULL);
            anyEvents = handleLinuxKeyEvent_(ev.code, ev.value, tsMs) || anyEvents;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiPrimitiveWindowInput: read error: %s",
                                 std::strerror(errno));
        }
        break;
    }
    return anyEvents;
#else
    return false;
#endif
}

bool RpiPrimitiveWindowInput::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out = PrimitiveInputEvent{};
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

#if defined(__linux__)
bool RpiPrimitiveWindowInput::handleLinuxKeyEvent_(uint16_t code,
                                                   int32_t value,
                                                   uint64_t timestampMs) noexcept {
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        shiftHeld_ = (value != 0);
        return false;
    }

    PrimitivePhase phase = PrimitivePhase::Down;
    switch (value) {
        case 0: phase = PrimitivePhase::Up; break;
        case 1: phase = PrimitivePhase::Down; break;
        case 2: phase = PrimitivePhase::Repeat; break;
        default: return false;
    }

    const PrimitiveControl control = mapPrimitiveLinuxKeyCode(code, shiftHeld_);
    if (control == PrimitiveControl::None) {
        return false;
    }

    PrimitiveInputEvent ev{};
    ev.control = control;
    ev.phase = phase;
    ev.timestampMs = timestampMs;

    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
    return true;
}
#endif

} // namespace avantgarde::raspi

