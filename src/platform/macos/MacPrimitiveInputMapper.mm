#include "platform/macos/MacPrimitiveInputMapper.h"

namespace avantgarde::macos {
namespace {

UiGesture mapWindowKeyCode(unsigned short keyCode) noexcept {
    switch (keyCode) {
        case 53: return UiGesture::BackScene;       // Esc
        case 12: return UiGesture::Quit;            // Q
        case 18: return UiGesture::SelectPrevTrack; // 1
        case 19: return UiGesture::SelectNextTrack; // 2
        case 43: return UiGesture::TrackPagePrev;   // ,
        case 47: return UiGesture::TrackPageNext;   // .
        case 46: return UiGesture::OpenManager;     // M
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

UiGesture mapWindowChars(NSString* chars) noexcept {
    if (!chars || [chars length] == 0) {
        return UiGesture::None;
    }
    const unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 27: return UiGesture::BackScene;
        case '\r':
        case '\n':
            return UiGesture::ListEnter;
        case 8:
        case 127:
            return UiGesture::ListParent;
        case ' ':
            return UiGesture::PreviewPlay;
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
        case ',':
            return UiGesture::TrackPagePrev;
        case '.':
            return UiGesture::TrackPageNext;
        default:
            return UiGesture::None;
    }
}

} // namespace

UiGesture mapPrimitiveWindowEvent(NSEvent* event) noexcept {
    if (!event || [event type] != NSEventTypeKeyDown) {
        return UiGesture::None;
    }
    // Особый кейс для '/' vs '?' (Shift+/).
    if ([event keyCode] == 44) {
        const NSEventModifierFlags mods =
            ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
        return (mods & NSEventModifierFlagShift)
                   ? UiGesture::ActionAdjustNext
                   : UiGesture::ActionAdjustPrev;
    }
    const UiGesture byKeyCode = mapWindowKeyCode([event keyCode]);
    if (byKeyCode != UiGesture::None) {
        return byKeyCode;
    }
    return mapWindowChars([event charactersIgnoringModifiers]);
}

} // namespace avantgarde::macos

