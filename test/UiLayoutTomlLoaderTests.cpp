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

[[layout.children.children]]
type = "knob"
id = "wet"
label = "WET"
bind = "scene.fx.param.value.0"

[[layout.children.children]]
type = "anim_slot"
id = "fx_anim"
size = [128, 128]
bind = "fx.anim.current"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));
    REQUIRE(err.empty());

    REQUIRE(tpl.widgetId == "fx_editor");
    REQUIRE(tpl.root.type == UiLayoutNodeType::Column);
    REQUIRE(tpl.root.children.size() == 2);
    REQUIRE(tpl.root.children[0].type == UiLayoutNodeType::StatusBar);
    REQUIRE(tpl.root.children[0].text == "FX EDITOR");
    REQUIRE(tpl.root.children[1].type == UiLayoutNodeType::Row);
    REQUIRE(tpl.root.children[1].children.size() == 2);
    REQUIRE(tpl.root.children[1].children[0].type == UiLayoutNodeType::Knob);
    REQUIRE(tpl.root.children[1].children[0].bind == "scene.fx.param.value.0");
    REQUIRE(tpl.root.children[1].children[1].type == UiLayoutNodeType::AnimSlot);
    REQUIRE(tpl.root.children[1].children[1].width.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[1].children[1].width.value == Catch::Approx(128.0f));
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

