#include "platform/macos/MacPrimitiveInputMapper.h"

namespace avantgarde::macos {
namespace {

UiGestureEvent makeEvent(UiGesture action, int16_t value = 0) noexcept {
    UiGestureEvent ev{};
    ev.action = action;
    ev.value = value;
    return ev;
}

UiGesture mapWindowKeyCode(unsigned short keyCode) noexcept {
    switch (keyCode) {
        case 53: return UiGesture::BackScene;       // Esc
        case 12: return UiGesture::Quit;            // Q
        case 43: return UiGesture::TrackPagePrev;   // ,
        case 47: return UiGesture::TrackPageNext;   // .
        case 46: return UiGesture::ToggleMetronome; // M
        case 38: return UiGesture::ListDown;        // J
        case 40: return UiGesture::ListUp;          // K
        case 36: return UiGesture::ListEnter;       // Enter
        case 4:  return UiGesture::ListParent;      // H
        case 51: return UiGesture::ListParent;      // Backspace
        case 49: return UiGesture::PreviewPlay;     // Space
        case 0:  return UiGesture::PreviewAutoToggle; // A
        case 35: return UiGesture::PlayActiveTrack; // P
        case 1:  return UiGesture::StopActiveTrack; // S
        case 32: return UiGesture::UnmuteActiveTrack; // U
        case 34: return UiGesture::MuteActiveTrack;   // I
        case 17: return UiGesture::MuteToggleActiveTrack; // T
        case 15: return UiGesture::ArmToggleActiveTrack; // R
        case 41: return UiGesture::ActionFocusPrev;  // ;
        case 39: return UiGesture::ActionFocusNext;  // '
        case 44: return UiGesture::ActionAdjustPrev; // /
        case 31: return UiGesture::ActionApply;      // O
        case 16: return UiGesture::ActionUndo;       // Y
        case 24: return UiGesture::TrackSpeedUp;     // =
        case 27: return UiGesture::TrackSpeedDown;   // -
        case 6:  return UiGesture::QuantNone;        // Z
        case 7:  return UiGesture::QuantBeat;        // X
        case 8:  return UiGesture::QuantBar;         // C
        case 30: return UiGesture::BpmUp;            // ]
        case 33: return UiGesture::BpmDown;          // [
        case 122: return UiGesture::F1;
        case 120: return UiGesture::F2;
        case 99: return UiGesture::F3;
        case 118: return UiGesture::F4;
        case 96: return UiGesture::F5;
        case 97: return UiGesture::F6;
        case 98: return UiGesture::F7;
        case 100: return UiGesture::F8;
        case 101: return UiGesture::F9;
        case 109: return UiGesture::F10;
        case 103: return UiGesture::F11;
        case 111: return UiGesture::F12;
        default:
            return UiGesture::None;
    }
}

UiGestureEvent mapDigitSelect(unsigned short keyCode, NSEventModifierFlags mods) noexcept {
    const bool shift = (mods & NSEventModifierFlagShift) != 0;
    switch (keyCode) {
        case 18: return shift ? makeEvent(UiGesture::SelectPatternDirect, 1) : makeEvent(UiGesture::SelectTrackDirect, 1); // 1 / !
        case 19: return shift ? makeEvent(UiGesture::SelectPatternDirect, 2) : makeEvent(UiGesture::SelectTrackDirect, 2); // 2 / @
        case 20: return shift ? makeEvent(UiGesture::SelectPatternDirect, 3) : makeEvent(UiGesture::SelectTrackDirect, 3); // 3 / #
        case 21: return shift ? makeEvent(UiGesture::SelectPatternDirect, 4) : makeEvent(UiGesture::SelectTrackDirect, 4); // 4 / $
        default:
            return makeEvent(UiGesture::None);
    }
}

UiGestureEvent mapWindowChars(NSString* chars) noexcept {
    if (!chars || [chars length] == 0) {
        return makeEvent(UiGesture::None);
    }
    const unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 27: return makeEvent(UiGesture::BackScene);
        case '\r':
        case '\n':
            return makeEvent(UiGesture::ListEnter);
        case 8:
        case 127:
            return makeEvent(UiGesture::ListParent);
        case ' ':
            return makeEvent(UiGesture::PreviewPlay);
        case 'u':
        case 'U':
            return makeEvent(UiGesture::UnmuteActiveTrack);
        case 'i':
        case 'I':
            return makeEvent(UiGesture::MuteActiveTrack);
        case 't':
        case 'T':
            return makeEvent(UiGesture::MuteToggleActiveTrack);
        case 'r':
        case 'R':
            return makeEvent(UiGesture::ArmToggleActiveTrack);
        case ';':
            return makeEvent(UiGesture::ActionFocusPrev);
        case '\'':
            return makeEvent(UiGesture::ActionFocusNext);
        case '/':
            return makeEvent(UiGesture::ActionAdjustPrev);
        case '?':
            return makeEvent(UiGesture::ActionAdjustNext);
        case 'o':
        case 'O':
            return makeEvent(UiGesture::ActionApply);
        case 'y':
        case 'Y':
            return makeEvent(UiGesture::ActionUndo);
        case ',':
            return makeEvent(UiGesture::TrackPagePrev);
        case '.':
            return makeEvent(UiGesture::TrackPageNext);
        case '1':
            return makeEvent(UiGesture::SelectTrackDirect, 1);
        case '2':
            return makeEvent(UiGesture::SelectTrackDirect, 2);
        case '3':
            return makeEvent(UiGesture::SelectTrackDirect, 3);
        case '4':
            return makeEvent(UiGesture::SelectTrackDirect, 4);
        case '!':
            return makeEvent(UiGesture::SelectPatternDirect, 1);
        case '@':
            return makeEvent(UiGesture::SelectPatternDirect, 2);
        case '#':
            return makeEvent(UiGesture::SelectPatternDirect, 3);
        case '$':
            return makeEvent(UiGesture::SelectPatternDirect, 4);
        default:
            return makeEvent(UiGesture::None);
    }
}

} // namespace

UiGestureEvent mapPrimitiveWindowEvent(NSEvent* event) noexcept {
    if (!event || [event type] != NSEventTypeKeyDown) {
        return makeEvent(UiGesture::None);
    }
    // Особый кейс для '/' vs '?' (Shift+/).
    if ([event keyCode] == 44) {
        const NSEventModifierFlags mods =
            ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
        return (mods & NSEventModifierFlagShift)
                   ? makeEvent(UiGesture::ActionAdjustNext)
                   : makeEvent(UiGesture::ActionAdjustPrev);
    }
    // Отдельная ветка для 1..4 vs Shift+1..4 (track/pattern select).
    if ([event keyCode] >= 18 && [event keyCode] <= 21) {
        const NSEventModifierFlags mods =
            ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
        const UiGestureEvent ev = mapDigitSelect([event keyCode], mods);
        if (ev.action != UiGesture::None) {
            return ev;
        }
    }
    const UiGesture byKeyCode = mapWindowKeyCode([event keyCode]);
    if (byKeyCode != UiGesture::None) {
        return makeEvent(byKeyCode);
    }
    return mapWindowChars([event charactersIgnoringModifiers]);
}

} // namespace avantgarde::macos
