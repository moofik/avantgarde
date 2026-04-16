#include "service/ui/input/PressPolicyResolver.h"

namespace avantgarde {

bool PressPolicyResolver::mapControlToTap_(PrimitiveControl control,
                                           UiGesture& action,
                                           int16_t& value) noexcept {
    value = 0;
    switch (control) {
        case PrimitiveControl::Quit: action = UiGesture::Quit; return true;
        case PrimitiveControl::SelectTrack1: action = UiGesture::SelectTrackDirect; value = 1; return true;
        case PrimitiveControl::SelectTrack2: action = UiGesture::SelectTrackDirect; value = 2; return true;
        case PrimitiveControl::SelectTrack3: action = UiGesture::SelectTrackDirect; value = 3; return true;
        case PrimitiveControl::SelectTrack4: action = UiGesture::SelectTrackDirect; value = 4; return true;
        case PrimitiveControl::SelectPattern1: action = UiGesture::SelectPatternDirect; value = 1; return true;
        case PrimitiveControl::SelectPattern2: action = UiGesture::SelectPatternDirect; value = 2; return true;
        case PrimitiveControl::SelectPattern3: action = UiGesture::SelectPatternDirect; value = 3; return true;
        case PrimitiveControl::SelectPattern4: action = UiGesture::SelectPatternDirect; value = 4; return true;
        case PrimitiveControl::SelectPrevTrack: action = UiGesture::SelectPrevTrack; return true;
        case PrimitiveControl::SelectNextTrack: action = UiGesture::SelectNextTrack; return true;
        case PrimitiveControl::TrackPagePrev: action = UiGesture::TrackPagePrev; return true;
        case PrimitiveControl::TrackPageNext: action = UiGesture::TrackPageNext; return true;
        case PrimitiveControl::OpenSequencer: action = UiGesture::OpenSequencer; return true;
        case PrimitiveControl::OpenPatternEdit: action = UiGesture::OpenPatternEdit; return true;
        case PrimitiveControl::OpenManager: action = UiGesture::OpenManager; return true;
        case PrimitiveControl::BackScene: action = UiGesture::BackScene; return true;
        case PrimitiveControl::ListUp: action = UiGesture::ListUp; return true;
        case PrimitiveControl::ListDown: action = UiGesture::ListDown; return true;
        case PrimitiveControl::ListEnter: action = UiGesture::ListEnter; return true;
        case PrimitiveControl::ListParent: action = UiGesture::ListParent; return true;
        case PrimitiveControl::PreviewPlay: action = UiGesture::PreviewPlay; return true;
        case PrimitiveControl::PreviewAutoToggle: action = UiGesture::PreviewAutoToggle; return true;
        case PrimitiveControl::PlayActiveTrack: action = UiGesture::PlayActiveTrack; return true;
        case PrimitiveControl::StopActiveTrack: action = UiGesture::StopActiveTrack; return true;
        case PrimitiveControl::UnmuteActiveTrack: action = UiGesture::UnmuteActiveTrack; return true;
        case PrimitiveControl::MuteActiveTrack: action = UiGesture::MuteActiveTrack; return true;
        case PrimitiveControl::MuteToggleActiveTrack: action = UiGesture::MuteToggleActiveTrack; return true;
        case PrimitiveControl::ArmToggleActiveTrack: action = UiGesture::ArmToggleActiveTrack; return true;
        case PrimitiveControl::TrackSpeedUp: action = UiGesture::TrackSpeedUp; return true;
        case PrimitiveControl::TrackSpeedDown: action = UiGesture::TrackSpeedDown; return true;
        case PrimitiveControl::QuantNone: action = UiGesture::QuantNone; return true;
        case PrimitiveControl::QuantBeat: action = UiGesture::QuantBeat; return true;
        case PrimitiveControl::QuantBar: action = UiGesture::QuantBar; return true;
        case PrimitiveControl::BpmUp: action = UiGesture::BpmUp; return true;
        case PrimitiveControl::BpmDown: action = UiGesture::BpmDown; return true;
        case PrimitiveControl::ToggleMetronome: action = UiGesture::ToggleMetronome; return true;
        case PrimitiveControl::Record: action = UiGesture::Record; return true;
        case PrimitiveControl::DeleteObject: action = UiGesture::DeleteObject; return true;
        case PrimitiveControl::Snapshot1: action = UiGesture::SnapshotSlotDirect; value = 1; return true;
        case PrimitiveControl::Snapshot2: action = UiGesture::SnapshotSlotDirect; value = 2; return true;
        case PrimitiveControl::Snapshot3: action = UiGesture::SnapshotSlotDirect; value = 3; return true;
        case PrimitiveControl::Snapshot4: action = UiGesture::SnapshotSlotDirect; value = 4; return true;
        case PrimitiveControl::ActionFocusPrev: action = UiGesture::ActionFocusPrev; return true;
        case PrimitiveControl::ActionFocusNext: action = UiGesture::ActionFocusNext; return true;
        case PrimitiveControl::ActionAdjustPrev: action = UiGesture::ActionAdjustPrev; return true;
        case PrimitiveControl::ActionAdjustNext: action = UiGesture::ActionAdjustNext; return true;
        case PrimitiveControl::ActionApply: action = UiGesture::ActionApply; return true;
        case PrimitiveControl::ActionUndo: action = UiGesture::ActionUndo; return true;
        case PrimitiveControl::ActionRedo: action = UiGesture::ActionRedo; return true;
        case PrimitiveControl::ActionScopeToggle: action = UiGesture::ActionScopeToggle; return true;
        case PrimitiveControl::ActionQuick: action = UiGesture::ActionQuick; return true;
        case PrimitiveControl::ActionAlt: action = UiGesture::ActionAlt; return true;
        case PrimitiveControl::ActionPress: action = UiGesture::ActionPress; return true;
        case PrimitiveControl::ActionRelease: action = UiGesture::ActionRelease; return true;
        case PrimitiveControl::F1: action = UiGesture::F1; return true;
        case PrimitiveControl::F2: action = UiGesture::F2; return true;
        case PrimitiveControl::F3: action = UiGesture::F3; return true;
        case PrimitiveControl::F4: action = UiGesture::F4; return true;
        case PrimitiveControl::F5: action = UiGesture::F5; return true;
        case PrimitiveControl::F6: action = UiGesture::F6; return true;
        case PrimitiveControl::F7: action = UiGesture::F7; return true;
        case PrimitiveControl::F8: action = UiGesture::F8; return true;
        case PrimitiveControl::F9: action = UiGesture::F9; return true;
        case PrimitiveControl::F10: action = UiGesture::F10; return true;
        case PrimitiveControl::F11: action = UiGesture::F11; return true;
        case PrimitiveControl::F12: action = UiGesture::F12; return true;
        case PrimitiveControl::None:
        default:
            action = UiGesture::None;
            return false;
    }
}

bool PressPolicyResolver::allowsRepeat_(PrimitiveControl control) noexcept {
    switch (control) {
        case PrimitiveControl::ListUp:
        case PrimitiveControl::ListDown:
        case PrimitiveControl::TrackPagePrev:
        case PrimitiveControl::TrackPageNext:
        case PrimitiveControl::ActionFocusPrev:
        case PrimitiveControl::ActionFocusNext:
        case PrimitiveControl::ActionAdjustPrev:
        case PrimitiveControl::ActionAdjustNext:
        case PrimitiveControl::TrackSpeedUp:
        case PrimitiveControl::TrackSpeedDown:
        case PrimitiveControl::BpmUp:
        case PrimitiveControl::BpmDown:
            return true;
        default:
            return false;
    }
}

PressPolicy PressPolicyResolver::resolve(UiScene scene, PrimitiveControl control) const noexcept {
    PressPolicy out{};
    UiGesture tapAction = UiGesture::None;
    int16_t tapValue = 0;
    if (!mapControlToTap_(control, tapAction, tapValue)) {
        return out;
    }

    out.valid = true;
    out.tapAction = tapAction;
    out.tapValue = tapValue;
    out.repeatEnabled = allowsRepeat_(control);

    // Специальная press-политика по ТЗ:
    // только в SampleEdit для F1 различаем tap/hold:
    // tap -> PreviewPlay, hold -> OpenSampleContextMenu.
    if (control == PrimitiveControl::F1 && scene == UiScene::SampleEdit) {
        out.tapAction = UiGesture::PreviewPlay;
        out.tapValue = 0;
        out.holdEnabled = true;
        out.holdThresholdMs = 300;
        out.holdAction = UiGesture::OpenSampleContextMenu;
        out.holdValue = 0;
        out.repeatEnabled = false;
    }
    return out;
}

} // namespace avantgarde

