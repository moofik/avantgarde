#include <catch2/catch_all.hpp>

#include "control/TerminalUiInput.h"

using namespace avantgarde;

TEST_CASE("TerminalUiInput: key mapping") {
    REQUIRE(TerminalUiInput::mapKey('q') == UiInputAction::Quit);
    REQUIRE(TerminalUiInput::mapKey('1') == UiInputAction::SelectPrevTrack);
    REQUIRE(TerminalUiInput::mapKey('2') == UiInputAction::SelectNextTrack);
    REQUIRE(TerminalUiInput::mapKey(',') == UiInputAction::TrackPagePrev);
    REQUIRE(TerminalUiInput::mapKey('.') == UiInputAction::TrackPageNext);
    REQUIRE(TerminalUiInput::mapKey('p') == UiInputAction::PlayActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('s') == UiInputAction::StopActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('u') == UiInputAction::UnmuteActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('i') == UiInputAction::MuteActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('t') == UiInputAction::MuteToggleActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('r') == UiInputAction::ArmToggleActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('=') == UiInputAction::TrackSpeedUp);
    REQUIRE(TerminalUiInput::mapKey('-') == UiInputAction::TrackSpeedDown);
    REQUIRE(TerminalUiInput::mapKey('z') == UiInputAction::QuantNone);
    REQUIRE(TerminalUiInput::mapKey('x') == UiInputAction::QuantBeat);
    REQUIRE(TerminalUiInput::mapKey('c') == UiInputAction::QuantBar);
    REQUIRE(TerminalUiInput::mapKey(']') == UiInputAction::BpmUp);
    REQUIRE(TerminalUiInput::mapKey('[') == UiInputAction::BpmDown);
    REQUIRE(TerminalUiInput::mapKey('v') == UiInputAction::None);
}
