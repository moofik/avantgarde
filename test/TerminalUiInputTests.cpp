#include <catch2/catch_all.hpp>

#include "control/TerminalUiInput.h"

using namespace avantgarde;

TEST_CASE("TerminalUiInput: key mapping") {
    REQUIRE(TerminalUiInput::mapKey('q') == UiInputAction::Quit);
    REQUIRE(TerminalUiInput::mapKey('1') == UiInputAction::SelectTrack0);
    REQUIRE(TerminalUiInput::mapKey('2') == UiInputAction::SelectTrack1);
    REQUIRE(TerminalUiInput::mapKey('p') == UiInputAction::PlayActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('s') == UiInputAction::StopActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('=') == UiInputAction::TrackSpeedUp);
    REQUIRE(TerminalUiInput::mapKey('-') == UiInputAction::TrackSpeedDown);
    REQUIRE(TerminalUiInput::mapKey('z') == UiInputAction::QuantNone);
    REQUIRE(TerminalUiInput::mapKey('x') == UiInputAction::QuantBeat);
    REQUIRE(TerminalUiInput::mapKey('c') == UiInputAction::QuantBar);
    REQUIRE(TerminalUiInput::mapKey(']') == UiInputAction::BpmUp);
    REQUIRE(TerminalUiInput::mapKey('[') == UiInputAction::BpmDown);
    REQUIRE(TerminalUiInput::mapKey('v') == UiInputAction::None);
}
