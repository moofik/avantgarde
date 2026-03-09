#pragma once

#include <cstdint>

namespace avantgarde {

enum class UiInputAction : uint8_t {
    None = 0,
    Quit,
    SelectTrack0,
    SelectTrack1,
    PlayActiveTrack,
    StopActiveTrack,
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
