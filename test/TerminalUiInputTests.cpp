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
    REQUIRE(TerminalUiInput::mapKey(';') == UiInputAction::ActionFocusPrev);
    REQUIRE(TerminalUiInput::mapKey('\'') == UiInputAction::ActionFocusNext);
    REQUIRE(TerminalUiInput::mapKey('/') == UiInputAction::ActionAdjustPrev);
    REQUIRE(TerminalUiInput::mapKey('?') == UiInputAction::ActionAdjustNext);
    REQUIRE(TerminalUiInput::mapKey('o') == UiInputAction::ActionApply);
    REQUIRE(TerminalUiInput::mapKey('y') == UiInputAction::ActionUndo);
    REQUIRE(TerminalUiInput::mapKey('=') == UiInputAction::TrackSpeedUp);
    REQUIRE(TerminalUiInput::mapKey('-') == UiInputAction::TrackSpeedDown);
    REQUIRE(TerminalUiInput::mapKey('z') == UiInputAction::QuantNone);
    REQUIRE(TerminalUiInput::mapKey('x') == UiInputAction::QuantBeat);
    REQUIRE(TerminalUiInput::mapKey('c') == UiInputAction::QuantBar);
    REQUIRE(TerminalUiInput::mapKey(']') == UiInputAction::BpmUp);
    REQUIRE(TerminalUiInput::mapKey('[') == UiInputAction::BpmDown);
    REQUIRE(TerminalUiInput::mapKey('v') == UiInputAction::None);
}

TEST_CASE("TerminalUiInput: escape sequence mapping") {
    REQUIRE(TerminalUiInput::mapEscapeSequence("[A") == UiInputAction::ActionAdjustNext);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[B") == UiInputAction::ActionAdjustPrev);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[C") == UiInputAction::ActionFocusNext);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[D") == UiInputAction::ActionFocusPrev);

    REQUIRE(TerminalUiInput::mapEscapeSequence("OP") == UiInputAction::F1);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OQ") == UiInputAction::F2);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OR") == UiInputAction::F3);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OS") == UiInputAction::F4);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[15~") == UiInputAction::F5);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[17~") == UiInputAction::F6);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[18~") == UiInputAction::F7);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[19~") == UiInputAction::F8);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[20~") == UiInputAction::F9);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[21~") == UiInputAction::F10);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[23~") == UiInputAction::F11);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[24~") == UiInputAction::F12);
}
