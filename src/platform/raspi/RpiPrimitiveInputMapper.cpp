#include "platform/raspi/RpiPrimitiveInputMapper.h"

#if defined(__linux__)
#include <linux/input.h>
#endif

namespace avantgarde::raspi {

PrimitiveControl mapPrimitiveLinuxKeyCode(uint16_t code, bool shiftHeld) noexcept {
#if defined(__linux__)
    switch (code) {
        case KEY_1: return shiftHeld ? PrimitiveControl::SelectPattern1 : PrimitiveControl::SelectTrack1;
        case KEY_2: return shiftHeld ? PrimitiveControl::SelectPattern2 : PrimitiveControl::SelectTrack2;
        case KEY_3: return shiftHeld ? PrimitiveControl::SelectPattern3 : PrimitiveControl::SelectTrack3;
        case KEY_4: return shiftHeld ? PrimitiveControl::SelectPattern4 : PrimitiveControl::SelectTrack4;
        case KEY_SLASH:
            return shiftHeld ? PrimitiveControl::ActionAdjustNext : PrimitiveControl::ActionAdjustPrev;
        case KEY_N:
            return shiftHeld ? PrimitiveControl::OpenPatternEdit : PrimitiveControl::OpenSequencer;
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
#else
    (void)code;
    (void)shiftHeld;
    return PrimitiveControl::None;
#endif
}

} // namespace avantgarde::raspi

