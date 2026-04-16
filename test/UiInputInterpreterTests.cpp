#include <catch2/catch_test_macros.hpp>

#include "service/ui/input/UiInputInterpreter.h"

namespace avantgarde {

TEST_CASE("UiInputInterpreter: SampleEdit F1 tap maps to PreviewPlay") {
    UiInputInterpreter sut{};

    PrimitiveInputEvent down{};
    down.control = PrimitiveControl::F1;
    down.phase = PrimitivePhase::Down;
    sut.onPrimitiveEvent(down, UiScene::SampleEdit, 1000);

    UiGestureEvent out{};
    REQUIRE_FALSE(sut.poll(out));

    PrimitiveInputEvent up{};
    up.control = PrimitiveControl::F1;
    up.phase = PrimitivePhase::Up;
    sut.onPrimitiveEvent(up, UiScene::SampleEdit, 1080);

    REQUIRE(sut.poll(out));
    CHECK(out.action == UiGesture::PreviewPlay);
    CHECK(out.press == UiPressType::Tap);
}

TEST_CASE("UiInputInterpreter: SampleEdit F1 hold maps to OpenSampleContextMenu") {
    UiInputInterpreter sut{};

    PrimitiveInputEvent down{};
    down.control = PrimitiveControl::F1;
    down.phase = PrimitivePhase::Down;
    sut.onPrimitiveEvent(down, UiScene::SampleEdit, 1000);

    sut.tick(1301);

    UiGestureEvent out{};
    REQUIRE(sut.poll(out));
    CHECK(out.action == UiGesture::OpenSampleContextMenu);
    CHECK(out.press == UiPressType::Hold);

    PrimitiveInputEvent up{};
    up.control = PrimitiveControl::F1;
    up.phase = PrimitivePhase::Up;
    sut.onPrimitiveEvent(up, UiScene::SampleEdit, 1320);

    CHECK_FALSE(sut.poll(out));
}

TEST_CASE("UiInputInterpreter: Tracks F1 stays immediate") {
    UiInputInterpreter sut{};

    PrimitiveInputEvent down{};
    down.control = PrimitiveControl::F1;
    down.phase = PrimitivePhase::Down;
    sut.onPrimitiveEvent(down, UiScene::Tracks, 1000);

    UiGestureEvent out{};
    REQUIRE(sut.poll(out));
    CHECK(out.action == UiGesture::F1);
    CHECK(out.press == UiPressType::Tap);
}

TEST_CASE("UiInputInterpreter: Repeat emitted only for repeatable controls") {
    UiInputInterpreter sut{};

    PrimitiveInputEvent rep1{};
    rep1.control = PrimitiveControl::TrackSpeedUp;
    rep1.phase = PrimitivePhase::Repeat;
    sut.onPrimitiveEvent(rep1, UiScene::Tracks, 1000);

    UiGestureEvent out{};
    REQUIRE(sut.poll(out));
    CHECK(out.action == UiGesture::TrackSpeedUp);
    CHECK(out.press == UiPressType::Repeat);

    PrimitiveInputEvent rep2{};
    rep2.control = PrimitiveControl::F1;
    rep2.phase = PrimitivePhase::Repeat;
    sut.onPrimitiveEvent(rep2, UiScene::Tracks, 1100);
    CHECK_FALSE(sut.poll(out));
}

} // namespace avantgarde

