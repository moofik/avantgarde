#include <catch2/catch_all.hpp>

#include "service/ui/widgets/FxEditorWidget.h"
#include "service/ui/widgets/FxListWidget.h"
#include "service/ui/widgets/PatternEditWidget.h"
#include "service/ui/widgets/SequencerWidget.h"
#include "service/ui/widgets/TrackContextMenuWidget.h"
#include "service/ui/widgets/TracksWidget.h"
#include "service/ui/UiSceneHost.h"

using namespace avantgarde;

namespace {

const UiLayoutTemplate& fakeTemplate() {
    static const UiLayoutTemplate tpl = []() {
        UiLayoutTemplate t{};
        t.widgetId = "fake";
        t.root.type = UiLayoutNodeType::Column;
        UiLayoutNode line{};
        line.type = UiLayoutNodeType::Text;
        line.id = "main_line";
        line.width.unit = UiLayoutSize::Unit::Percent;
        line.width.value = 100.0f;
        t.root.children.push_back(std::move(line));
        return t;
    }();
    return tpl;
}

class FakeWidget final : public IUiWidget {
public:
    const char* id() const noexcept override { return "fake"; }

    bool buildPreparedLayout(UiPreparedLayout& out, const UiState&, const UiNavState& navState) const override {
        UiPreparedLayoutBuilder b{};
        b.sceneId("fake")
            .templateRef(&fakeTemplate())
            .frameWidth(32)
            .addComponent(UiTextBuilder("main_line").text(navState.selectedTrack == 0 ? "track0" : "track1"));
        out = std::move(b).build();
        return true;
    }

    WidgetOutput onGesture(UiGesture action, const UiState&, UiNavState& navState) override {
        if (action == UiGesture::PreviewPlay) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 1);
            return WidgetOutput{true, {}};
        }
        return {};
    }

    UiActionCatalog queryAvailableActions(const UiState&, const UiNavState& navState) const override {
        UiActionCatalog out{};
        UiAction a0{};
        a0.def.id = UiAction::Id::SceneTempoBpm;
        a0.def.scope = UiAction::Scope::Scene;
        a0.def.execution = UiAction::Execution::ImmediateContinuous;
        a0.def.valueKind = UiAction::ValueKind::Float;
        a0.def.label = "Tempo BPM";
        a0.def.step = 1.0f;
        a0.state.enabled = true;
        out.actions.push_back(a0);

        UiAction a1{};
        a1.def.id = UiAction::Id::SceneTrackMute;
        a1.def.scope = UiAction::Scope::Scene;
        a1.def.execution = UiAction::Execution::ImmediateStep;
        a1.def.valueKind = UiAction::ValueKind::Bool;
        a1.def.label = "Track Mute";
        a1.state.enabled = true;
        out.actions.push_back(a1);

        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, 1);
        return out;
    }

    WidgetOutput onAction(UiAction& action, const UiState&, UiNavState& navState) override {
        if (action.def.id == UiAction::Id::SceneTempoBpm &&
            action.op == UiAction::Op::AdjustNext) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 10);
            UiIntent it{};
            it.type = UiIntentType::SetTransportBpm;
            it.value = 121.0f;
            return WidgetOutput{true, {it}};
        }
        if (action.def.id == UiAction::Id::SceneDetectProjectBpm &&
            action.op == UiAction::Op::Apply) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 1000);
            UiIntent it{};
            it.type = UiIntentType::DetectProjectBpmFromTrack;
            it.track = navState.selectedTrack;
            return WidgetOutput{true, {it}};
        }
        if (action.op == UiAction::Op::Redo) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 100);
            return WidgetOutput{true, {}};
        }
        return {};
    }
};

class FakePreparedWidget final : public IUiWidget {
public:
    const char* id() const noexcept override { return "fake_prepared"; }

    bool buildPreparedLayout(UiPreparedLayout& out, const UiState&, const UiNavState&) const override {
        UiPreparedLayoutBuilder b{};
        b.sceneId("fake_prepared")
            .templateRef(&fakeTemplate())
            .frameWidth(32)
            .addComponent(UiTextBuilder("main_line").text("PREPARED"));
        out = std::move(b).build();
        return true;
    }

    WidgetOutput onGesture(UiGesture, const UiState&, UiNavState&) override { return {}; }

};

} // namespace

TEST_CASE("UiSceneHost: registers widget and builds prepared layout for active scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);
    UiPreparedLayout prepared{};
    REQUIRE(host.buildPreparedActive(prepared, state));
    REQUIRE(prepared.layoutTemplate != nullptr);
    REQUIRE_FALSE(prepared.components.empty());
}

TEST_CASE("UiSceneHost: prefers prepared layout path from widget") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakePreparedWidget>()));

    UiState state{};
    UiPreparedLayout prepared{};
    REQUIRE(host.buildPreparedActive(prepared, state));
    REQUIRE(prepared.layoutTemplate != nullptr);
    REQUIRE_FALSE(prepared.components.empty());
}

TEST_CASE("UiSceneHost: handles global track navigation and delegates local input") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);

    const WidgetOutput prevOut = host.handleGesture(UiGesture::SelectPrevTrack, state);
    REQUIRE(prevOut.handled);
    REQUIRE(host.nav().selectedTrack == 1);

    const WidgetOutput nextOut = host.handleGesture(UiGesture::SelectNextTrack, state);
    REQUIRE(nextOut.handled);
    REQUIRE(host.nav().selectedTrack == 0);

    host.nav().trackPage = 0;
    const WidgetOutput pagePrevOut = host.handleGesture(UiGesture::TrackPagePrev, state);
    REQUIRE(pagePrevOut.handled);
    REQUIRE(host.nav().trackPage == 1);
    REQUIRE(pagePrevOut.intents.size() == 1);
    REQUIRE(pagePrevOut.intents[0].type == UiIntentType::SetActiveTrack);
    REQUIRE(pagePrevOut.intents[0].track == 1);

    const WidgetOutput pageNextOut = host.handleGesture(UiGesture::TrackPageNext, state);
    REQUIRE(pageNextOut.handled);
    REQUIRE(host.nav().selectedTrack == 0);
    REQUIRE(host.nav().trackPage == 0);
    REQUIRE(pageNextOut.intents.size() == 1);
    REQUIRE(pageNextOut.intents[0].type == UiIntentType::SetActiveTrack);
    REQUIRE(pageNextOut.intents[0].track == 0);

    const WidgetOutput localOut = host.handleGesture(UiGesture::PreviewPlay, state);
    REQUIRE(localOut.handled);
    REQUIRE(host.nav().cursor == 1);
}

TEST_CASE("UiSceneHost: track page navigation wraps for multi-page track list") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(4);

    host.nav().trackPage = 0;
    const WidgetOutput pagePrevOut = host.handleGesture(UiGesture::TrackPagePrev, state);
    REQUIRE(pagePrevOut.handled);
    REQUIRE(host.nav().selectedTrack == 3);
    REQUIRE(host.nav().trackPage == 3);
    REQUIRE(pagePrevOut.intents.size() == 1);
    REQUIRE(pagePrevOut.intents[0].type == UiIntentType::SetActiveTrack);
    REQUIRE(pagePrevOut.intents[0].track == 3);

    const WidgetOutput pageNextOut = host.handleGesture(UiGesture::TrackPageNext, state);
    REQUIRE(pageNextOut.handled);
    REQUIRE(host.nav().selectedTrack == 0);
    REQUIRE(host.nav().trackPage == 0);
    REQUIRE(pageNextOut.intents.size() == 1);
    REQUIRE(pageNextOut.intents[0].type == UiIntentType::SetActiveTrack);
    REQUIRE(pageNextOut.intents[0].track == 0);
}

TEST_CASE("UiSceneHost: pointer actions are routed to widget action handler") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);

    const WidgetOutput out = host.handleGesture(UiGesture::ActionAdjustNext, state);
    REQUIRE(out.handled);
    REQUIRE(host.nav().cursor == 10);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetTransportBpm);

    const WidgetOutput outF = host.handleGesture(UiGesture::F8, state);
    REQUIRE(outF.handled);
    REQUIRE(host.nav().cursor == 20);

    const WidgetOutput focusOut = host.handleGesture(UiGesture::ListDown, state);
    REQUIRE(focusOut.handled);
    REQUIRE(host.nav().sceneActionIndex == 1);

    const WidgetOutput redoOut = host.handleGesture(UiGesture::F9, state);
    REQUIRE(redoOut.handled);
    REQUIRE(host.nav().cursor == 120);
}

TEST_CASE("UiSceneHost: global pointer scope uses host global catalog") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(4);
    state.transport.playing = false;

    host.nav().actionScope = UiAction::Scope::Global;
    host.nav().globalActionIndex = 0;
    host.nav().sceneActionIndex = 1;
    host.nav().cursor = 7;

    // В глобальном scope ListDown двигает global pointer (в Tracks-сцене -> FocusNext).
    const WidgetOutput focusOut = host.handleGesture(UiGesture::ListDown, state);
    REQUIRE(focusOut.handled);
    REQUIRE(host.nav().globalActionIndex == 1);
    REQUIRE(host.nav().sceneActionIndex == 1);

    // Возвращаемся на первый глобальный экшен: Transport Play.
    host.nav().globalActionIndex = 0;
    const WidgetOutput playOut = host.handleGesture(UiGesture::ActionApply, state);
    REQUIRE(playOut.handled);
    REQUIRE(playOut.intents.size() == 1);
    REQUIRE(playOut.intents[0].type == UiIntentType::SetTransportPlaying);
    REQUIRE(playOut.intents[0].value == 1.0f);
    // Важно: глобальный путь не должен дергать widget->onAction.
    REQUIRE(host.nav().cursor == 7);

    // Глобальный Track Page Prev.
    host.nav().trackPage = 0;
    host.nav().globalActionIndex = 2;
    const WidgetOutput pageOut = host.handleGesture(UiGesture::ActionApply, state);
    REQUIRE(pageOut.handled);
    REQUIRE(host.nav().selectedTrack == 3);
    REQUIRE(host.nav().trackPage == 3);
    REQUIRE(pageOut.intents.size() == 1);
    REQUIRE(pageOut.intents[0].type == UiIntentType::SetActiveTrack);
    REQUIRE(pageOut.intents[0].track == 3);
}

TEST_CASE("UiSceneHost: F10 triggers detect BPM quick action on tracks scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);
    host.nav().scene = UiScene::Tracks;
    host.nav().selectedTrack = 1;
    host.nav().cursor = 5;

    const WidgetOutput out = host.handleGesture(UiGesture::F10, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::DetectProjectBpmFromTrack);
    REQUIRE(out.intents[0].track == 1);
    REQUIRE(host.nav().cursor == 1005);
}

TEST_CASE("UiSceneHost: M-toggle emits SetMetronomeEnabled intent") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(1);
    state.transport.metronomeEnabled = false;
    host.nav().scene = UiScene::Tracks;

    const WidgetOutput onOut = host.handleGesture(UiGesture::ToggleMetronome, state);
    REQUIRE(onOut.handled);
    REQUIRE(onOut.intents.size() == 1);
    REQUIRE(onOut.intents[0].type == UiIntentType::SetMetronomeEnabled);
    REQUIRE(onOut.intents[0].value == Catch::Approx(1.0f));

    state.transport.metronomeEnabled = true;
    const WidgetOutput offOut = host.handleGesture(UiGesture::ToggleMetronome, state);
    REQUIRE(offOut.handled);
    REQUIRE(offOut.intents.size() == 1);
    REQUIRE(offOut.intents[0].type == UiIntentType::SetMetronomeEnabled);
    REQUIRE(offOut.intents[0].value == Catch::Approx(0.0f));
}

TEST_CASE("UiSceneHost: FxList fast F-keys route to slot nav and quick actions") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));
    REQUIRE(host.registerWidget(UiScene::FxList, std::make_unique<FxListWidget>(60)));

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};
    state.tracks[0].fxEnabled = {1};

    host.setScene(UiScene::FxList);
    host.nav().selectedTrack = 0;
    host.nav().selectedFx = 1; // виртуальный пустой слот
    host.nav().fxAddPopupOpen = false;

    const WidgetOutput openPopup = host.handleGesture(UiGesture::F1, state);
    REQUIRE(openPopup.handled);
    REQUIRE(host.nav().fxAddPopupOpen);
    REQUIRE(openPopup.intents.empty());

    const uint16_t prevType = host.nav().selectedFxType;
    const WidgetOutput typeNext = host.handleGesture(UiGesture::F6, state);
    REQUIRE(typeNext.handled);
    REQUIRE(host.nav().selectedFxType != prevType);

    const WidgetOutput closePopup = host.handleGesture(UiGesture::BackScene, state);
    REQUIRE(closePopup.handled);
    REQUIRE_FALSE(host.nav().fxAddPopupOpen);
    REQUIRE(host.scene() == UiScene::FxList);

    const WidgetOutput slotUp = host.handleGesture(UiGesture::F5, state);
    REQUIRE(slotUp.handled);
    REQUIRE(host.nav().selectedFx == 0);

    const WidgetOutput bypass = host.handleGesture(UiGesture::F7, state);
    REQUIRE(bypass.handled);
    REQUIRE(bypass.intents.size() == 1);
    REQUIRE(bypass.intents[0].type == UiIntentType::SetFxEnabled);

    const WidgetOutput remove = host.handleGesture(UiGesture::F8, state);
    REQUIRE(remove.handled);
    REQUIRE(remove.intents.size() == 1);
    REQUIRE(remove.intents[0].type == UiIntentType::RemoveFxFromTrack);
}

TEST_CASE("UiSceneHost: FxEditor fast F-keys route to select/value/bypass") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));
    REQUIRE(host.registerWidget(UiScene::FxEditor, std::make_unique<FxEditorWidget>(60, 0.1f)));

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};
    state.tracks[0].fxEnabled = {1};

    host.setScene(UiScene::FxEditor);
    host.nav().selectedTrack = 0;
    host.nav().selectedFx = 0;
    host.nav().cursor = 0;

    const WidgetOutput prevParam = host.handleGesture(UiGesture::F5, state);
    REQUIRE(prevParam.handled);
    REQUIRE(host.nav().cursor == 3);

    const WidgetOutput nextParam = host.handleGesture(UiGesture::F6, state);
    REQUIRE(nextParam.handled);
    REQUIRE(host.nav().cursor == 0);

    const WidgetOutput decValue = host.handleGesture(UiGesture::F7, state);
    REQUIRE(decValue.handled);
    REQUIRE(decValue.intents.size() == 1);
    REQUIRE(decValue.intents[0].type == UiIntentType::SetFxParam);

    const WidgetOutput bypass = host.handleGesture(UiGesture::F1, state);
    REQUIRE(bypass.handled);
    REQUIRE(bypass.intents.size() == 1);
    REQUIRE(bypass.intents[0].type == UiIntentType::SetFxEnabled);
}

TEST_CASE("UiSceneHost: apply on Track Select opens TrackContext scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<TracksWidget>(TracksWidget::Options{})));
    REQUIRE(host.registerWidget(UiScene::TrackContext, std::make_unique<TrackContextMenuWidget>(60)));

    UiState state{};
    state.tracks.resize(2);
    state.tracks[0].id = 0;
    state.tracks[1].id = 1;
    host.setScene(UiScene::Tracks);
    host.nav().selectedTrack = 0;
    host.nav().sceneActionIndex = 0; // SceneTrackSelect по умолчанию.

    const WidgetOutput out = host.handleGesture(UiGesture::F1, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::TrackContext);
    REQUIRE(out.intents[0].resetCursor);
    REQUIRE(out.intents[0].resetScroll);
    REQUIRE(out.intents[0].resetSceneActionIndex);
}

TEST_CASE("UiSceneHost: D emits delete intent in SequencerLane") {
    UiSceneHost host;
    SequencerWidget::Options laneOptions{};
    laneOptions.mode = SequencerWidget::Mode::Lane;
    REQUIRE(host.registerWidget(UiScene::SequencerLane, std::make_unique<SequencerWidget>(laneOptions)));

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    state.sequencer.points.resize(1);
    state.sequencer.points[0].value = 0.5f;
    state.sequencer.points[0].tick = 120;
    host.setScene(UiScene::SequencerLane);

    const WidgetOutput out = host.handleGesture(UiGesture::DeleteObject, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SequencerDeleteSelectedObject);
}

TEST_CASE("UiSceneHost: D emits delete-lane intent in Sequencer list view") {
    UiSceneHost host;
    SequencerWidget::Options listOptions{};
    listOptions.mode = SequencerWidget::Mode::List;
    REQUIRE(host.registerWidget(UiScene::Sequencer, std::make_unique<SequencerWidget>(listOptions)));

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    host.setScene(UiScene::Sequencer);

    const WidgetOutput out = host.handleGesture(UiGesture::DeleteObject, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SequencerDeleteSelectedLane);
}

TEST_CASE("UiSceneHost: F1 deletes when selected scene action is Delete in SequencerLane") {
    UiSceneHost host;
    SequencerWidget::Options laneOptions{};
    laneOptions.mode = SequencerWidget::Mode::Lane;
    REQUIRE(host.registerWidget(UiScene::SequencerLane, std::make_unique<SequencerWidget>(laneOptions)));

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    state.sequencer.points.resize(1);
    state.sequencer.points[0].value = 0.5f;
    state.sequencer.points[0].tick = 120;
    host.setScene(UiScene::SequencerLane);

    SequencerWidget probe(laneOptions);
    UiNavState probeNav = host.nav();
    const UiActionCatalog catalog = probe.queryAvailableActions(state, probeNav);
    int deleteIdx = -1;
    for (std::size_t i = 0; i < catalog.actions.size(); ++i) {
        if (catalog.actions[i].def.id == UiAction::Id::SceneSequencerDeleteObject) {
            deleteIdx = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(deleteIdx >= 0);
    host.nav().sceneActionIndex = static_cast<uint16_t>(deleteIdx);

    const WidgetOutput out = host.handleGesture(UiGesture::F1, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SequencerDeleteSelectedObject);
}

TEST_CASE("UiSceneHost: F1 deletes lane when selected scene action is Delete in Sequencer list") {
    UiSceneHost host;
    SequencerWidget::Options listOptions{};
    listOptions.mode = SequencerWidget::Mode::List;
    REQUIRE(host.registerWidget(UiScene::Sequencer, std::make_unique<SequencerWidget>(listOptions)));

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    host.setScene(UiScene::Sequencer);

    SequencerWidget probe(listOptions);
    UiNavState probeNav = host.nav();
    const UiActionCatalog catalog = probe.queryAvailableActions(state, probeNav);
    int deleteIdx = -1;
    for (std::size_t i = 0; i < catalog.actions.size(); ++i) {
        if (catalog.actions[i].def.id == UiAction::Id::SceneSequencerDeleteObject) {
            deleteIdx = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(deleteIdx >= 0);
    host.nav().sceneActionIndex = static_cast<uint16_t>(deleteIdx);

    const WidgetOutput out = host.handleGesture(UiGesture::F1, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SequencerDeleteSelectedLane);
}

TEST_CASE("UiSceneHost: Back from SequencerLane goes to Tracks") {
    UiSceneHost host;
    SequencerWidget::Options laneOptions{};
    laneOptions.mode = SequencerWidget::Mode::Lane;
    REQUIRE(host.registerWidget(UiScene::SequencerLane, std::make_unique<SequencerWidget>(laneOptions)));

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    host.setScene(UiScene::SequencerLane);

    const WidgetOutput out = host.handleGesture(UiGesture::BackScene, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::Back);
    REQUIRE(out.intents[0].scene == UiScene::Tracks);
}

TEST_CASE("UiSceneHost: Shift+N opens PatternEdit scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::PatternEdit, std::make_unique<PatternEditWidget>()));

    UiState state{};
    host.setScene(UiScene::Tracks);
    const WidgetOutput out = host.handleGesture(UiGesture::OpenPatternEdit, state);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenScene);
    REQUIRE(out.intents[0].scene == UiScene::PatternEdit);
}

TEST_CASE("UiSceneHost: Play/Stop hotkeys are ignored in SampleEdit scene") {
    UiSceneHost host;
    UiState state{};
    state.tracks.resize(1);
    host.nav().scene = UiScene::SampleEdit;

    const WidgetOutput playOut = host.handleGesture(UiGesture::PlayActiveTrack, state);
    REQUIRE(playOut.handled);
    REQUIRE(playOut.intents.empty());

    const WidgetOutput stopOut = host.handleGesture(UiGesture::StopActiveTrack, state);
    REQUIRE(stopOut.handled);
    REQUIRE(stopOut.intents.empty());
}
