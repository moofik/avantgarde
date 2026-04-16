#include <catch2/catch_all.hpp>

#include "service/ui/widgets/TrackContextMenuWidget.h"

using namespace avantgarde;

TEST_CASE("TrackContextMenuWidget: list navigation wraps between menu items") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.sceneActionIndex = 0;

    const WidgetOutput up = widget.onGesture(UiGesture::ListUp, state, nav);
    REQUIRE(up.handled);
    REQUIRE(nav.sceneActionIndex == 4);

    const WidgetOutput down = widget.onGesture(UiGesture::ListDown, state, nav);
    REQUIRE(down.handled);
    REQUIRE(nav.sceneActionIndex == 0);
}

TEST_CASE("TrackContextMenuWidget: apply CLEAR emits clear intent and returns to tracks") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 1;
    nav.sceneActionIndex = 0; // CLEAR

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 2);
    REQUIRE(out.intents[0].type == UiIntentType::ClearTrackSample);
    REQUIRE(out.intents[0].track == 1);
    REQUIRE(out.intents[1].type == UiIntentType::Back);
    REQUIRE(out.intents[1].scene == UiScene::Tracks);
    REQUIRE(out.intents[1].resetSceneActionIndex);
}

TEST_CASE("TrackContextMenuWidget: apply LOAD FX opens fx list scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 1; // LOAD FX

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::FxList);
    REQUIRE(out.intents[0].resetSelectedFx);
    REQUIRE(out.intents[0].closeFxAddPopup);
}

TEST_CASE("TrackContextMenuWidget: apply SAMPLE EDIT opens sample edit scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 2; // SAMPLE EDIT

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::SampleEdit);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}

TEST_CASE("TrackContextMenuWidget: apply SEQUENCER VIEW opens sequencer scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 3; // SEQUENCER VIEW

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::Sequencer);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}

TEST_CASE("TrackContextMenuWidget: apply PATTERN EDIT opens pattern edit scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 4; // PATTERN EDIT

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::PatternEdit);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}
