#include <catch2/catch_all.hpp>

#include "control/TerminalUiInput.h"

using namespace avantgarde;

TEST_CASE("TerminalUiInput: key mapping") {
    REQUIRE(TerminalUiInput::mapKey('q') == UiGesture::Quit);
    REQUIRE(TerminalUiInput::mapKey('1') == UiGesture::SelectPrevTrack);
    REQUIRE(TerminalUiInput::mapKey('2') == UiGesture::SelectNextTrack);
    REQUIRE(TerminalUiInput::mapKey(',') == UiGesture::TrackPagePrev);
    REQUIRE(TerminalUiInput::mapKey('.') == UiGesture::TrackPageNext);
    REQUIRE(TerminalUiInput::mapKey('p') == UiGesture::PlayActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('s') == UiGesture::StopActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('u') == UiGesture::UnmuteActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('i') == UiGesture::MuteActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('t') == UiGesture::MuteToggleActiveTrack);
    REQUIRE(TerminalUiInput::mapKey('r') == UiGesture::ArmToggleActiveTrack);
    REQUIRE(TerminalUiInput::mapKey(';') == UiGesture::ActionFocusPrev);
    REQUIRE(TerminalUiInput::mapKey('\'') == UiGesture::ActionFocusNext);
    REQUIRE(TerminalUiInput::mapKey('/') == UiGesture::ActionAdjustPrev);
    REQUIRE(TerminalUiInput::mapKey('?') == UiGesture::ActionAdjustNext);
    REQUIRE(TerminalUiInput::mapKey('o') == UiGesture::ActionApply);
    REQUIRE(TerminalUiInput::mapKey('y') == UiGesture::ActionUndo);
    REQUIRE(TerminalUiInput::mapKey('=') == UiGesture::TrackSpeedUp);
    REQUIRE(TerminalUiInput::mapKey('-') == UiGesture::TrackSpeedDown);
    REQUIRE(TerminalUiInput::mapKey('z') == UiGesture::QuantNone);
    REQUIRE(TerminalUiInput::mapKey('x') == UiGesture::QuantBeat);
    REQUIRE(TerminalUiInput::mapKey('c') == UiGesture::QuantBar);
    REQUIRE(TerminalUiInput::mapKey(']') == UiGesture::BpmUp);
    REQUIRE(TerminalUiInput::mapKey('[') == UiGesture::BpmDown);
    REQUIRE(TerminalUiInput::mapKey('v') == UiGesture::None);
}

TEST_CASE("TerminalUiInput: escape sequence mapping") {
    REQUIRE(TerminalUiInput::mapEscapeSequence("[A") == UiGesture::ActionAdjustNext);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[B") == UiGesture::ActionAdjustPrev);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[C") == UiGesture::ActionFocusNext);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[D") == UiGesture::ActionFocusPrev);

    REQUIRE(TerminalUiInput::mapEscapeSequence("OP") == UiGesture::F1);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OQ") == UiGesture::F2);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OR") == UiGesture::F3);
    REQUIRE(TerminalUiInput::mapEscapeSequence("OS") == UiGesture::F4);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[15~") == UiGesture::F5);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[17~") == UiGesture::F6);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[18~") == UiGesture::F7);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[19~") == UiGesture::F8);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[20~") == UiGesture::F9);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[21~") == UiGesture::F10);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[23~") == UiGesture::F11);
    REQUIRE(TerminalUiInput::mapEscapeSequence("[24~") == UiGesture::F12);
}
