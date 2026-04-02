#include <catch2/catch_all.hpp>
#include <algorithm>

#include "contracts/ids.h"
#include "service/ui/FxEditorWidget.h"
#include "service/ui/FxListWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"

using namespace avantgarde;

TEST_CASE("FxListWidget: enter opens fx editor for selected slot") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 2;

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1;

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::FxEditor);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenFxEditor);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].fxSlot == 1);
}

TEST_CASE("FxEditorWidget: adjust value emits SetFxParam intent") {
    FxEditorWidget widget(60, 0.1f);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0; // Wet

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    REQUIRE(catalog.actions.size() >= 2);
    UiAction action = catalog.actions[1];
    action.op = UiAction::Op::AdjustNext;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetFxParam);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].fxSlot == 0);
    REQUIRE(out.intents[0].paramIndex == toParamIndex(ReverbParamId::Wet));
    REQUIRE(out.intents[0].value > 0.25f);
}

TEST_CASE("FxEditorWidget: regular keyboard keys adjust selected param") {
    FxEditorWidget widget(60, 0.1f);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds.push_back("fx.reverb.schroeder");

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0;

    const WidgetOutput out = widget.onGesture(UiGesture::BpmUp, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetFxParam);
    REQUIRE(out.intents[0].paramIndex == toParamIndex(ReverbParamId::Wet));
    REQUIRE(out.intents[0].value > 0.25f);
}

TEST_CASE("FxListWidget: pointer apply on FX Slot opens editor for current slot") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 2;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder", "fx.reverb.schroeder"};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1;
    nav.sceneActionIndex = 0; // SceneFxSlotSelect

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    REQUIRE_FALSE(catalog.actions.empty());
    UiAction action = catalog.actions[0];
    REQUIRE(action.def.id == UiAction::Id::SceneFxSlotSelect);
    action.op = UiAction::Op::Apply;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::FxEditor);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::OpenFxEditor);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].fxSlot == 1);
}

TEST_CASE("FxListWidget: remove action emits RemoveFxFromTrack intent") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 2;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder", "fx.reverb.schroeder"};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneFxRemove;
    });
    REQUIRE(it != catalog.actions.end());
    UiAction action = *it;
    action.op = UiAction::Op::Apply;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::RemoveFxFromTrack);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].fxSlot == 1);
    REQUIRE(nav.selectedFx == 0);
}

TEST_CASE("FxEditorWidget: uses TOML layout title and knobs when template provided") {
    const char* toml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
text = "FX TOML"
[[layout.children]]
type = "row"
[[layout.children.children]]
type = "knob"
label = "WET"
bind = "scene.fx.param.value.0"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    FxEditorWidget widget(60, 0.1f, tpl);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds.push_back("fx.reverb.schroeder");

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0;

    UiTextBuffer out{};
    widget.render(out, state, nav);
    REQUIRE_FALSE(out.lines.empty());

    bool hasTomlTitle = false;
    bool hasKnobLabel = false;
    for (const std::string& line : out.lines) {
        if (line.find("FX TOML") != std::string::npos) {
            hasTomlTitle = true;
        }
        if (line.find("WET") != std::string::npos) {
            hasKnobLabel = true;
        }
    }
    REQUIRE(hasTomlTitle);
    REQUIRE(hasKnobLabel);
}

TEST_CASE("FxEditorWidget: knob label priority is template over descriptor") {
    const char* toml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
text = "FX LABEL TEST"
[[layout.children]]
type = "row"
gap = 1
[[layout.children.children]]
type = "knob"
id = "wet_custom"
label = "CUSTOM WET"
bind = "scene.fx.param.value.0"
[[layout.children.children]]
type = "knob"
id = "room_auto"
bind = "scene.fx.param.value.1"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    FxEditorWidget widget(60, 0.1f, tpl);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds.push_back("fx.reverb.schroeder");

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0;

    UiTextBuffer out{};
    widget.render(out, state, nav);
    REQUIRE_FALSE(out.lines.empty());

    bool hasTemplateLabel = false;
    bool hasDescriptorFallbackLabel = false;
    for (const std::string& line : out.lines) {
        if (line.find("CUSTOM WET") != std::string::npos) {
            hasTemplateLabel = true;
        }
        if (line.find("Room") != std::string::npos) {
            hasDescriptorFallbackLabel = true;
        }
    }
    REQUIRE(hasTemplateLabel);
    REQUIRE(hasDescriptorFallbackLabel);
}
