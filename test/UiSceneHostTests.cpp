#include <catch2/catch_all.hpp>

#include "service/ui/UiSceneHost.h"

using namespace avantgarde;

namespace {

class FakeWidget final : public IUiWidget {
public:
    const char* id() const noexcept override { return "fake"; }

    void render(UiTextBuffer& out, const UiState&, const UiNavState& navState) override {
        out.lines.push_back(navState.selectedTrack == 0 ? "track0" : "track1");
    }

    WidgetOutput onInput(UiInputAction action, const UiState&, UiNavState& navState) override {
        if (action == UiInputAction::BpmUp) {
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
        if (action.op == UiAction::Op::Redo) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 100);
            return WidgetOutput{true, {}};
        }
        return {};
    }
};

} // namespace

TEST_CASE("UiSceneHost: registers widget and renders active scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);
    UiTextBuffer out{};
    REQUIRE(host.renderActive(out, state));
    REQUIRE(out.lines.size() == 1);
    REQUIRE(out.lines[0] == "track0");
}

TEST_CASE("UiSceneHost: handles global track navigation and delegates local input") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);

    const WidgetOutput prevOut = host.handleInput(UiInputAction::SelectPrevTrack, state);
    REQUIRE(prevOut.handled);
    REQUIRE(host.nav().selectedTrack == 1);

    const WidgetOutput nextOut = host.handleInput(UiInputAction::SelectNextTrack, state);
    REQUIRE(nextOut.handled);
    REQUIRE(host.nav().selectedTrack == 0);

    host.nav().trackPage = 0;
    const WidgetOutput pagePrevOut = host.handleInput(UiInputAction::TrackPagePrev, state);
    REQUIRE(pagePrevOut.handled);
    REQUIRE(host.nav().trackPage == 0);

    const WidgetOutput pageNextOut = host.handleInput(UiInputAction::TrackPageNext, state);
    REQUIRE(pageNextOut.handled);
    REQUIRE(host.nav().trackPage == 0);

    const WidgetOutput localOut = host.handleInput(UiInputAction::BpmUp, state);
    REQUIRE(localOut.handled);
    REQUIRE(host.nav().cursor == 1);
}

TEST_CASE("UiSceneHost: track page navigation wraps for multi-page track list") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(4);

    host.nav().trackPage = 0;
    const WidgetOutput pagePrevOut = host.handleInput(UiInputAction::TrackPagePrev, state);
    REQUIRE(pagePrevOut.handled);
    REQUIRE(host.nav().trackPage == 1);

    const WidgetOutput pageNextOut = host.handleInput(UiInputAction::TrackPageNext, state);
    REQUIRE(pageNextOut.handled);
    REQUIRE(host.nav().trackPage == 0);
}

TEST_CASE("UiSceneHost: pointer actions are routed to widget action handler") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    state.tracks.resize(2);

    const WidgetOutput out = host.handleInput(UiInputAction::ActionAdjustNext, state);
    REQUIRE(out.handled);
    REQUIRE(host.nav().cursor == 10);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetTransportBpm);

    const WidgetOutput outF = host.handleInput(UiInputAction::F8, state);
    REQUIRE(outF.handled);
    REQUIRE(host.nav().cursor == 20);

    const WidgetOutput focusOut = host.handleInput(UiInputAction::ListDown, state);
    REQUIRE(focusOut.handled);
    REQUIRE(host.nav().sceneActionIndex == 1);

    const WidgetOutput redoOut = host.handleInput(UiInputAction::F9, state);
    REQUIRE(redoOut.handled);
    REQUIRE(host.nav().cursor == 120);
}
