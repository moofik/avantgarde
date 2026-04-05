#include <catch2/catch_all.hpp>
#include <algorithm>

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

TEST_CASE("TracksWidget: Detect BPM action emits intent for selected track") {
    TracksWidget widget(TracksWidget::Options{});

    UiState state{};
    state.tracks.resize(2);
    state.tracks[0].id = 0;
    state.tracks[0].clipName = "loop.wav";
    state.tracks[0].clipPath = "/tmp/loop.wav";
    state.tracks[0].stretchRatio = 1.0f;
    state.tracks[1].id = 1;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneDetectProjectBpm;
    });
    REQUIRE(it != catalog.actions.end());
    REQUIRE(it->state.enabled);

    UiAction action = *it;
    action.op = UiAction::Op::Apply;
    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::DetectProjectBpmFromTrack);
    REQUIRE(out.intents[0].track == 0);
}

TEST_CASE("TracksWidget: apply on Track Select opens TrackContext") {
    TracksWidget widget(TracksWidget::Options{});

    UiState state{};
    state.tracks.resize(2);
    state.tracks[0].id = 0;
    state.tracks[1].id = 1;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    REQUIRE_FALSE(catalog.actions.empty());
    REQUIRE(catalog.actions[0].def.id == UiAction::Id::SceneTrackSelect);

    UiAction action = catalog.actions[0];
    action.op = UiAction::Op::Apply;
    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::TrackContext);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
}
