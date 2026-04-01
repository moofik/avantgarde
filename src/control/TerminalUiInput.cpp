#include "control/TerminalUiInput.h"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <new>
#include <string>
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

UiGesture TerminalUiInput::mapKey(char ch) noexcept {
    switch (ch) {
        case 27:  // ESC
            return UiGesture::BackScene;
        case 'q':
        case 'Q':
            return UiGesture::Quit;
        case '1':
            return UiGesture::SelectPrevTrack;
        case '2':
            return UiGesture::SelectNextTrack;
        case ',':
            return UiGesture::TrackPagePrev;
        case '.':
            return UiGesture::TrackPageNext;
        case 'm':
        case 'M':
            return UiGesture::OpenManager;
        case 'j':
        case 'J':
            return UiGesture::ListDown;
        case 'k':
        case 'K':
            return UiGesture::ListUp;
        case '\r':
        case '\n':
            return UiGesture::ListEnter;
        case 8:    // ctrl+h / backspace on some terminals
        case 127:  // backspace
        case 'h':
        case 'H':
            return UiGesture::ListParent;
        case ' ':
            return UiGesture::PreviewPlay;
        case 'a':
        case 'A':
            return UiGesture::PreviewAutoToggle;
        case 'p':
        case 'P':
            return UiGesture::PlayActiveTrack;
        case 's':
        case 'S':
            return UiGesture::StopActiveTrack;
        case 'u':
        case 'U':
            return UiGesture::UnmuteActiveTrack;
        case 'i':
        case 'I':
            return UiGesture::MuteActiveTrack;
        case 't':
        case 'T':
            return UiGesture::MuteToggleActiveTrack;
        case 'r':
        case 'R':
            return UiGesture::ArmToggleActiveTrack;
        case ';':
            return UiGesture::ActionFocusPrev;
        case '\'':
            return UiGesture::ActionFocusNext;
        case '/':
            return UiGesture::ActionAdjustPrev;
        case '?':
            return UiGesture::ActionAdjustNext;
        case 'o':
        case 'O':
            return UiGesture::ActionApply;
        case 'y':
        case 'Y':
            return UiGesture::ActionUndo;
        case '+':
        case '=':
            return UiGesture::TrackSpeedUp;
        case '-':
        case '_':
            return UiGesture::TrackSpeedDown;
        case 'z':
        case 'Z':
            return UiGesture::QuantNone;
        case 'x':
        case 'X':
            return UiGesture::QuantBeat;
        case 'c':
        case 'C':
            return UiGesture::QuantBar;
        case ']':
            return UiGesture::BpmUp;
        case '[':
            return UiGesture::BpmDown;
        default:
            return UiGesture::None;
    }
}

UiGesture TerminalUiInput::mapEscapeSequence(std::string_view seq) noexcept {
    // Arrow keys: ESC [ A/B/C/D
    if (seq == "[A") return UiGesture::ActionAdjustNext; // Up
    if (seq == "[B") return UiGesture::ActionAdjustPrev; // Down
    if (seq == "[C") return UiGesture::ActionFocusNext;  // Right
    if (seq == "[D") return UiGesture::ActionFocusPrev;  // Left

    // xterm function keys:
    // F1..F4: ESC O P/Q/R/S
    if (seq == "OP") return UiGesture::F1;
    if (seq == "OQ") return UiGesture::F2;
    if (seq == "OR") return UiGesture::F3;
    if (seq == "OS") return UiGesture::F4;
    // F5..F12: ESC [15~ [17~ [18~ [19~ [20~ [21~ [23~ [24~
    if (seq == "[15~") return UiGesture::F5;
    if (seq == "[17~") return UiGesture::F6;
    if (seq == "[18~") return UiGesture::F7;
    if (seq == "[19~") return UiGesture::F8;
    if (seq == "[20~") return UiGesture::F9;
    if (seq == "[21~") return UiGesture::F10;
    if (seq == "[23~") return UiGesture::F11;
    if (seq == "[24~") return UiGesture::F12;

    // Linux console alternative for F1..F4:
    // ESC [[A .. ESC [[D
    if (seq == "[[A") return UiGesture::F1;
    if (seq == "[[B") return UiGesture::F2;
    if (seq == "[[C") return UiGesture::F3;
    if (seq == "[[D") return UiGesture::F4;

    // Linux console alternative for F1..F12:
    // ESC [11~ .. ESC [24~
    if (seq == "[11~") return UiGesture::F1;
    if (seq == "[12~") return UiGesture::F2;
    if (seq == "[13~") return UiGesture::F3;
    if (seq == "[14~") return UiGesture::F4;
    if (seq == "[15~") return UiGesture::F5;
    if (seq == "[17~") return UiGesture::F6;
    if (seq == "[18~") return UiGesture::F7;
    if (seq == "[19~") return UiGesture::F8;
    if (seq == "[20~") return UiGesture::F9;
    if (seq == "[21~") return UiGesture::F10;
    if (seq == "[23~") return UiGesture::F11;
    if (seq == "[24~") return UiGesture::F12;

    return UiGesture::None;
}

bool TerminalUiInput::poll(UiGestureEvent& out) noexcept {
    out.action = UiGesture::None;
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

    if (ch == 27) {
        // ESC может быть как одиночной клавишей, так и началом escape-последовательности.
        // Читаем "хвост" без блокировки и пытаемся декодировать функциональные клавиши.
        std::string seq;
        seq.reserve(8);
        for (int i = 0; i < 7; ++i) {
            char b = 0;
            const ssize_t rn = read(STDIN_FILENO, &b, 1);
            if (rn <= 0) {
                break;
            }
            seq.push_back(b);
            if ((b >= 'A' && b <= 'Z') || b == '~') {
                break;
            }
        }
        if (!seq.empty()) {
            const UiGesture escAction = mapEscapeSequence(seq);
            if (escAction != UiGesture::None) {
                out.action = escAction;
                return true;
            }
        }
        out.action = UiGesture::BackScene;
        return true;
    }

    const UiGesture action = mapKey(ch);
    if (action == UiGesture::None) {
        return false;
    }

    out.action = action;
    return true;
}

} // namespace avantgarde
