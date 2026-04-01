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
        case 'u':
        case 'U':
            return UiInputAction::UnmuteActiveTrack;
        case 'i':
        case 'I':
            return UiInputAction::MuteActiveTrack;
        case 't':
        case 'T':
            return UiInputAction::MuteToggleActiveTrack;
        case 'r':
        case 'R':
            return UiInputAction::ArmToggleActiveTrack;
        case ';':
            return UiInputAction::ActionFocusPrev;
        case '\'':
            return UiInputAction::ActionFocusNext;
        case '/':
            return UiInputAction::ActionAdjustPrev;
        case '?':
            return UiInputAction::ActionAdjustNext;
        case 'o':
        case 'O':
            return UiInputAction::ActionApply;
        case 'y':
        case 'Y':
            return UiInputAction::ActionUndo;
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

UiInputAction TerminalUiInput::mapEscapeSequence(std::string_view seq) noexcept {
    // Arrow keys: ESC [ A/B/C/D
    if (seq == "[A") return UiInputAction::ActionAdjustNext; // Up
    if (seq == "[B") return UiInputAction::ActionAdjustPrev; // Down
    if (seq == "[C") return UiInputAction::ActionFocusNext;  // Right
    if (seq == "[D") return UiInputAction::ActionFocusPrev;  // Left

    // xterm function keys:
    // F1..F4: ESC O P/Q/R/S
    if (seq == "OP") return UiInputAction::F1;
    if (seq == "OQ") return UiInputAction::F2;
    if (seq == "OR") return UiInputAction::F3;
    if (seq == "OS") return UiInputAction::F4;
    // F5..F12: ESC [15~ [17~ [18~ [19~ [20~ [21~ [23~ [24~
    if (seq == "[15~") return UiInputAction::F5;
    if (seq == "[17~") return UiInputAction::F6;
    if (seq == "[18~") return UiInputAction::F7;
    if (seq == "[19~") return UiInputAction::F8;
    if (seq == "[20~") return UiInputAction::F9;
    if (seq == "[21~") return UiInputAction::F10;
    if (seq == "[23~") return UiInputAction::F11;
    if (seq == "[24~") return UiInputAction::F12;

    // Linux console alternative for F1..F4:
    // ESC [[A .. ESC [[D
    if (seq == "[[A") return UiInputAction::F1;
    if (seq == "[[B") return UiInputAction::F2;
    if (seq == "[[C") return UiInputAction::F3;
    if (seq == "[[D") return UiInputAction::F4;

    // Linux console alternative for F1..F12:
    // ESC [11~ .. ESC [24~
    if (seq == "[11~") return UiInputAction::F1;
    if (seq == "[12~") return UiInputAction::F2;
    if (seq == "[13~") return UiInputAction::F3;
    if (seq == "[14~") return UiInputAction::F4;
    if (seq == "[15~") return UiInputAction::F5;
    if (seq == "[17~") return UiInputAction::F6;
    if (seq == "[18~") return UiInputAction::F7;
    if (seq == "[19~") return UiInputAction::F8;
    if (seq == "[20~") return UiInputAction::F9;
    if (seq == "[21~") return UiInputAction::F10;
    if (seq == "[23~") return UiInputAction::F11;
    if (seq == "[24~") return UiInputAction::F12;

    return UiInputAction::None;
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
            const UiInputAction escAction = mapEscapeSequence(seq);
            if (escAction != UiInputAction::None) {
                out.action = escAction;
                return true;
            }
        }
        out.action = UiInputAction::BackScene;
        return true;
    }

    const UiInputAction action = mapKey(ch);
    if (action == UiInputAction::None) {
        return false;
    }

    out.action = action;
    return true;
}

} // namespace avantgarde
