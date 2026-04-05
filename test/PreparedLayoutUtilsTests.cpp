#include <catch2/catch_test_macros.hpp>

#include "contracts/UiPreparedLayout.h"
#include "platform/render/PreparedLayoutUtils.h"

namespace avantgarde {

TEST_CASE("PreparedLayoutUtils: buildComponentIndex indexes nested view slots") {
    UiPreparedLayout prepared{};
    UiTrackViewBuilder view("track_view");
    view.addToSlot("left", UiStatusBarBuilder("header_title").text("TRACKS"));
    view.addToSlot("left", UiKnobBuilder("knob_speed").label("SPD").value01(0.5f));
    prepared.components.push_back(std::move(view).build());

    const render::UiComponentIndex index = render::buildComponentIndex(prepared);
    REQUIRE(index.find("track_view") != index.end());
    REQUIRE(index.find("header_title") != index.end());
    REQUIRE(index.find("knob_speed") != index.end());
}

TEST_CASE("PreparedLayoutUtils: estimateInnerHeight respects frame hint and list rows") {
    UiPreparedLayout hinted{};
    hinted.frameHeightHint = 33U;
    REQUIRE(render::estimateInnerHeight(hinted) == 33U);

    UiPreparedLayout prepared{};
    UiListBuilder list("tracks_body");
    list.addRow("one").addRow("two").addRow("three");
    prepared.components.push_back(std::move(list).build());
    // base(8) + rows(3) = 11, затем min=12.
    REQUIRE(render::estimateInnerHeight(prepared) == 12U);
}

TEST_CASE("PreparedLayoutUtils: markerPrefix returns marker only for selected row") {
    REQUIRE(render::markerPrefix(UiListComponent::Marker::Arrow, false) == "  ");
    REQUIRE(render::markerPrefix(UiListComponent::Marker::Arrow, true) == "> ");
    REQUIRE(render::markerPrefix(UiListComponent::Marker::Dot, true) == "* ");
    REQUIRE(render::markerPrefix(UiListComponent::Marker::None, true) == "  ");
}

} // namespace avantgarde
