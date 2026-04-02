#include <catch2/catch_all.hpp>

#include "service/ui/TracksWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"

using namespace avantgarde;

TEST_CASE("TracksWidget: applies TOML template title and keys hint") {
    const char* toml = R"(
id = "tracks"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
text = "MAIN TRACKS"
[[layout.children]]
type = "text"
id = "keys_hint"
text = " keys [TEST KEYS] "
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    TracksWidget widget(TracksWidget::Options{
        .frameWidth = 64,
        .headerTitle = "AVANTGARDE",
        .speedStep = 0.05f,
        .bpmStep = 1.0f,
        .layoutTemplate = tpl,
    });

    UiState state{};
    state.transport.bpm = 120.0f;
    state.transport.quant = QuantizeMode::Bar;
    state.tracks.resize(2);
    state.tracks[0].id = 0;
    state.tracks[0].state = UiTrackState::Stopped;
    state.tracks[0].clipName = "loop.wav";

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;
    nav.trackPage = 0;

    UiTextBuffer out{};
    widget.render(out, state, nav);
    REQUIRE_FALSE(out.lines.empty());

    bool hasCustomTitle = false;
    bool hasCustomKeys = false;
    for (const std::string& line : out.lines) {
        if (line.find("MAIN TRACKS") != std::string::npos) {
            hasCustomTitle = true;
        }
        if (line.find("TEST KEYS") != std::string::npos) {
            hasCustomKeys = true;
        }
    }
    REQUIRE(hasCustomTitle);
    REQUIRE(hasCustomKeys);
}

