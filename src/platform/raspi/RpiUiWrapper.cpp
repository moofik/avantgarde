#include "platform/raspi/RpiUiWrapper.h"

#include "app/AppDiagnostics.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace avantgarde::raspi {

RpiUiRenderer::RpiUiRenderer(RpiUiWrapperConfig config) noexcept
    : config_(config) {}

void RpiUiRenderer::render(const UiState& /*state*/) {
    if (!warned_) {
        AppDiagnostics::logf(AppLogLevel::Warn,
                             "RpiUiRenderer: stub backend active (no real framebuffer output yet)");
        warned_ = true;
    }
}

void RpiUiRenderer::renderPreparedLayout(const UiPreparedLayout& /*prepared*/) noexcept {
    // Пока в RPi-режиме просто удерживаем контракт. Реальный renderer (DRM/SDL/fbdev)
    // будет применен позже без изменения app-слоя.
}

RpiPrimitiveInput::~RpiPrimitiveInput() {
    shutdown();
}

bool RpiPrimitiveInput::init(const RpiUiWrapperConfig& config, std::string& errorOut) {
    errorOut.clear();
#if defined(__linux__)
    shutdown();
    shiftHeld_ = false;

    if (config.inputDevice.empty()) {
        AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveInput: input disabled (empty device path)");
        return true;
    }

    const int fd = ::open(config.inputDevice.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        AppDiagnostics::logf(AppLogLevel::Warn,
                             "RpiPrimitiveInput: cannot open input device '%s': %s",
                             config.inputDevice.c_str(),
                             std::strerror(errno));
        // Не фейлим init всего приложения: wrapper может работать в headless-режиме.
        return true;
    }
    inputFd_ = fd;
    AppDiagnostics::logf(AppLogLevel::Info,
                         "RpiPrimitiveInput: attached to '%s'",
                         config.inputDevice.c_str());
    return true;
#else
    (void)config;
    errorOut.clear();
    AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveInput: non-linux platform, using stub input backend");
    return true;
#endif
}

void RpiPrimitiveInput::shutdown() noexcept {
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

bool RpiPrimitiveInput::pollPlatformEvents() noexcept {
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
                                  static_cast<uint64_t>(ev.time.tv_usec / 1000);
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
                                 "RpiPrimitiveInput: read error: %s",
                                 std::strerror(errno));
        }
        break;
    }
    return anyEvents;
#endif
    // Stub для non-linux.
    return false;
}

bool RpiPrimitiveInput::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out = PrimitiveInputEvent{};
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void RpiPrimitiveInput::pushSynthetic(const PrimitiveInputEvent& ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
}

#if defined(__linux__)
bool RpiPrimitiveInput::handleLinuxKeyEvent_(uint16_t code, int32_t value, uint64_t timestampMs) noexcept {
    // Трекаем Shift локально, чтобы поддерживать комбинации:
    // Shift+1..4 -> SelectPattern1..4, Shift+/ -> ActionAdjustNext, Shift+N -> OpenPatternEdit.
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

    PrimitiveControl control = PrimitiveControl::None;
    switch (code) {
        case KEY_1: control = shiftHeld_ ? PrimitiveControl::SelectPattern1 : PrimitiveControl::SelectTrack1; break;
        case KEY_2: control = shiftHeld_ ? PrimitiveControl::SelectPattern2 : PrimitiveControl::SelectTrack2; break;
        case KEY_3: control = shiftHeld_ ? PrimitiveControl::SelectPattern3 : PrimitiveControl::SelectTrack3; break;
        case KEY_4: control = shiftHeld_ ? PrimitiveControl::SelectPattern4 : PrimitiveControl::SelectTrack4; break;
        case KEY_SLASH:
            control = shiftHeld_ ? PrimitiveControl::ActionAdjustNext : PrimitiveControl::ActionAdjustPrev;
            break;
        case KEY_N:
            control = shiftHeld_ ? PrimitiveControl::OpenPatternEdit : PrimitiveControl::OpenSequencer;
            break;
        default:
            control = mapLinuxKeyCode_(code);
            break;
    }
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

PrimitiveControl RpiPrimitiveInput::mapLinuxKeyCode_(uint16_t code) const noexcept {
    switch (code) {
        case KEY_ESC: return PrimitiveControl::BackScene;
        case KEY_Q: return PrimitiveControl::Quit;
        case KEY_COMMA: return PrimitiveControl::TrackPagePrev;
        case KEY_DOT: return PrimitiveControl::TrackPageNext;
        case KEY_M: return PrimitiveControl::ToggleMetronome;
        case KEY_V: return PrimitiveControl::Record;
        case KEY_J: return PrimitiveControl::ListDown;
        case KEY_K: return PrimitiveControl::ListUp;
        case KEY_ENTER: return PrimitiveControl::ListEnter;
        case KEY_H: return PrimitiveControl::ListParent;
        case KEY_BACKSPACE: return PrimitiveControl::DeleteObject;
        case KEY_SPACE: return PrimitiveControl::PreviewPlay;
        case KEY_A: return PrimitiveControl::PreviewAutoToggle;
        case KEY_P: return PrimitiveControl::PlayActiveTrack;
        case KEY_S: return PrimitiveControl::StopActiveTrack;
        case KEY_D: return PrimitiveControl::DeleteObject;
        case KEY_U: return PrimitiveControl::UnmuteActiveTrack;
        case KEY_I: return PrimitiveControl::MuteActiveTrack;
        case KEY_E: return PrimitiveControl::Snapshot1;
        case KEY_R: return PrimitiveControl::Snapshot2;
        case KEY_T: return PrimitiveControl::Snapshot3;
        case KEY_Y: return PrimitiveControl::Snapshot4;
        case KEY_SEMICOLON: return PrimitiveControl::ActionFocusPrev;
        case KEY_APOSTROPHE: return PrimitiveControl::ActionFocusNext;
        case KEY_O: return PrimitiveControl::ActionApply;
        case KEY_EQUAL: return PrimitiveControl::TrackSpeedUp;
        case KEY_MINUS: return PrimitiveControl::TrackSpeedDown;
        case KEY_Z: return PrimitiveControl::QuantNone;
        case KEY_X: return PrimitiveControl::QuantBeat;
        case KEY_C: return PrimitiveControl::QuantBar;
        case KEY_RIGHTBRACE: return PrimitiveControl::BpmUp;
        case KEY_LEFTBRACE: return PrimitiveControl::BpmDown;
        case KEY_F1: return PrimitiveControl::F1;
        case KEY_F2: return PrimitiveControl::F2;
        case KEY_F3: return PrimitiveControl::F3;
        case KEY_F4: return PrimitiveControl::F4;
        case KEY_F5: return PrimitiveControl::F5;
        case KEY_F6: return PrimitiveControl::F6;
        case KEY_F7: return PrimitiveControl::F7;
        case KEY_F8: return PrimitiveControl::F8;
        case KEY_F9: return PrimitiveControl::F9;
        case KEY_F10: return PrimitiveControl::F10;
        case KEY_F11: return PrimitiveControl::F11;
        case KEY_F12: return PrimitiveControl::F12;
        default:
            return PrimitiveControl::None;
    }
}
#endif

bool RpiUiWrapper::init(const RpiUiWrapperConfig& config, std::string& errorOut) {
    config_ = config;
    renderer_ = RpiUiRenderer{config_};
    return input_.init(config_, errorOut);
}

bool RpiUiWrapper::pollEvents() noexcept {
    return input_.pollPlatformEvents();
}

bool RpiUiWrapper::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    return input_.readNextInputEvent(out);
}

void RpiUiWrapper::render(const UiState& state, const UiPreparedLayout* preparedLayout) {
    if (preparedLayout) {
        renderer_.renderPreparedLayout(*preparedLayout);
        return;
    }
    renderer_.render(state);
}

} // namespace avantgarde::raspi
