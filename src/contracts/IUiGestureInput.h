#pragma once

#include <cstdint>

namespace avantgarde {

// Низкоуровневые пользовательские жесты/команды управления.
// Это слой "физического" ввода (клавиши/энкодеры/кнопки),
// не бизнес-действия домена.
enum class UiGesture : uint8_t {
    None = 0,
    Quit,
    SelectPrevTrack,
    SelectNextTrack,
    TrackPagePrev,
    TrackPageNext,
    OpenManager,
    BackScene,
    ListUp,
    ListDown,
    ListEnter,
    ListParent,
    PreviewPlay,
    PreviewAutoToggle,
    PlayActiveTrack,
    StopActiveTrack,
    UnmuteActiveTrack,
    MuteActiveTrack,
    MuteToggleActiveTrack,
    ArmToggleActiveTrack,
    TrackSpeedUp,
    TrackSpeedDown,
    QuantNone,
    QuantBeat,
    QuantBar,
    BpmUp,
    BpmDown,

    // Active Action Pointer controls (encoder-like model).
    ActionFocusPrev,
    ActionFocusNext,
    ActionAdjustPrev,
    ActionAdjustNext,
    ActionApply,
    ActionUndo,
    ActionRedo,
    ActionScopeToggle,
    ActionQuick,
    ActionAlt,
    ActionPress,
    ActionRelease,

    // F-row for hardware-like mapping.
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
};

struct UiGestureEvent {
    UiGesture action{UiGesture::None};
};

struct IUiGestureInput {
    virtual ~IUiGestureInput() = default;
    virtual bool poll(UiGestureEvent& out) noexcept = 0;
};

} // namespace avantgarde
