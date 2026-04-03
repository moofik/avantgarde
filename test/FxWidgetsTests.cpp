#include <catch2/catch_all.hpp>
#include <algorithm>
#include <unordered_map>

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
    state.tracks[0].fxChainIds.push_back("fx.reverb.schroeder");

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 0; // Wet

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto itValue = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneFxParamValue;
    });
    REQUIRE(itValue != catalog.actions.end());
    UiAction action = *itValue;
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

TEST_CASE("FxListWidget: FX On/Off action emits SetFxEnabled intent") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};
    state.tracks[0].fxEnabled = {1};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneFxEnabled;
    });
    REQUIRE(it != catalog.actions.end());
    UiAction action = *it;
    action.op = UiAction::Op::Apply;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::SetFxEnabled);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].fxSlot == 0);
    REQUIRE(out.intents[0].value == 0.0f);
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

TEST_CASE("FxListWidget: add fx from popup uses selected FX type") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 0;

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.selectedFxType = 2; // Stutter
    nav.fxAddPopupOpen = false;

    const WidgetOutput popupOut = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(popupOut.handled);
    REQUIRE(nav.fxAddPopupOpen);
    REQUIRE(popupOut.intents.empty());

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE_FALSE(nav.fxAddPopupOpen);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::AddFxToTrack);
    REQUIRE(out.intents[0].path == "fx.stutter.sync");
}

TEST_CASE("FxListWidget: FX type action cycles available types") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFxType = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneFxTypeSelect;
    });
    REQUIRE(it != catalog.actions.end());
    UiAction action = *it;
    action.op = UiAction::Op::AdjustNext;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.empty());
    REQUIRE(nav.selectedFxType == 1);
}

TEST_CASE("FxListWidget: Add FX action also cycles selected type on adjust") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFxType = 0;

    UiActionCatalog catalog = widget.queryAvailableActions(state, nav);
    auto it = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneAddFx;
    });
    REQUIRE(it != catalog.actions.end());
    UiAction action = *it;
    action.op = UiAction::Op::AdjustNext;

    const WidgetOutput out = widget.onAction(action, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.empty());
    REQUIRE(nav.selectedFxType == 1);
}

TEST_CASE("FxListWidget: enter on virtual empty slot opens add popup") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1; // виртуальный пустой слот = fxCount
    nav.fxAddPopupOpen = false;

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(nav.scene == UiScene::FxList);
    REQUIRE(nav.fxAddPopupOpen);
    REQUIRE(out.intents.empty());
}

TEST_CASE("FxListWidget: popup apply adds selected FX type and closes popup") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1;
    nav.selectedFxType = 2; // Stutter
    nav.fxAddPopupOpen = true;

    const WidgetOutput out = widget.onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE_FALSE(nav.fxAddPopupOpen);
    REQUIRE(out.intents.size() == 1);
    REQUIRE(out.intents[0].type == UiIntentType::AddFxToTrack);
    REQUIRE(out.intents[0].track == 0);
    REQUIRE(out.intents[0].path == "fx.stutter.sync");
}

TEST_CASE("FxListWidget: bypass/remove actions ignore virtual empty slot") {
    FxListWidget widget(60);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {"fx.reverb.schroeder"};
    state.tracks[0].fxEnabled = {1};

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 1; // виртуальный пустой слот

    UiAction bypass{};
    bypass.def.id = UiAction::Id::SceneFxEnabled;
    bypass.op = UiAction::Op::Apply;
    const WidgetOutput bypassOut = widget.onAction(bypass, state, nav);
    REQUIRE(bypassOut.handled);
    REQUIRE(bypassOut.intents.empty());

    UiAction remove{};
    remove.def.id = UiAction::Id::SceneFxRemove;
    remove.op = UiAction::Op::Apply;
    const WidgetOutput removeOut = widget.onAction(remove, state, nav);
    REQUIRE(removeOut.handled);
    REQUIRE(removeOut.intents.empty());
}

TEST_CASE("FxEditorWidget: uses TOML layout title and knobs when template provided") {
    const char* baseToml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
id = "header_title"
text = "FX TOML"
bind = "status.scene.title"
[[layout.children]]
type = "fx_editor_view"
id = "fx_view"
[[layout.children.children]]
type = "column"
id = "fx_body"
[[layout.children]]
type = "text"
id = "action_status"
bind = "status.action"
[[layout.children]]
type = "text"
id = "keys_hint"
bind = "status.keys"
)";
    const char* profileToml = R"(
id = "fx_profile_reverb"
[layout]
type = "column"
[[layout.children]]
type = "text"
id = "fx_meta"
bind = "status.fx.meta"
[[layout.children]]
type = "row"
[[layout.children.children]]
type = "knob"
id = "wet"
label = "WET"
bind = "scene.fx.param.value.0"
)";

    UiLayoutTemplate baseTpl{};
    UiLayoutTemplate profileTpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(baseToml, baseTpl, err));
    REQUIRE(UiLayoutTomlLoader::loadFromString(profileToml, profileTpl, err));

    std::unordered_map<std::string, UiLayoutTemplate> profiles{};
    profiles.emplace(std::string(FxRegistry::kReverbSchroederId), std::move(profileTpl));
    FxEditorWidget widget(60, 0.1f, std::move(baseTpl), std::move(profiles));
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

    bool hasTomlTitle = false;
    bool hasKnobLabel = false;
    UiPreparedLayout prepared{};
    REQUIRE(widget.buildPreparedLayout(prepared, state, nav));
    std::vector<const IUiComponent*> stack{};
    for (const auto& c : prepared.components) {
        if (c) {
            stack.push_back(c.get());
        }
    }
    while (!stack.empty()) {
        const IUiComponent* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        if (const auto* status = dynamic_cast<const UiStatusBarComponent*>(cur)) {
            if (status->text.find("FX TOML") != std::string::npos) {
                hasTomlTitle = true;
            }
        }
        if (const auto* knob = dynamic_cast<const UiKnobComponent*>(cur)) {
            if (knob->label.find("WET") != std::string::npos) {
                hasKnobLabel = true;
            }
        }
        auto pushSlots = [&stack](const auto* view) {
            if (!view) {
                return;
            }
            for (const UiComponentSlot& slot : view->slots) {
                for (const auto& nested : slot.components) {
                    if (nested) {
                        stack.push_back(nested.get());
                    }
                }
            }
        };
        if (cur->type() == UiComponentType::FxEditorView) {
            pushSlots(dynamic_cast<const UiFxEditorViewComponent*>(cur));
        }
    }
    REQUIRE(hasTomlTitle);
    REQUIRE(hasKnobLabel);
}

TEST_CASE("FxEditorWidget: knob label priority is template over descriptor") {
    const char* baseToml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
id = "header_title"
text = "FX LABEL TEST"
bind = "status.scene.title"
[[layout.children]]
type = "fx_editor_view"
id = "fx_view"
[[layout.children.children]]
type = "column"
id = "fx_body"
[[layout.children]]
type = "text"
id = "action_status"
bind = "status.action"
[[layout.children]]
type = "text"
id = "keys_hint"
bind = "status.keys"
)";
    const char* profileToml = R"(
id = "fx_profile_reverb"
[layout]
type = "column"
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

    UiLayoutTemplate baseTpl{};
    UiLayoutTemplate profileTpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(baseToml, baseTpl, err));
    REQUIRE(UiLayoutTomlLoader::loadFromString(profileToml, profileTpl, err));

    std::unordered_map<std::string, UiLayoutTemplate> profiles{};
    profiles.emplace(std::string(FxRegistry::kReverbSchroederId), std::move(profileTpl));
    FxEditorWidget widget(60, 0.1f, std::move(baseTpl), std::move(profiles));
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

    bool hasTemplateLabel = false;
    bool hasDescriptorFallbackLabel = false;
    UiPreparedLayout prepared{};
    REQUIRE(widget.buildPreparedLayout(prepared, state, nav));
    std::vector<const IUiComponent*> stack{};
    for (const auto& c : prepared.components) {
        if (c) {
            stack.push_back(c.get());
        }
    }
    while (!stack.empty()) {
        const IUiComponent* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        if (const auto* knob = dynamic_cast<const UiKnobComponent*>(cur)) {
            if (knob->label.find("CUSTOM WET") != std::string::npos) {
                hasTemplateLabel = true;
            }
            if (knob->label.find("Room") != std::string::npos) {
                hasDescriptorFallbackLabel = true;
            }
        }
        if (cur->type() == UiComponentType::FxEditorView) {
            const auto* view = dynamic_cast<const UiFxEditorViewComponent*>(cur);
            if (!view) {
                continue;
            }
            for (const UiComponentSlot& slot : view->slots) {
                for (const auto& nested : slot.components) {
                    if (nested) {
                        stack.push_back(nested.get());
                    }
                }
            }
        }
    }
    REQUIRE(hasTemplateLabel);
    REQUIRE(hasDescriptorFallbackLabel);
}

TEST_CASE("FxEditorWidget: strict mode throws when fx profile layout is missing") {
    const char* baseToml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
id = "header_title"
bind = "status.scene.title"
[[layout.children]]
type = "fx_editor_view"
id = "fx_view"
[[layout.children.children]]
type = "column"
id = "fx_body"
[[layout.children]]
type = "text"
id = "action_status"
bind = "status.action"
[[layout.children]]
type = "text"
id = "keys_hint"
bind = "status.keys"
)";

    UiLayoutTemplate baseTpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(baseToml, baseTpl, err));

    FxEditorWidget widget(60, 0.1f, std::move(baseTpl), {});
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

    UiPreparedLayout prepared{};
    REQUIRE_THROWS(widget.buildPreparedLayout(prepared, state, nav));
}

TEST_CASE("FxEditorWidget: builds switch component for discrete FX parameter from layout") {
    const char* baseToml = R"(
id = "fx_editor"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
id = "header_title"
text = "FX SWITCH TEST"
bind = "status.scene.title"
[[layout.children]]
type = "fx_editor_view"
id = "fx_view"
[[layout.children.children]]
type = "column"
id = "fx_body"
[[layout.children]]
type = "text"
id = "action_status"
bind = "status.action"
[[layout.children]]
type = "text"
id = "keys_hint"
bind = "status.keys"
)";
    const char* profileToml = R"(
id = "fx_profile_stutter"
[layout]
type = "column"
[[layout.children]]
type = "row"
gap = 1
[[layout.children.children]]
type = "switch"
id = "retrig_mode"
label = "RETRIG"
bind = "scene.fx.param.value.3"
options = ["OFF", "1", "2", "4", "8", "16"]
)";

    UiLayoutTemplate baseTpl{};
    UiLayoutTemplate profileTpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(baseToml, baseTpl, err));
    REQUIRE(UiLayoutTomlLoader::loadFromString(profileToml, profileTpl, err));

    std::unordered_map<std::string, UiLayoutTemplate> profiles{};
    profiles.emplace(std::string(FxRegistry::kStutterId), std::move(profileTpl));
    FxEditorWidget widget(60, 0.1f, std::move(baseTpl), std::move(profiles));

    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds = {std::string(FxRegistry::kStutterId)};

    UiNavState nav{};
    nav.scene = UiScene::FxEditor;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;
    nav.cursor = 3;

    UiPreparedLayout prepared{};
    REQUIRE(widget.buildPreparedLayout(prepared, state, nav));

    bool hasSwitch = false;
    std::vector<const IUiComponent*> stack{};
    for (const auto& c : prepared.components) {
        if (c) {
            stack.push_back(c.get());
        }
    }
    while (!stack.empty()) {
        const IUiComponent* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        if (const auto* sw = dynamic_cast<const UiSwitchComponent*>(cur)) {
            hasSwitch = true;
            REQUIRE(sw->label == "RETRIG");
            REQUIRE(sw->options.size() == 6);
            REQUIRE(sw->selected);
        }
        if (cur->type() == UiComponentType::FxEditorView) {
            const auto* view = dynamic_cast<const UiFxEditorViewComponent*>(cur);
            if (!view) {
                continue;
            }
            for (const UiComponentSlot& slot : view->slots) {
                for (const auto& nested : slot.components) {
                    if (nested) {
                        stack.push_back(nested.get());
                    }
                }
            }
        }
    }
    REQUIRE(hasSwitch);
}
