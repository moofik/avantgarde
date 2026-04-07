#include <catch2/catch_all.hpp>

#include "service/ui/UiLayoutJsonLoader.h"

using namespace avantgarde;

TEST_CASE("UiLayoutJsonLoader: parses nested layout template") {
    const char* json = R"json(
{
  "id": "fx_editor",
  "layout": {
    "type": "column",
    "padding": 2,
    "gap": 1,
    "children": [
      {
        "type": "statusbar",
        "text": "FX EDITOR",
        "width": "100%"
      },
      {
        "type": "row",
        "gap": 3,
        "wrap": false,
        "justify": "space_between",
        "align": "center",
        "children": [
          {
            "type": "knob",
            "id": "wet",
            "label": "WET",
            "bind": "scene.fx.param.value.0",
            "knob_size": 1.35,
            "effects": [
              {
                "type": "glitch",
                "effect_trigger": "change",
                "effect_transition": "instant",
                "effect_trigger_out": "1s"
              },
              {
                "type": "glow",
                "effect_trigger": "change",
                "effect_trigger_out": "1200ms",
                "effect_color": "#A86DB5"
              }
            ]
          },
          {
            "type": "anim_slot",
            "id": "fx_anim",
            "size": [128, 128],
            "bind": "fx.anim.current"
          },
          {
            "type": "switch",
            "id": "retrig_mode",
            "label": "RTRG",
            "bind": "scene.fx.param.value.3",
            "options": ["OFF", "1", "2", "4"]
          }
        ]
      }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());

    REQUIRE(tpl.widgetId == "fx_editor");
    REQUIRE(tpl.root.type == UiLayoutNodeType::Column);
    REQUIRE(tpl.root.children.size() == 2);
    REQUIRE(tpl.root.children[1].type == UiLayoutNodeType::Row);
    REQUIRE_FALSE(tpl.root.children[1].wrap);
    REQUIRE(tpl.root.children[1].children.size() == 3);
    REQUIRE(tpl.root.children[1].children[0].type == UiLayoutNodeType::Knob);
    REQUIRE(tpl.root.children[1].children[0].knobSize == Catch::Approx(1.35f));
    REQUIRE(tpl.root.children[1].children[0].effects.size() == 2);
    REQUIRE(tpl.root.children[1].children[0].effects[0].type == "glitch");
    REQUIRE(tpl.root.children[1].children[0].effects[0].effectTransition == "instant");
    REQUIRE(tpl.root.children[1].children[0].effects[0].effectTriggerOutMs == 1000U);
    REQUIRE(tpl.root.children[1].children[0].effects[1].type == "glow");
    REQUIRE(tpl.root.children[1].children[0].effects[1].effectTriggerOutMs == 1200U);
    REQUIRE(tpl.root.children[1].children[0].effects[1].effectColor == "#A86DB5");
    REQUIRE(tpl.root.children[1].children[1].width.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[1].children[1].width.value == Catch::Approx(128.0f));
    REQUIRE(tpl.root.children[1].children[2].options.size() == 4);
}

TEST_CASE("UiLayoutJsonLoader: parses size and duration formats") {
    const char* json = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      { "type": "knob", "id": "k1", "effects": [{ "type": "glitch", "effect_trigger_out": "750ms" }] },
      { "type": "knob", "id": "k2", "effects": [{ "type": "glitch", "effect_trigger_out": "1.5s" }] },
      { "type": "knob", "id": "k3", "effects": [{ "type": "glitch", "effect_trigger_out": 250 }] },
      { "type": "knob", "id": "k4", "width": "75%" },
      { "type": "knob", "id": "k5", "height": 3 }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 5);
    REQUIRE(tpl.root.children[0].effects[0].effectTriggerOutMs == 750U);
    REQUIRE(tpl.root.children[1].effects[0].effectTriggerOutMs == 1500U);
    REQUIRE(tpl.root.children[2].effects[0].effectTriggerOutMs == 250U);
    REQUIRE(tpl.root.children[3].width.unit == UiLayoutSize::Unit::Percent);
    REQUIRE(tpl.root.children[3].width.value == Catch::Approx(75.0f));
    REQUIRE(tpl.root.children[4].height.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[4].height.value == Catch::Approx(3.0f));
}

TEST_CASE("UiLayoutJsonLoader: rejects removed legacy effect keys") {
    const char* json = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      { "type": "text", "id": "title", "effect": "glitch" }
    ]
  }
}
)json";
    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE_FALSE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.find("removed") != std::string::npos);
}

TEST_CASE("UiLayoutJsonLoader: fails on malformed json") {
    const char* badJson = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      { "type": "text", "id": "x" }
    ]
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE_FALSE(UiLayoutJsonLoader::loadFromString(badJson, tpl, err));
    REQUIRE_FALSE(err.empty());
}
