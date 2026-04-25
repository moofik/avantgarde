#include <catch2/catch_all.hpp>

#include <unordered_map>

#include "contracts/FxRegistry.h"
#include "service/ui/widgets/FxEditorWidget.h"

using namespace avantgarde;

namespace {

UiLayoutTemplate makeFxEditorBaseTemplate() {
    UiLayoutTemplate tpl{};
    tpl.widgetId = "fx_editor";
    tpl.root.type = UiLayoutNodeType::Column;
    tpl.root.id = "root";

    UiLayoutNode body{};
    body.type = UiLayoutNodeType::Column;
    body.id = "fx_body";
    tpl.root.children.push_back(std::move(body));
    return tpl;
}

UiLayoutTemplate makeFxProfileTemplateWithRemappedTarget() {
    UiLayoutTemplate tpl{};
    tpl.widgetId = "fx_profile_test";
    tpl.root.type = UiLayoutNodeType::Column;
    tpl.root.id = "profile_root";

    UiLayoutNode knob{};
    knob.type = UiLayoutNodeType::Knob;
    knob.id = "k_wet";
    knob.bind = "fx.selected.param.0";
    knob.target = "param.fx.selected.1";
    tpl.root.children.push_back(std::move(knob));
    return tpl;
}

UiLayoutTemplate makeFxProfileTemplateWithSparseParams() {
    UiLayoutTemplate tpl{};
    tpl.widgetId = "fx_profile_sparse";
    tpl.root.type = UiLayoutNodeType::Column;
    tpl.root.id = "profile_root_sparse";

    UiLayoutNode knobA{};
    knobA.type = UiLayoutNodeType::Knob;
    knobA.id = "k_mix";
    knobA.bind = "fx.selected.param.0";
    knobA.target = "param.fx.selected.0";
    tpl.root.children.push_back(std::move(knobA));

    UiLayoutNode knobB{};
    knobB.type = UiLayoutNodeType::Knob;
    knobB.id = "k_retrig";
    knobB.bind = "fx.selected.param.6";
    knobB.target = "param.fx.selected.6";
    tpl.root.children.push_back(std::move(knobB));
    return tpl;
}

UiState makeFxEditorState() {
    UiState state{};
    UiTrackStateView track{};
    track.id = 0;
    track.fxCount = 1;
    track.fxChainIds.push_back(std::string(FxRegistry::kStutterId));
    track.fxEnabled.push_back(1U);
    track.clipName = "clip";
    state.tracks.push_back(std::move(track));
    return state;
}

} // namespace

TEST_CASE("FxEditorWidget: target write-path overrides bind read-path for SetFxParam") {
    std::unordered_map<std::string, UiLayoutTemplate> profiles{};
    profiles.emplace(std::string(FxRegistry::kStutterId), makeFxProfileTemplateWithRemappedTarget());

    FxEditorWidget widget{
        60,
        0.10f,
        std::optional<UiLayoutTemplate>{makeFxEditorBaseTemplate()},
        std::move(profiles),
    };

    UiState state = makeFxEditorState();
    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0; // Выбран первый read-параметр.

    UiAction action{};
    action.def.id = UiAction::Id::SceneFxParamValue;
    action.def.step = 0.10f;
    action.op = UiAction::Op::AdjustNext;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1U);
    REQUIRE(out.intents[0].type == UiIntentType::SetFxParam);
    REQUIRE(out.intents[0].fxSlot == 0U);
    // target=param.fx.selected.1 => пишем второй параметр (Rate), а не первый (Wet).
    REQUIRE(out.intents[0].paramIndex == toParamIndex(StutterParamId::Rate));
    REQUIRE(out.intents[0].value == Catch::Approx(0.75f));
}

TEST_CASE("FxEditorWidget: parameter selector follows visible controls from layout") {
    std::unordered_map<std::string, UiLayoutTemplate> profiles{};
    profiles.emplace(std::string(FxRegistry::kBufferFxId), makeFxProfileTemplateWithSparseParams());

    FxEditorWidget widget{
        60,
        0.10f,
        std::optional<UiLayoutTemplate>{makeFxEditorBaseTemplate()},
        std::move(profiles),
    };

    UiState state{};
    UiTrackStateView track{};
    track.id = 0;
    track.fxCount = 1;
    track.fxChainIds.push_back(std::string(FxRegistry::kBufferFxId));
    track.fxEnabled.push_back(1U);
    track.clipName = "clip";
    state.tracks.push_back(std::move(track));

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0;

    const UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    REQUIRE(catalog.actions.size() >= 2U);
    REQUIRE(catalog.actions[1].def.id == UiAction::Id::SceneFxParamSelect);
    REQUIRE(catalog.actions[1].def.maxValue == Catch::Approx(2.0f));

    WidgetOutput step1 = widget.onGesture(UiGesture::TrackSpeedUp, state, nav);
    REQUIRE(step1.handled);
    REQUIRE(nav.cursor == 1U);

    WidgetOutput step2 = widget.onGesture(UiGesture::TrackSpeedUp, state, nav);
    REQUIRE(step2.handled);
    REQUIRE(nav.cursor == 0U);
}
