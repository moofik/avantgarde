#include "control/TerminalUiInput.h"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <new>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace avantgarde {

struct TerminalUiInput::termios_storage {
    termios t{};
};

TerminalUiInput::TerminalUiInput() {
    oldTerm_ = new (std::nothrow) termios_storage();
    if (!oldTerm_) {
        return;
    }

    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &oldTerm_->t) != 0) {
        return;
    }
    hasOldTermios_ = true;

    termios raw = oldTerm_->t;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return;
    }

    oldFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (oldFlags_ >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, oldFlags_ | O_NONBLOCK);
    }

    valid_ = true;
}

TerminalUiInput::~TerminalUiInput() {
    if (hasOldTermios_) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm_->t);
    }
    if (oldFlags_ >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, oldFlags_);
    }
    delete oldTerm_;
}

UiInputAction TerminalUiInput::mapKey(char ch) noexcept {
    switch (ch) {
        case 27:  // ESC
            return UiInputAction::BackScene;
        case 'q':
        case 'Q':
            return UiInputAction::Quit;
        case '1':
            return UiInputAction::SelectPrevTrack;
        case '2':
            return UiInputAction::SelectNextTrack;
        case ',':
            return UiInputAction::TrackPagePrev;
        case '.':
            return UiInputAction::TrackPageNext;
        case 'm':
        case 'M':
            return UiInputAction::OpenManager;
        case 'j':
        case 'J':
            return UiInputAction::ListDown;
        case 'k':
        case 'K':
            return UiInputAction::ListUp;
        case '\r':
        case '\n':
            return UiInputAction::ListEnter;
        case 8:    // ctrl+h / backspace on some terminals
        case 127:  // backspace
        case 'h':
        case 'H':
            return UiInputAction::ListParent;
        case ' ':
            return UiInputAction::PreviewPlay;
        case 'a':
        case 'A':
            return UiInputAction::PreviewAutoToggle;
        case 'p':
        case 'P':
            return UiInputAction::PlayActiveTrack;
        case 's':
        case 'S':
            return UiInputAction::StopActiveTrack;
        case '+':
        case '=':
            return UiInputAction::TrackSpeedUp;
        case '-':
        case '_':
            return UiInputAction::TrackSpeedDown;
        case 'z':
        case 'Z':
            return UiInputAction::QuantNone;
        case 'x':
        case 'X':
            return UiInputAction::QuantBeat;
        case 'c':
        case 'C':
            return UiInputAction::QuantBar;
        case ']':
            return UiInputAction::BpmUp;
        case '[':
            return UiInputAction::BpmDown;
        default:
            return UiInputAction::None;
    }
}

bool TerminalUiInput::poll(UiInputEvent& out) noexcept {
    out.action = UiInputAction::None;
    if (!valid_) {
        return false;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(STDIN_FILENO, &readSet);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    const int ready = select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &tv);
    if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &readSet)) {
        return false;
    }

    char ch = 0;
    const ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
        return false;
    }

    const UiInputAction action = mapKey(ch);
    if (action == UiInputAction::None) {
        return false;
    }

    out.action = action;
    return true;
}

} // namespace avantgarde
