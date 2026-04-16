#include <catch2/catch_all.hpp>

#include "service/ui/widgets/SampleWidgetContextMenuWidget.h"

using namespace avantgarde;

TEST_CASE("SampleWidgetContextMenuWidget: apply PREVIEW emits preview intent with trim/speed") {
    SampleWidgetContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].clipPath = "/tmp/test.wav";
    state.tracks[0].stretchRatio = 1.25f;
    state.tracks[0].trimStart01 = 0.2f;
    state.tracks[0].trimEnd01 = 0.8f;

    UiNavState nav{};
    nav.scene = UiScene::SampleContextMenu;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 0; // PREVIEW SAMPLE

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::PreviewRequest);
    REQUIRE(out.intents[0].path == "/tmp/test.wav");
    REQUIRE(out.intents[0].previewSpeed == Catch::Approx(1.25f));
    REQUIRE(out.intents[0].previewStart01 == Catch::Approx(0.2f));
    REQUIRE(out.intents[0].previewEnd01 == Catch::Approx(0.8f));
}

TEST_CASE("SampleWidgetContextMenuWidget: apply LOAD SAMPLE opens manager scene") {
    SampleWidgetContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);

    UiNavState nav{};
    nav.scene = UiScene::SampleContextMenu;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 1; // LOAD SAMPLE

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::Manager);
    REQUIRE(out.intents[0].resetCursor);
    REQUIRE(out.intents[0].resetScroll);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}

TEST_CASE("SampleWidgetContextMenuWidget: apply DETECT BPM emits detect intent for selected track") {
    SampleWidgetContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(2);
    state.tracks[1].clipPath = "/tmp/test2.wav";

    UiNavState nav{};
    nav.scene = UiScene::SampleContextMenu;
    nav.selectedTrack = 1;
    nav.sceneActionIndex = 2; // DETECT BPM

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::DetectProjectBpmFromTrack);
    REQUIRE(out.intents[0].track == 1);
}

TEST_CASE("SampleWidgetContextMenuWidget: apply PREVIEW emits PreviewStop when preview is active") {
    SampleWidgetContextMenuWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].clipPath = "/tmp/test.wav";

    UiNavState nav{};
    nav.scene = UiScene::SampleContextMenu;
    nav.selectedTrack = 0;
    nav.sceneActionIndex = 0; // PREVIEW SAMPLE
    state.transport.previewPlaying = true;

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1U);
    REQUIRE(out.intents[0].type == UiIntentType::PreviewStop);
}
