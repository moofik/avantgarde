#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "service/ui/hud/HudNotificationsLayer.h"

using namespace avantgarde;

TEST_CASE("HudNotificationsLayer: snapshot events are queued and rendered") {
    HudNotificationsLayer hud{};
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    hud.notify(HudEventId::SnapshotCaptured, HudEventPayload{.slot = 2, .text = {}});
    const UiHudOverlayView first = hud.view(nowMs);
    CHECK(first.visible);
    CHECK(first.text.find("SNAPSHOT 2 CAPTURED") != std::string::npos);

    const UiHudOverlayView done = hud.view(nowMs + 1'000'000'000ULL);
    CHECK_FALSE(done.visible);
}

TEST_CASE("HudNotificationsLayer: loads hud.json and applies event template") {
    HudNotificationsLayer hud{};
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    std::string error{};
    const std::string path = std::string(AVANTGARDE_SOURCE_DIR) + "/assets/ui/hud.json";
    REQUIRE(hud.loadConfigFromFile(path, error));

    hud.notify(HudEventId::SnapshotApplied, HudEventPayload{.slot = 4, .text = {}});
    const UiHudOverlayView first = hud.view(nowMs);
    CHECK(first.visible);
    CHECK(first.text.find("SNAPSHOT 4 APPLIED") != std::string::npos);
    CHECK(first.font == "gothic");
    CHECK(first.fontSize == Catch::Approx(28.0f));
    CHECK(first.align == UiLayoutAlign::Center);
    CHECK(first.justify == UiLayoutJustify::Center);
    CHECK(first.textWrap);
    REQUIRE(first.textEffects.size() == 1U);
    CHECK(first.textEffects[0].type == "typing");
}

TEST_CASE("HudNotificationsLayer: info hud event resolves glow text animation") {
    HudNotificationsLayer hud{};
    std::string error{};
    const std::string path = std::string(AVANTGARDE_SOURCE_DIR) + "/assets/ui/hud.json";
    REQUIRE(hud.loadConfigFromFile(path, error));

    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    hud.notify(HudEventId::SnapshotCaptured, HudEventPayload{.slot = 2, .text = {}});
    const UiHudOverlayView view = hud.view(nowMs);
    REQUIRE(view.visible);
    CAPTURE(view.text);
    CAPTURE(view.font);
    CAPTURE(view.fontSize);
    CAPTURE(view.textEffects.size());
    REQUIRE_FALSE(view.textEffects.empty());
    CHECK(view.textEffects.front().type == "glow");
}

TEST_CASE("HudNotificationsLayer: resolves @theme tokens from sibling themes.json") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "avantgarde_hud_theme_tokens_test";
    std::error_code ec{};
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    {
        std::ofstream themes(dir / "themes.json");
        themes << R"({
  "themes": {
    "colors": { "text": { "primary": "#123456" } },
    "fonts": { "display": "gothic" },
    "sizes": { "font": { "hud": 19 } }
  }
})";
    }
    {
        std::ofstream hud(dir / "hud.json");
        hud << R"({
  "styles": {
    "hud.base": {
      "width": 24,
      "height": 4,
      "font": "@theme.fonts.display",
      "font_size": "@theme.sizes.font.hud",
      "text_color": "@theme.colors.text.primary"
    }
  },
  "types": {
    "info": { "style_ref": "@styles.hud.base" }
  },
  "events": {
    "snapshot.captured": { "type": "info", "text": "SNAPSHOT {slot} CAPTURED" }
  }
})";
    }

    HudNotificationsLayer hud{};
    std::string error{};
    const bool loaded = hud.loadConfigFromFile((dir / "hud.json").string(), error);
    CAPTURE(error);
    REQUIRE(loaded);

    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    hud.notify(HudEventId::SnapshotCaptured, HudEventPayload{.slot = 1, .text = {}});
    const UiHudOverlayView view = hud.view(nowMs);
    CHECK(view.visible);
    CHECK(view.font == "gothic");
    CHECK(view.fontSize == Catch::Approx(19.0f));
    CHECK(view.textColor == "#123456");
}
