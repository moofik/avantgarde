#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "contracts/UiPreparedLayout.h"
#include "platform/render/RenderGeometry.h"

namespace avantgarde {

TEST_CASE("RenderGeometry: computeFrameMetrics keeps frame centered with adaptive grid") {
    UiPreparedLayout prepared{};
    prepared.frameWidth = 60U;

    const render::UiFrameMetrics m = render::computeFrameMetrics(
        prepared,
        640.0f,
        480.0f);

    REQUIRE(m.frameWidthChars == 60U);
    REQUIRE(m.innerHeightChars == 12U);
    REQUIRE(m.frameHeightChars == 14U);
    REQUIRE(m.cellWidthPx == Catch::Approx(10.2f).epsilon(0.001));
    REQUIRE(m.cellHeightPx == Catch::Approx(18.0f).epsilon(0.001));
    REQUIRE(m.frameWidthPx == Catch::Approx(640.0f).epsilon(0.001));
    REQUIRE(m.offsetXPx == Catch::Approx(0.0f).epsilon(0.001));
    REQUIRE(m.offsetYPx == Catch::Approx(100.0f).epsilon(0.001));
}

TEST_CASE("RenderGeometry: charsToPixelsTopDown and toBottomUp convert coordinates predictably") {
    UiPreparedLayout prepared{};
    prepared.frameWidth = 60U;
    const render::UiFrameMetrics m = render::computeFrameMetrics(
        prepared,
        640.0f,
        480.0f);

    const render::UiRectPx top = render::charsToPixelsTopDown(m, 1, 2, 3, 4);
    REQUIRE(top.x == Catch::Approx(24.2f).epsilon(0.001));
    REQUIRE(top.y == Catch::Approx(150.0f).epsilon(0.001));
    REQUIRE(top.w == Catch::Approx(30.6f).epsilon(0.001));
    REQUIRE(top.h == Catch::Approx(72.0f).epsilon(0.001));

    const render::UiRectPx bottom = render::toBottomUp(top, 480.0f);
    REQUIRE(bottom.y == Catch::Approx(258.0f).epsilon(0.001));
}

TEST_CASE("RenderGeometry: layout root width/height override prepared frame size") {
    UiLayoutTemplate tpl{};
    tpl.widgetId = "test";
    tpl.root.type = UiLayoutNodeType::Column;
    tpl.root.width = UiLayoutSize{UiLayoutSize::Unit::Px, 72.0f};
    tpl.root.height = UiLayoutSize{UiLayoutSize::Unit::Px, 20.0f};

    UiPreparedLayout prepared{};
    prepared.layoutTemplate = &tpl;
    prepared.frameWidth = 60U;
    prepared.frameHeightHint = 12U;

    const render::UiFrameMetrics m = render::computeFrameMetrics(
        prepared,
        640.0f,
        480.0f);

    REQUIRE(m.frameWidthChars == 72U);
    REQUIRE(m.innerHeightChars == 20U);
    REQUIRE(m.frameHeightChars == 22U);
}

} // namespace avantgarde
