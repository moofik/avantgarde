#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

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
            "bind": "fx.selected.param.0",
            "target": "param.fx.selected.0",
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
            "bind": "fx.selected.param.3",
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
    REQUIRE(tpl.root.children[1].children[0].target == "param.fx.selected.0");
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

TEST_CASE("UiLayoutJsonLoader: parses icon, margin and readable size units") {
    const char* json = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      {
        "type": "icon",
        "id": "track_state_icon",
        "path": "images/state.png",
        "margin": 2,
        "width": "12cols",
        "height": "6rows"
      },
      {
        "type": "icon",
        "id": "track_state_icon_2",
        "path": "assets/images/state2.png",
        "width": "24px",
        "height": "8cells"
      }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 2);

    REQUIRE(tpl.root.children[0].type == UiLayoutNodeType::Icon);
    REQUIRE(tpl.root.children[0].assetPath == "images/state.png");
    REQUIRE(tpl.root.children[0].margin == 2U);
    REQUIRE(tpl.root.children[0].width.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[0].width.value == Catch::Approx(12.0f));
    REQUIRE(tpl.root.children[0].height.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[0].height.value == Catch::Approx(6.0f));

    REQUIRE(tpl.root.children[1].type == UiLayoutNodeType::Icon);
    REQUIRE(tpl.root.children[1].width.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[1].width.value == Catch::Approx(24.0f));
    REQUIRE(tpl.root.children[1].height.unit == UiLayoutSize::Unit::Px);
    REQUIRE(tpl.root.children[1].height.value == Catch::Approx(8.0f));
}

TEST_CASE("UiLayoutJsonLoader: resolves style_ref cascade and local overrides") {
    const char* json = R"json(
{
  "id": "styled_scene",
  "styles": {
    "text.base": {
      "font": "gothic",
      "font_size": 14,
      "text_color": "#AABBCC",
      "width": "80%"
    },
    "text.alert": {
      "style_ref": "@styles.text.base",
      "text_color": "#FF2288"
    },
    "frame.root": {
      "background_color": "#0B0711",
      "border_color": "#6F4A77",
      "default_text_color": "#C7B2CC"
    }
  },
  "layout": {
    "type": "column",
    "style_ref": "@styles.frame.root",
    "children": [
      {
        "type": "text",
        "id": "line_1",
        "style_ref": "@styles.text.base"
      },
      {
        "type": "text",
        "id": "line_2",
        "style_ref": ["@styles.text.base", "@styles.text.alert"],
        "font_size": 20
      }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 2);

    REQUIRE(tpl.root.backgroundColor == "#0B0711");
    REQUIRE(tpl.root.borderColor == "#6F4A77");
    REQUIRE(tpl.root.defaultTextColor == "#C7B2CC");

    REQUIRE(tpl.root.children[0].font == "gothic");
    REQUIRE(tpl.root.children[0].fontSize == Catch::Approx(14.0f));
    REQUIRE(tpl.root.children[0].textColor == "#AABBCC");
    REQUIRE(tpl.root.children[0].width.unit == UiLayoutSize::Unit::Percent);
    REQUIRE(tpl.root.children[0].width.value == Catch::Approx(80.0f));

    REQUIRE(tpl.root.children[1].font == "gothic");
    REQUIRE(tpl.root.children[1].fontSize == Catch::Approx(20.0f)); // Локальное поле выше style_ref.
    REQUIRE(tpl.root.children[1].textColor == "#FF2288");           // Последний style_ref переопределяет цвет.
}

TEST_CASE("UiLayoutJsonLoader: resolves @theme and @effects preset references") {
    const char* json = R"json(
{
  "id": "styled_scene",
  "themes": {
    "colors": {
      "accent": "#A86DB5"
    },
    "fonts": {
      "main": "gothic"
    },
    "sizes": {
      "title": 19
    }
  },
  "effects": {
    "mode": {
      "glow": {
        "type": "glow",
        "effect_trigger": "change",
        "effect_speed": 0.75,
        "effect_color": "@theme.colors.accent"
      }
    }
  },
  "styles": {
    "title": {
      "font": "@theme.fonts.main",
      "font_size": "@theme.sizes.title",
      "text_color": "@theme.colors.accent",
      "effects": [
        { "preset": "@effects.mode.glow" }
      ]
    }
  },
  "layout": {
    "type": "column",
    "children": [
      { "type": "text", "id": "line_1", "style_ref": "@styles.title" }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 1);
    REQUIRE(tpl.root.children[0].font == "gothic");
    REQUIRE(tpl.root.children[0].fontSize == Catch::Approx(19.0f));
    REQUIRE(tpl.root.children[0].textColor == "#A86DB5");
    REQUIRE(tpl.root.children[0].effects.size() == 1);
    REQUIRE(tpl.root.children[0].effects[0].type == "glow");
    REQUIRE(tpl.root.children[0].effects[0].effectColor == "#A86DB5");
    REQUIRE(tpl.root.children[0].effects[0].effectSpeed == Catch::Approx(0.75f));
}

TEST_CASE("UiLayoutJsonLoader: resolves anim_ref from animations catalog") {
    const char* json = R"json(
{
  "id": "tracks",
  "animations": {
    "track": {
      "bottle": {
        "demo": {
          "mode": "scrub",
          "fps": 6,
          "frames": ["sprites/bottle/1.png", "sprites/bottle/2.png", "sprites/bottle/3.png"]
        }
      }
    }
  },
  "layout": {
    "type": "column",
    "children": [
      {
        "type": "anim_slot",
        "id": "track_anim",
        "bind": "track.selected.playhead",
        "anim_ref": "@animations.track.bottle.demo",
        "anim_fps": 10,
        "anim_show_frame": false,
        "anim_frame_width": 2,
        "anim_frame_radius": 7
      }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 1);
    REQUIRE(tpl.root.children[0].type == UiLayoutNodeType::AnimSlot);
    REQUIRE(tpl.root.children[0].animMode == "scrub");
    REQUIRE(tpl.root.children[0].animFps == Catch::Approx(10.0f)); // Локальный override выше anim_ref.
    REQUIRE(tpl.root.children[0].animFrames.size() == 3);
    REQUIRE(tpl.root.children[0].animFrames[0] == "sprites/bottle/1.png");
    REQUIRE_FALSE(tpl.root.children[0].animShowFrame);
    REQUIRE(tpl.root.children[0].animFrameWidth == Catch::Approx(2.0f));
    REQUIRE(tpl.root.children[0].animFrameRadius == Catch::Approx(7.0f));
}

TEST_CASE("UiLayoutJsonLoader: loads sibling styles.json for loadFromFile") {
    namespace fs = std::filesystem;
    const fs::path tempDir =
        fs::temp_directory_path() / fs::path("avantgarde_ui_loader_test_" + std::to_string(std::rand()));
    fs::create_directories(tempDir / "fx");

    const fs::path layoutPath = tempDir / "fx" / "layout.json";
    const fs::path stylesPath = tempDir / "styles.json";

    {
        std::ofstream out(stylesPath);
        REQUIRE(out.is_open());
        out << R"json(
{
  "styles": {
    "text.base": {
      "font": "gothic",
      "text_color": "#AABBCC",
      "width": "75%"
    }
  }
}
)json";
    }
    {
        std::ofstream out(layoutPath);
        REQUIRE(out.is_open());
        out << R"json(
{
  "id": "scene_file",
  "layout": {
    "type": "column",
    "children": [
      {
        "type": "text",
        "id": "line",
        "style_ref": "@styles.text.base"
      }
    ]
  }
}
)json";
    }

    UiLayoutTemplate tpl{};
    std::string err{};
    const bool ok = UiLayoutJsonLoader::loadFromFile(layoutPath.string(), tpl, err);
    fs::remove_all(tempDir);

    REQUIRE(ok);
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 1);
    REQUIRE(tpl.root.children[0].font == "gothic");
    REQUIRE(tpl.root.children[0].textColor == "#AABBCC");
    REQUIRE(tpl.root.children[0].width.unit == UiLayoutSize::Unit::Percent);
    REQUIRE(tpl.root.children[0].width.value == Catch::Approx(75.0f));
}

TEST_CASE("UiLayoutJsonLoader: fails when duplicate ancestor styles.json are present") {
    namespace fs = std::filesystem;
    const fs::path tempDir =
        fs::temp_directory_path() / fs::path("avantgarde_ui_loader_test_dup_" + std::to_string(std::rand()));
    fs::create_directories(tempDir / "fx");

    const fs::path layoutPath = tempDir / "fx" / "layout.json";
    const fs::path rootStylesPath = tempDir / "styles.json";
    const fs::path nestedStylesPath = tempDir / "fx" / "styles.json";

    {
        std::ofstream out(rootStylesPath);
        REQUIRE(out.is_open());
        out << R"json(
{
  "styles": {
    "text.base": { "font": "gothic" }
  }
}
)json";
    }
    {
        std::ofstream out(nestedStylesPath);
        REQUIRE(out.is_open());
        out << R"json(
{
  "styles": {
    "text.base": { "font": "mono" }
  }
}
)json";
    }
    {
        std::ofstream out(layoutPath);
        REQUIRE(out.is_open());
        out << R"json(
{
  "id": "scene_file",
  "layout": {
    "type": "column",
    "children": [
      { "type": "text", "id": "line", "style_ref": "@styles.text.base" }
    ]
  }
}
)json";
    }

    UiLayoutTemplate tpl{};
    std::string err{};
    const bool ok = UiLayoutJsonLoader::loadFromFile(layoutPath.string(), tpl, err);
    fs::remove_all(tempDir);

    REQUIRE_FALSE(ok);
    REQUIRE(err.find("duplicate styles.json files found") != std::string::npos);
}

TEST_CASE("UiLayoutJsonLoader: parses visible_if and state blocks with overrides") {
    const char* json = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      {
        "type": "text",
        "id": "mode",
        "visible_if": "track.selected.exists",
        "active": {
          "if": "track.selected.fx.enabled",
          "opacity": 1.0,
          "text_color": "#DDCCEE"
        },
        "inactive": {
          "if": "!track.selected.fx.enabled",
          "opacity": 0.4,
          "effects": [{ "type": "color_filter", "effect_color": "#808080", "effect_amount": 0.8 }]
        },
        "disabled": {
          "if": "!target.active",
          "opacity": 0.2
        },
        "opacity": 0.9
      },
      {
        "type": "knob",
        "id": "gain",
        "active": { "if": "track.selected.exists" },
        "inactive": { "if": "!track.selected.exists", "opacity": 0.5 }
      }
    ]
  }
}
)json";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutJsonLoader::loadFromString(json, tpl, err));
    REQUIRE(err.empty());
    REQUIRE(tpl.root.children.size() == 2);
    REQUIRE(tpl.root.children[0].visibleIf == "track.selected.exists");
    REQUIRE(tpl.root.children[0].active.ifExpr == "track.selected.fx.enabled");
    REQUIRE(tpl.root.children[0].inactive.ifExpr == "!track.selected.fx.enabled");
    REQUIRE(tpl.root.children[0].disabled.ifExpr == "!target.active");
    REQUIRE(tpl.root.children[0].opacity == Catch::Approx(0.9f));
    REQUIRE(tpl.root.children[0].active.opacity == Catch::Approx(1.0f));
    REQUIRE(tpl.root.children[0].inactive.opacity == Catch::Approx(0.4f));
    REQUIRE(tpl.root.children[0].disabled.opacity == Catch::Approx(0.2f));
    REQUIRE(tpl.root.children[0].active.textColor == "#DDCCEE");
    REQUIRE(tpl.root.children[0].inactive.effects.size() == 1);
    REQUIRE(tpl.root.children[0].inactive.effects[0].type == "color_filter");
    REQUIRE(tpl.root.children[1].active.ifExpr == "track.selected.exists");
    REQUIRE(tpl.root.children[1].inactive.ifExpr == "!track.selected.exists");
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

TEST_CASE("UiLayoutJsonLoader: rejects removed legacy state keys") {
    const char* json = R"json(
{
  "id": "tracks",
  "layout": {
    "type": "column",
    "children": [
      {
        "type": "text",
        "id": "title",
        "active_if": "track.selected.exists",
        "effects_inactive": [{ "type": "glow" }]
      }
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
