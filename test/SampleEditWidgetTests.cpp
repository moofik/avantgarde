#include <catch2/catch_all.hpp>
#include <algorithm>

#include "service/ui/widgets/SampleEditWidget.h"

using namespace avantgarde;

TEST_CASE("SampleEditWidget: catalog contains playback profile and trim actions") {
    SampleEditWidget widget(SampleEditWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].playbackProfile = UiTrackPlaybackProfile::Loop;
    state.tracks[0].trimStart01 = 0.10f;
    state.tracks[0].trimEnd01 = 0.90f;

    UiNavState nav{};
    nav.scene = UiScene::SampleEdit;
    nav.selectedTrack = 0;

    const UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    const auto profileIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackPlaybackProfile;
    });
    const auto startIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackTrimStart;
    });
    const auto endIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackTrimEnd;
    });

    REQUIRE(profileIt != catalog.actions.end());
    REQUIRE(startIt != catalog.actions.end());
    REQUIRE(endIt != catalog.actions.end());
    REQUIRE(profileIt->state.enabled);
    REQUIRE(startIt->state.enabled);
    REQUIRE(endIt->state.enabled);
}

TEST_CASE("SampleEditWidget: apply profile emits SetTrackPlaybackProfile intent") {
    SampleEditWidget widget(SampleEditWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].playbackProfile = UiTrackPlaybackProfile::Loop;

    UiNavState nav{};
    nav.scene = UiScene::SampleEdit;
    nav.selectedTrack = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackPlaybackProfile;
    });
    REQUIRE(it != catalog.actions.end());

    UiAction action = *it;
    action.op = UiAction::Op::Apply;
    const WidgetOutput out = widget.onAction(action, state, nav);

    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetTrackPlaybackProfile);
    REQUIRE(out.intents[0].track == 0);
    // LOOP(2) -> ONESHOT(3) при циклическом apply.
    REQUIRE(out.intents[0].value == Catch::Approx(3.0f));
}

TEST_CASE("SampleEditWidget: PreviewPlay toggles to PreviewStop when preview is active") {
    SampleEditWidget widget(SampleEditWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].clipPath = "/tmp/test.wav";
    state.tracks[0].clipName = "test.wav";
    state.tracks[0].stretchRatio = 1.0f;
    state.tracks[0].trimStart01 = 0.1f;
    state.tracks[0].trimEnd01 = 0.9f;

    UiNavState nav{};
    nav.scene = UiScene::SampleEdit;
    nav.selectedTrack = 0;
    state.transport.previewPlaying = false;

    {
        const WidgetOutput out = widget.onGesture(UiGesture::PreviewPlay, state, nav);
        REQUIRE(out.handled);
        REQUIRE(out.intents.size() == 1U);
        REQUIRE(out.intents[0].type == UiIntentType::PreviewRequest);
        REQUIRE(out.intents[0].path == "/tmp/test.wav");
    }

    state.transport.previewPlaying = true;
    {
        const WidgetOutput out = widget.onGesture(UiGesture::PreviewPlay, state, nav);
        REQUIRE(out.handled);
        REQUIRE(out.intents.size() == 1U);
        REQUIRE(out.intents[0].type == UiIntentType::PreviewStop);
    }
}

TEST_CASE("SampleEditWidget: tempo sync uses directional adjust semantics") {
    SampleEditWidget widget(SampleEditWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].tempoSync = false;

    UiNavState nav{};
    nav.scene = UiScene::SampleEdit;
    nav.selectedTrack = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackTempoSync;
    });
    REQUIRE(it != catalog.actions.end());

    {
        UiAction action = *it;
        action.op = UiAction::Op::AdjustNext;
        const WidgetOutput out = widget.onAction(action, state, nav);
        REQUIRE(out.handled);
        REQUIRE(out.intents.size() == 1U);
        REQUIRE(out.intents[0].type == UiIntentType::SetTrackTempoSync);
        REQUIRE(out.intents[0].value == Catch::Approx(1.0f));
    }

    state.tracks[0].tempoSync = true;
    catalog = widget.queryAvailableActions(state, nav);
    it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackTempoSync;
    });
    REQUIRE(it != catalog.actions.end());
    {
        UiAction action = *it;
        action.op = UiAction::Op::AdjustPrev;
        const WidgetOutput out = widget.onAction(action, state, nav);
        REQUIRE(out.handled);
        REQUIRE(out.intents.size() == 1U);
        REQUIRE(out.intents[0].type == UiIntentType::SetTrackTempoSync);
        REQUIRE(out.intents[0].value == Catch::Approx(0.0f));
    }
}
