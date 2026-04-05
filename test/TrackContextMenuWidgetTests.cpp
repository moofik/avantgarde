#include <catch2/catch_all.hpp>

#include "service/ui/TrackContextMenuWidget.h"

using namespace avantgarde;

TEST_CASE("TrackContextMenuWidget: list navigation wraps between three items") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.sceneActionIndex = 0;

    const WidgetOutput up = widget.onGesture(UiGesture::ListUp, state, nav);
    REQUIRE(up.handled);
    REQUIRE(nav.sceneActionIndex == 2);

    const WidgetOutput down = widget.onGesture(UiGesture::ListDown, state, nav);
    REQUIRE(down.handled);
    REQUIRE(nav.sceneActionIndex == 0);
}

TEST_CASE("TrackContextMenuWidget: apply LOAD SAMPLE opens manager scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 1;
    nav.sceneActionIndex = 0; // LOAD SAMPLE

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::Manager);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
}

TEST_CASE("TrackContextMenuWidget: apply CLEAR emits clear intent and returns to tracks") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 1;
    nav.sceneActionIndex = 1; // CLEAR

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::Tracks);
    REQUIRE(out.intents.size() == 2);
    REQUIRE(out.intents[0].type == UiIntentType::ClearTrackSample);
    REQUIRE(out.intents[0].track == 1);
    REQUIRE(out.intents[1].type == UiIntentType::Back);
}

TEST_CASE("TrackContextMenuWidget: apply LOAD FX opens fx list scene") {
    TrackContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);

    UiNavState nav{};
    nav.scene = UiScene::TrackContext;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 2; // LOAD FX

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::FxList);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
}
