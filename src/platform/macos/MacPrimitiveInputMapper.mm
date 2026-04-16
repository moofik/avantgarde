#include "platform/macos/MacPrimitiveInputMapper.h"

#include <cmath>
#include <cstdint>

namespace avantgarde::macos {
namespace {

uint64_t eventTimeMs(NSEvent* event) noexcept {
    if (!event) {
        return 0;
    }
    const double ts = [event timestamp];
    if (!std::isfinite(ts) || ts <= 0.0) {
        return 0;
    }
    return static_cast<uint64_t>(std::llround(ts * 1000.0));
}

PrimitiveControl fromKeyCode(unsigned short keyCode) noexcept {
    switch (keyCode) {
        case 53: return PrimitiveControl::BackScene;       // Esc
        case 12: return PrimitiveControl::Quit;            // Q
        case 43: return PrimitiveControl::TrackPagePrev;   // ,
        case 47: return PrimitiveControl::TrackPageNext;   // .
        case 45: return PrimitiveControl::OpenSequencer;   // N
        case 46: return PrimitiveControl::ToggleMetronome; // M
        case 9:  return PrimitiveControl::Record;          // V
        case 38: return PrimitiveControl::ListDown;        // J
        case 40: return PrimitiveControl::ListUp;          // K
        case 36: return PrimitiveControl::ListEnter;       // Enter
        case 4:  return PrimitiveControl::ListParent;      // H
        case 51: return PrimitiveControl::DeleteObject;    // Backspace/Delete
        case 49: return PrimitiveControl::PreviewPlay;     // Space
        case 0:  return PrimitiveControl::PreviewAutoToggle; // A
        case 35: return PrimitiveControl::PlayActiveTrack; // P
        case 1:  return PrimitiveControl::StopActiveTrack; // S
        case 2:  return PrimitiveControl::DeleteObject;    // D
        case 32: return PrimitiveControl::UnmuteActiveTrack; // U
        case 34: return PrimitiveControl::MuteActiveTrack;   // I
        case 41: return PrimitiveControl::ActionFocusPrev;   // ;
        case 39: return PrimitiveControl::ActionFocusNext;   // '
        case 44: return PrimitiveControl::ActionAdjustPrev;  // /
        case 31: return PrimitiveControl::ActionApply;       // O
        case 24: return PrimitiveControl::TrackSpeedUp;      // =
        case 27: return PrimitiveControl::TrackSpeedDown;    // -
        case 6:  return PrimitiveControl::QuantNone;         // Z
        case 7:  return PrimitiveControl::QuantBeat;         // X
        case 8:  return PrimitiveControl::QuantBar;          // C
        case 30: return PrimitiveControl::BpmUp;             // ]
        case 33: return PrimitiveControl::BpmDown;           // [
        case 122: return PrimitiveControl::F1;
        case 120: return PrimitiveControl::F2;
        case 99: return PrimitiveControl::F3;
        case 118: return PrimitiveControl::F4;
        case 96: return PrimitiveControl::F5;
        case 97: return PrimitiveControl::F6;
        case 98: return PrimitiveControl::F7;
        case 100: return PrimitiveControl::F8;
        case 101: return PrimitiveControl::F9;
        case 109: return PrimitiveControl::F10;
        case 103: return PrimitiveControl::F11;
        case 111: return PrimitiveControl::F12;
        default:
            return PrimitiveControl::None;
    }
}

PrimitiveControl mapDigitSelect(unsigned short keyCode, NSEventModifierFlags mods) noexcept {
    const bool shift = (mods & NSEventModifierFlagShift) != 0;
    switch (keyCode) {
        case 18: return shift ? PrimitiveControl::SelectPattern1 : PrimitiveControl::SelectTrack1; // 1/!
        case 19: return shift ? PrimitiveControl::SelectPattern2 : PrimitiveControl::SelectTrack2; // 2/@
        case 20: return shift ? PrimitiveControl::SelectPattern3 : PrimitiveControl::SelectTrack3; // 3/#
        case 21: return shift ? PrimitiveControl::SelectPattern4 : PrimitiveControl::SelectTrack4; // 4/$
        default:
            return PrimitiveControl::None;
    }
}

PrimitiveControl mapChars(NSString* chars) noexcept {
    if (!chars || [chars length] == 0) {
        return PrimitiveControl::None;
    }
    const unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 27: return PrimitiveControl::BackScene;
        case '\r':
        case '\n':
            return PrimitiveControl::ListEnter;
        case 8:
        case 127:
            return PrimitiveControl::ListParent;
        case ' ':
            return PrimitiveControl::PreviewPlay;
        case 'u':
        case 'U':
            return PrimitiveControl::UnmuteActiveTrack;
        case 'i':
        case 'I':
            return PrimitiveControl::MuteActiveTrack;
        case 'e':
        case 'E':
            return PrimitiveControl::Snapshot1;
        case 'r':
        case 'R':
            return PrimitiveControl::Snapshot2;
        case 't':
        case 'T':
            return PrimitiveControl::Snapshot3;
        case 'y':
        case 'Y':
            return PrimitiveControl::Snapshot4;
        case ';':
            return PrimitiveControl::ActionFocusPrev;
        case '\'':
            return PrimitiveControl::ActionFocusNext;
        case '/':
            return PrimitiveControl::ActionAdjustPrev;
        case '?':
            return PrimitiveControl::ActionAdjustNext;
        case 'o':
        case 'O':
            return PrimitiveControl::ActionApply;
        case 'v':
        case 'V':
            return PrimitiveControl::Record;
        case 'd':
        case 'D':
            return PrimitiveControl::DeleteObject;
        case ',':
            return PrimitiveControl::TrackPagePrev;
        case '.':
            return PrimitiveControl::TrackPageNext;
        case 'n':
        case 'N':
            return PrimitiveControl::OpenSequencer;
        case '1':
            return PrimitiveControl::SelectTrack1;
        case '2':
            return PrimitiveControl::SelectTrack2;
        case '3':
            return PrimitiveControl::SelectTrack3;
        case '4':
            return PrimitiveControl::SelectTrack4;
        case '!':
            return PrimitiveControl::SelectPattern1;
        case '@':
            return PrimitiveControl::SelectPattern2;
        case '#':
            return PrimitiveControl::SelectPattern3;
        case '$':
            return PrimitiveControl::SelectPattern4;
        default:
            return PrimitiveControl::None;
    }
}

PrimitiveControl resolveControl(NSEvent* event) noexcept {
    if (!event) {
        return PrimitiveControl::None;
    }
    const unsigned short keyCode = static_cast<unsigned short>([event keyCode]);
    const NSEventModifierFlags mods = ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);

    // Shift+/ -> '?'.
    if (keyCode == 44) {
        return ((mods & NSEventModifierFlagShift) != 0)
                   ? PrimitiveControl::ActionAdjustNext
                   : PrimitiveControl::ActionAdjustPrev;
    }

    // Shift+N -> быстрый вход в pattern edit.
    if (keyCode == 45 && (mods & NSEventModifierFlagShift) != 0) {
        return PrimitiveControl::OpenPatternEdit;
    }

    // 1..4 vs Shift+1..4.
    if (keyCode >= 18 && keyCode <= 21) {
        const PrimitiveControl d = mapDigitSelect(keyCode, mods);
        if (d != PrimitiveControl::None) {
            return d;
        }
    }

    // Snapshot slots E/R/T/Y по keyCode.
    switch (keyCode) {
        case 14: return PrimitiveControl::Snapshot1; // E
        case 15: return PrimitiveControl::Snapshot2; // R
        case 17: return PrimitiveControl::Snapshot3; // T
        case 16: return PrimitiveControl::Snapshot4; // Y
        default: break;
    }

    const PrimitiveControl byKeyCode = fromKeyCode(keyCode);
    if (byKeyCode != PrimitiveControl::None) {
        return byKeyCode;
    }
    return mapChars([event charactersIgnoringModifiers]);
}

} // namespace

PrimitiveInputEvent mapPrimitiveWindowEvent(NSEvent* event) noexcept {
    PrimitiveInputEvent out{};
    out.timestampMs = eventTimeMs(event);
    if (!event) {
        return out;
    }
    const NSEventType type = [event type];
    if (type != NSEventTypeKeyDown && type != NSEventTypeKeyUp) {
        return out;
    }
    out.control = resolveControl(event);
    if (out.control == PrimitiveControl::None) {
        return out;
    }
    if (type == NSEventTypeKeyUp) {
        out.phase = PrimitivePhase::Up;
        return out;
    }
    out.phase = ([event isARepeat] == YES) ? PrimitivePhase::Repeat : PrimitivePhase::Down;
    return out;
}

} // namespace avantgarde::macos
