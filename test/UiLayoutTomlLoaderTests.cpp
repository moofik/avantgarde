#include <catch2/catch_all.hpp>

#include "service/ui/UiLayoutTomlLoader.h"

using namespace avantgarde;

TEST_CASE("UiLayoutTomlLoader: parses fx_editor template with nested children") {
    const char* toml = R"(
id = "fx_editor"

[layout]
type = "column"
padding = 2
gap = 1

[[layout.children]]
type = "statusbar"
text = "FX EDITOR"
width = "100%"

[[layout.children]]
type = "row"
gap = 3
wrap = false
justify = "space_between"
align = "center"

[[layout.children.children]]
type = "knob"
id = "wet"
label = "WET"
bind = "scene.fx.param.value.0"
knob_size = 1.35
effect = "glitch"
effect_trigger = "change"
effect_transition = "instant"
effect_trigger_out = "1s"

[[layout.children.children]]
type = "anim_slot"
id = "fx_anim"
size = [128, 128]
bind = "fx.anim.current"

[[layout.children.children]]
type = "switch"
id = "retrig_mode"
label = "RTRG"
bind = "scene.fx.param.value.3"
options = ["OFF", "1", "2", "4"]

[[layout.children]]
type = "text"
id = "keys"
text_wrap = true
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));
    REQUIRE(err.empty());

    REQUIRE(tpl.widgetId == "fx_editor");
    REQUIRE(tpl.root.type == UiLayoutNodeType::Column);
    REQUIRE(tpl.root.children.size() == 3);
    REQUIRE(tpl.root.children[0].type == UiLayoutNodeType::StatusBar);
    REQUIRE(tpl.root.children[0].text == "FX EDITOR");
    REQUIRE(tpl.root.children[1].type == UiLayoutNodeType::Row);
    REQUIRE_FALSE(tpl.root.children[1].wrap);
    REQUIRE(tpl.root.children[1].justify == UiLayoutJustify::SpaceBetween);
    REQUIRE(tpl.root.children[1].align == UiLayoutAlign::Center);
    REQUIRE(tpl.root.children[1].children.size() == 3);
    REQUIRE(tpl.root.children[1].children[0].type == UiLayoutNodeType::Knob);
    REQUIRE(tpl.root.children[1].children[0].bind == "scene.fx.param.value.0");
    REQUIRE(tpl.root.children[1].children[0].knobSize == Catch::Approx(1.35f));
    REQUIRE_FALSE(tpl.root.children[1].children[0].effects.empty());
    REQUIRE(tpl.root.children[1].children[0].effects[0].effectTransition == "instant");
    REQUIRE(tpl.root.children[1].children[0].effects[0].effectTriggerOutMs == 1000U);
    REQUIRE(tpl.root.children[1].children[1].type == UiLayoutNodeType::AnimSlot);
    REQUIRE(tpl.root.children[1].children[1].width.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[1].children[1].width.value == Catch::Approx(128.0f));
    REQUIRE(tpl.root.children[1].children[2].type == UiLayoutNodeType::Switch);
    REQUIRE(tpl.root.children[1].children[2].options.size() == 4);
    REQUIRE(tpl.root.children[1].children[2].options[0] == "OFF");
    REQUIRE(tpl.root.children[1].children[2].options[3] == "4");
    REQUIRE(tpl.root.children[2].type == UiLayoutNodeType::Text);
    REQUIRE(tpl.root.children[2].textWrap);
}

TEST_CASE("UiLayoutTomlLoader: parses knob_size and clamps extreme values") {
    const char* toml = R"(
id = "tracks"

[layout]
type = "column"

[[layout.children]]
type = "knob"
id = "k_small"
knob_size = 0.1

[[layout.children]]
type = "knob"
id = "k_big"
knob_size = 99

[[layout.children]]
type = "knob"
id = "k_normal"
knob_size = 1.75
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 3);
    REQUIRE(tpl.root.children[0].knobSize == Catch::Approx(0.2f));
    REQUIRE(tpl.root.children[1].knobSize == Catch::Approx(4.0f));
    REQUIRE(tpl.root.children[2].knobSize == Catch::Approx(1.75f));
}

TEST_CASE("UiLayoutTomlLoader: parses effect_trigger_out in ms and seconds") {
    const char* toml = R"(
id = "tracks"

[layout]
type = "column"

[[layout.children]]
type = "knob"
id = "k1"
effect_trigger_out = "750ms"

[[layout.children]]
type = "knob"
id = "k2"
effect_trigger_out = "1.5s"

[[layout.children]]
type = "knob"
id = "k3"
effect_trigger_out = 250
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 3);
    REQUIRE_FALSE(tpl.root.children[0].effects.empty());
    REQUIRE_FALSE(tpl.root.children[1].effects.empty());
    REQUIRE_FALSE(tpl.root.children[2].effects.empty());
    REQUIRE(tpl.root.children[0].effects[0].effectTriggerOutMs == 750U);
    REQUIRE(tpl.root.children[1].effects[0].effectTriggerOutMs == 1500U);
    REQUIRE(tpl.root.children[2].effects[0].effectTriggerOutMs == 250U);
}

TEST_CASE("UiLayoutTomlLoader: fails on malformed headers") {
    const char* badToml = R"(
id = "fx_editor"
[[layout.invalid]]
type = "knob"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE_FALSE(UiLayoutTomlLoader::loadFromString(badToml, tpl, err));
    REQUIRE_FALSE(err.empty());
}
