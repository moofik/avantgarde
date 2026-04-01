#include <catch2/catch_all.hpp>

#include "service/ui/UiKnobComposer.h"

using namespace avantgarde;

TEST_CASE("UiKnobComposer: draws knob at requested position") {
    UiKnobComposer panel(24, 10);
    UiKnobModel knob{};
    knob.label = "WET";
    knob.value01 = 0.75f;
    knob.x = 3;
    knob.y = 1;
    knob.selected = true;
    panel.drawKnob(knob);

    const auto& lines = panel.lines();
    REQUIRE(lines.size() == 10);
    REQUIRE(lines[4].find('#') != std::string::npos);
    REQUIRE(lines[9].find("WET") != std::string::npos);
}
