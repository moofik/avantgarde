#pragma once

#include <cstdint>

namespace avantgarde {

enum class UiInputAction : uint8_t {
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

    // Active Action Pointer controls (reserved contract for encoder-like UI).
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

    // F-row reserved actions for hardware-like mapping.
    // Пока не привязаны к логике: резервируем в контракте заранее.
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

struct UiInputEvent {
    UiInputAction action{UiInputAction::None};
};

struct IUiInput {
    virtual ~IUiInput() = default;
    virtual bool poll(UiInputEvent& out) noexcept = 0;
};

} // namespace avantgarde
