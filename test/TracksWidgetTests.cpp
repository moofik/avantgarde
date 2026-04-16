#include <catch2/catch_all.hpp>
#include <algorithm>
#include <string>

#include "service/ui/UiWidgetFactory.h"
#include "service/ui/widgets/TracksWidget.h"
#include "platform/render/PreparedLayoutUtils.h"

using namespace avantgarde;

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
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::TrackContext);
    REQUIRE(out.intents[0].resetCursor);
    REQUIRE(out.intents[0].resetScroll);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}

TEST_CASE("TracksWidget: mode action cycles 4-profile SetTrackPlaybackProfile intent") {
    TracksWidget widget(TracksWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].playbackMode = UiTrackPlaybackMode::Looper;
    state.tracks[0].playbackProfile = UiTrackPlaybackProfile::Loop;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    const auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackPlaybackProfile;
    });
    REQUIRE(it != catalog.actions.end());
    REQUIRE(it->state.enabled);
    REQUIRE(it->state.value == Catch::Approx(2.0f));

    UiAction action = *it;
    action.op = UiAction::Op::Apply;
    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetTrackPlaybackProfile);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].value == Catch::Approx(3.0f));
}

TEST_CASE("TracksWidget: Track gain/speed actions moved to SampleEdit scene") {
    TracksWidget widget(TracksWidget::Options{});

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].gain01 = 0.50f;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    const auto gainIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackGain;
    });
    const auto speedIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackSpeed;
    });
    REQUIRE(gainIt == catalog.actions.end());
    REQUIRE(speedIt == catalog.actions.end());
}

TEST_CASE("TracksWidget: Track-scoped actions disabled when no tracks") {
    TracksWidget widget(TracksWidget::Options{});
    UiState state{};
    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    const UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    const auto profileIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneTrackPlaybackProfile;
    });
    REQUIRE(profileIt != catalog.actions.end());
    REQUIRE_FALSE(profileIt->state.enabled);
}

TEST_CASE("TracksWidget: FX icon visibility follows selected track FX enabled state") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Tracks);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxEnabled = {1U};

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));

    auto index = render::buildComponentIndex(prepared);
    auto it = index.find("fx_enabled_icon");
    REQUIRE(it != index.end());
    const auto* icon = dynamic_cast<const UiIconComponent*>(it->second);
    REQUIRE(icon != nullptr);
    CHECK(icon->isVisible());
    CHECK(icon->isActive());
    CHECK(icon->path == "images/icon.png");

    state.tracks[0].fxEnabled = {0U};
    UiPreparedLayout preparedOff{};
    REQUIRE(widget->buildPreparedLayout(preparedOff, state, nav));
    auto indexOff = render::buildComponentIndex(preparedOff);
    auto itOff = indexOff.find("fx_enabled_icon");
    REQUIRE(itOff != indexOff.end());
    const auto* iconOff = dynamic_cast<const UiIconComponent*>(itOff->second);
    REQUIRE(iconOff != nullptr);
    CHECK(iconOff->isVisible());
    CHECK_FALSE(iconOff->isActive());
    CHECK(iconOff->opacity() < icon->opacity());
}

TEST_CASE("TracksWidget: transport line shows global REC flag") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Tracks);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.transport.recordEnabled = true;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));
    auto index = render::buildComponentIndex(prepared);
    auto it = index.find("transport_line");
    REQUIRE(it != index.end());
    const auto* text = dynamic_cast<const UiTextComponent*>(it->second);
    REQUIRE(text != nullptr);
    CHECK(text->text.find("REC:Y") != std::string::npos);
}

TEST_CASE("TracksWidget: track anim playhead bind uses selected track playhead (not sequencer)") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Tracks);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.transport.tsNum = 4;
    state.transport.tsDen = 4;
    state.sequencer.playheadTick = 0; // намеренно 0, чтобы проверить источник именно из track.playhead01
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].playhead01 = 0.73f;

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));
    auto index = render::buildComponentIndex(prepared);
    auto it = index.find("track_anim");
    REQUIRE(it != index.end());
    const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(it->second);
    REQUIRE(anim != nullptr);
    CHECK(anim->intensity01 == Catch::Approx(0.73f));
}

TEST_CASE("TracksWidget: bars row contains current bar derived from track playhead") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Tracks);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].clipName = "loop.wav";
    state.tracks[0].bars = 4;
    state.tracks[0].playhead01 = 0.73f; // bar 3 из 4

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));
    auto index = render::buildComponentIndex(prepared);
    auto it = index.find("tracks_body");
    REQUIRE(it != index.end());
    const auto* list = dynamic_cast<const UiListComponent*>(it->second);
    REQUIRE(list != nullptr);

    bool found = false;
    for (const std::string& row : list->rows) {
        if (row.find("bars:4 current bar:3") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}
