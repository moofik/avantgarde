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
    BpmDown
};

struct UiInputEvent {
    UiInputAction action{UiInputAction::None};
};

struct IUiInput {
    virtual ~IUiInput() = default;
    virtual bool poll(UiInputEvent& out) noexcept = 0;
};

} // namespace avantgarde
