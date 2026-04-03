#include "service/ui/FxEditorWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

#include "service/ui/UiBindResolver.h"
namespace avantgarde {
namespace {

bool fxEnabledForSlot(const UiTrackStateView& track, uint16_t fxSlot) noexcept {
    if (fxSlot < track.fxEnabled.size()) {
        return track.fxEnabled[fxSlot] != 0U;
    }
    return true;
}

}

FxEditorWidget::FxEditorWidget(uint16_t frameWidth,
                               float paramStep,
                               std::optional<UiLayoutTemplate> baseLayoutTemplate,
                               std::unordered_map<std::string, UiLayoutTemplate> profileLayoutTemplates) noexcept
    : frameWidth_(frameWidth),
      paramStep_(paramStep > 0.0f ? paramStep : 0.05f),
      baseLayoutTemplate_(std::move(baseLayoutTemplate)),
      profileLayoutTemplates_(std::move(profileLayoutTemplates)) {}

const char* FxEditorWidget::id() const noexcept {
    return "fx_editor";
}

uint8_t FxEditorWidget::clampTrack_(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    return (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
}

uint16_t FxEditorWidget::clampFx_(uint16_t fx, std::size_t fxCount) noexcept {
    if (fxCount == 0) {
        return 0;
    }
    return (fx >= fxCount) ? static_cast<uint16_t>(fxCount - 1U) : fx;
}

uint16_t FxEditorWidget::clampParamIndex_(uint16_t index, std::size_t paramCount) noexcept {
    if (paramCount == 0) {
        return 0;
    }
    return (index >= paramCount) ? static_cast<uint16_t>(paramCount - 1U) : index;
}

uint32_t FxEditorWidget::slotKey_(uint8_t track, uint16_t fxSlot) noexcept {
    return (static_cast<uint32_t>(track) << 16U) | static_cast<uint32_t>(fxSlot);
}

const FxDescriptor* FxEditorWidget::resolveDescriptor_(const UiState& rtState,
                                                       uint8_t track,
                                                       uint16_t fxSlot) noexcept {
    if (track >= rtState.tracks.size()) {
        return nullptr;
    }
    const UiTrackStateView& tr = rtState.tracks[track];
    if (fxSlot >= tr.fxCount) {
        return nullptr;
    }
    if (fxSlot >= tr.fxChainIds.size()) {
        return nullptr;
    }
    return FxRegistry::find(tr.fxChainIds[fxSlot]);
}

const std::string* FxEditorWidget::resolveFxId_(const UiState& rtState,
                                                uint8_t track,
                                                uint16_t fxSlot) noexcept {
    if (track >= rtState.tracks.size()) {
        return nullptr;
    }
    const UiTrackStateView& tr = rtState.tracks[track];
    if (fxSlot >= tr.fxCount || fxSlot >= tr.fxChainIds.size()) {
        return nullptr;
    }
    return &tr.fxChainIds[fxSlot];
}

FxEditorWidget::SlotCache& FxEditorWidget::cacheFor_(uint8_t track, uint16_t fxSlot, const FxDescriptor& descriptor) {
    const uint32_t key = slotKey_(track, fxSlot);
    SlotCache& cache = slotCache_[key];

    if (cache.fxId == descriptor.id && cache.values.size() == descriptor.paramCount) {
        return cache;
    }

    cache.fxId = std::string(descriptor.id);
    cache.values.assign(descriptor.paramCount, 0.0f);
    for (std::size_t i = 0; i < descriptor.paramCount; ++i) {
        cache.values[i] = descriptor.params[i].defaultValue;
    }
    return cache;
}

const FxEditorWidget::SlotCache* FxEditorWidget::cacheForConst_(uint8_t track,
                                                                 uint16_t fxSlot,
                                                                 const FxDescriptor& descriptor) const {
    const uint32_t key = slotKey_(track, fxSlot);
    auto it = slotCache_.find(key);
    if (it == slotCache_.end()) {
        return nullptr;
    }
    if (it->second.fxId != descriptor.id || it->second.values.size() != descriptor.paramCount) {
        return nullptr;
    }
    return &it->second;
}

bool FxEditorWidget::buildPreparedLayout(UiPreparedLayout& out,
                                         const UiState& rtState,
                                         const UiNavState& navState) const {
    if (!baseLayoutTemplate_.has_value()) {
        return false;
    }

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 34U);
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const std::string* fxId = resolveFxId_(rtState, track, fxSlot);
    if (!fxId || fxId->empty()) {
        throw std::runtime_error("FxEditorWidget: active FX slot does not have a valid fxId");
    }
    const UiLayoutTemplate* activeTemplate = resolveActiveLayoutTemplate_(*fxId);
    if (!activeTemplate) {
        throw std::runtime_error("FxEditorWidget: missing layout profile for fxId '" + *fxId + "'");
    }
    const LayoutModel layout = buildLayoutModel_(*activeTemplate);
    if (!layout.enabled) {
        throw std::runtime_error("FxEditorWidget: invalid layout model for fxId '" + *fxId + "'");
    }

    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    if (!descriptor) {
        throw std::runtime_error("FxEditorWidget: fxId '" + *fxId + "' is not registered in FxRegistry");
    }
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;
    const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
    const SlotCache* cache = descriptor ? cacheForConst_(track, fxSlot, *descriptor) : nullptr;

    auto resolveValue = [cache](const FxDescriptor& d, uint16_t idx) -> float {
        if (idx >= d.paramCount) {
            return 0.0f;
        }
        if (cache && idx < cache->values.size()) {
            return cache->values[idx];
        }
        return d.params[idx].defaultValue;
    };

    char title[192]{};
    const std::string titlePrefix = !layout.title.empty() ? layout.title : "FX EDITOR";
    std::snprintf(title,
                  sizeof(title),
                  " %s T%u S%u (%s) ",
                  titlePrefix.c_str(),
                  static_cast<unsigned>(track + 1U),
                  static_cast<unsigned>(fxSlot + 1U),
                  descriptor ? std::string(descriptor->displayName).c_str() : "No FX");

    const std::string keys = !layout.keysHint.empty()
                                 ? layout.keysHint
                                 : " keys [F5/F6 param] [F7/F8 value] [F1 bypass] [esc] ";

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("fx_editor")
        .templateRef(activeTemplate)
        .frameWidth(frameWidth)
        .frameHeightHint(12U)
        .addComponent(UiStatusBarBuilder("header_title").text(title))
        .addComponent(UiSeparatorBuilder("sep_top").style(UiSeparatorComponent::Style::Heavy));

    const std::string viewId = layout.viewNodeId.empty() ? std::string("fx_view") : layout.viewNodeId;
    UiFxEditorViewBuilder view(viewId);

    if (!layout.metaNodeId.empty()) {
        char meta[192]{};
        const bool fxEnabled = (track < rtState.tracks.size()) ? fxEnabledForSlot(rtState.tracks[track], fxSlot) : true;
        std::snprintf(meta,
                      sizeof(meta),
                      " fx:%s  state:%s  params:%u  sel:%u ",
                      descriptor ? std::string(descriptor->displayName).c_str() : "none",
                      fxEnabled ? "ON" : "OFF",
                      static_cast<unsigned>(paramCount),
                      static_cast<unsigned>(selectedParam + 1U));
        view.addToSlot(layout.metaNodeId, UiTextBuilder(layout.metaNodeId).text(meta));
    }

    if (descriptor && paramCount > 0U) {
        for (const LayoutKnob& knobCfg : layout.knobs) {
            if (knobCfg.nodeId.empty()) {
                continue;
            }
            const uint16_t idx = resolveKnobParam_(knobCfg, selectedParam, paramCount);
            const FxParamDescriptor& def = descriptor->params[idx];
            const float value = std::clamp(resolveValue(*descriptor, idx), def.minValue, def.maxValue);
            const float range = std::max(1e-6f, def.maxValue - def.minValue);
            const float value01 = std::clamp((value - def.minValue) / range, 0.0f, 1.0f);
            const std::string label = !knobCfg.label.empty() ? knobCfg.label : std::string(def.label);
            view.addToSlot(knobCfg.nodeId,
                           UiKnobBuilder(knobCfg.nodeId)
                               .label(label)
                               .value01(value01)
                               .selected(idx == selectedParam));
        }

        for (const LayoutSwitch& swCfg : layout.switches) {
            if (swCfg.nodeId.empty()) {
                continue;
            }
            const uint16_t idx = (swCfg.paramIndex < 0)
                                     ? clampParamIndex_(selectedParam, paramCount)
                                     : clampParamIndex_(static_cast<uint16_t>(swCfg.paramIndex), paramCount);
            const FxParamDescriptor& def = descriptor->params[idx];
            const float value = std::clamp(resolveValue(*descriptor, idx), def.minValue, def.maxValue);
            const float range = std::max(1e-6f, def.maxValue - def.minValue);
            const float value01 = std::clamp((value - def.minValue) / range, 0.0f, 1.0f);

            std::vector<std::string> options = swCfg.options;
            if (options.empty()) {
                options = {"OFF", "ON"};
            }
            uint16_t selectedIndex = 0U;
            if (options.size() > 1U) {
                const float scaled = value01 * static_cast<float>(options.size() - 1U);
                selectedIndex = static_cast<uint16_t>(std::lround(scaled));
                selectedIndex = std::min<uint16_t>(selectedIndex, static_cast<uint16_t>(options.size() - 1U));
            }

            const std::string label = !swCfg.label.empty() ? swCfg.label : std::string(def.label);
            view.addToSlot(swCfg.nodeId,
                           UiSwitchBuilder(swCfg.nodeId)
                               .label(label)
                               .options(std::move(options))
                               .selectedIndex(selectedIndex)
                               .selected(idx == selectedParam));
        }
    }

    if (layout.anim.enabled && !layout.anim.nodeId.empty()) {
        float intensity01 = 0.0f;
        if (descriptor && paramCount > 0U) {
            const uint16_t idx = clampParamIndex_(selectedParam, paramCount);
            const FxParamDescriptor& def = descriptor->params[idx];
            const float value = std::clamp(resolveValue(*descriptor, idx), def.minValue, def.maxValue);
            const float range = std::max(1e-6f, def.maxValue - def.minValue);
            intensity01 = std::clamp((value - def.minValue) / range, 0.0f, 1.0f);
        }
        view.addToSlot(layout.anim.nodeId,
                       UiAnimSlotBuilder(layout.anim.nodeId)
                           .label(layout.anim.bindCanonical)
                           .animKey(layout.anim.bindCanonical)
                           .intensity01(intensity01));
    }

    builder.addComponent(std::move(view));

    builder.addComponent(UiSeparatorBuilder("sep_bottom").style(UiSeparatorComponent::Style::Heavy))
        .addComponent(UiTextBuilder("action_status").text(buildActionStatusLine_(rtState, navState)))
        .addComponent(UiTextBuilder("keys_hint").text(keys));

    out = std::move(builder).build();
    return true;
}

void FxEditorWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    out.clear();
    // В strict-архитектуре виджет не рендерит сам:
    // финальная отрисовка выполняется на стороне host-пайплайна.
    UiPreparedLayout prepared{};
    if (!buildPreparedLayout(prepared, rtState, navState)) {
        return;
    }
    out.lines.push_back("[FxEditorWidget] legacy render path disabled; use UiSceneHost render pipeline");
}

uint16_t FxEditorWidget::resolveKnobParam_(const LayoutKnob& knob,
                                           uint16_t selectedParam,
                                           std::size_t paramCount) noexcept {
    if (paramCount == 0U) {
        return 0U;
    }
    if (knob.paramIndex < 0) {
        return clampParamIndex_(selectedParam, paramCount);
    }
    return clampParamIndex_(static_cast<uint16_t>(knob.paramIndex), paramCount);
}

FxEditorWidget::LayoutModel FxEditorWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    LayoutModel layout{};
    if (tpl.widgetId != "fx_editor") {
        return layout;
    }

    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (node.type == UiLayoutNodeType::FxEditorView && !node.id.empty()) {
            layout.viewNodeId = node.id;
        }
        if (node.type == UiLayoutNodeType::StatusBar && !node.text.empty()) {
            layout.title = node.text;
        }
        if (node.type == UiLayoutNodeType::Text &&
            node.bind == "status.fx.meta" &&
            !node.id.empty()) {
            layout.metaNodeId = node.id;
        }
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout.keysHint = node.text;
        }
        if (node.type == UiLayoutNodeType::Knob) {
            const UiBindResolution resolved = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob, node.bind);
            if (!resolved.ok) {
                return;
            }
            LayoutKnob k{};
            k.nodeId = node.id;
            k.label = node.label;
            k.paramIndex = resolved.paramIndex;
            k.bindCanonical = resolved.canonical;
            layout.knobs.push_back(std::move(k));
        }
        if (node.type == UiLayoutNodeType::Switch) {
            const UiBindResolution resolved = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Switch, node.bind);
            if (!resolved.ok) {
                return;
            }
            LayoutSwitch sw{};
            sw.nodeId = node.id;
            sw.label = node.label;
            sw.paramIndex = resolved.paramIndex;
            sw.bindCanonical = resolved.canonical;
            sw.options = node.options;
            layout.switches.push_back(std::move(sw));
        }
        if (node.type == UiLayoutNodeType::AnimSlot && !layout.anim.enabled) {
            const UiBindResolution resolved = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, node.bind);
            if (!resolved.ok) {
                return;
            }
            layout.anim.enabled = true;
            layout.anim.nodeId = node.id.empty() ? "fx_anim" : node.id;
            layout.anim.bindCanonical = resolved.canonical;
            if (node.width.unit == UiLayoutSize::Unit::Px && node.width.value > 0.0f) {
                layout.anim.width = static_cast<uint16_t>(node.width.value);
            }
            if (node.height.unit == UiLayoutSize::Unit::Px && node.height.value > 0.0f) {
                layout.anim.height = static_cast<uint16_t>(node.height.value);
            }
            if (layout.anim.width == 0U) {
                layout.anim.width = 128;
            }
            if (layout.anim.height == 0U) {
                layout.anim.height = 128;
            }
        }
    });

    if (!layout.knobs.empty() || !layout.switches.empty() || layout.anim.enabled || !layout.title.empty()) {
        layout.enabled = true;
    }
    return layout;
}

UiLayoutNode* FxEditorWidget::findNodeById_(UiLayoutNode& root, std::string_view id) noexcept {
    if (!id.empty() && root.id == id) {
        return &root;
    }
    for (UiLayoutNode& child : root.children) {
        if (UiLayoutNode* found = findNodeById_(child, id)) {
            return found;
        }
    }
    return nullptr;
}

const UiLayoutTemplate* FxEditorWidget::resolveActiveLayoutTemplate_(std::string_view fxId) const {
    if (!baseLayoutTemplate_.has_value() || fxId.empty()) {
        return nullptr;
    }

    const auto itCached = composedLayoutTemplates_.find(std::string(fxId));
    if (itCached != composedLayoutTemplates_.end()) {
        return &itCached->second;
    }

    const auto itProfile = profileLayoutTemplates_.find(std::string(fxId));
    if (itProfile == profileLayoutTemplates_.end()) {
        return nullptr;
    }

    UiLayoutTemplate composed = composeLayoutTemplate_(fxId, itProfile->second);
    const auto [itInserted, _] = composedLayoutTemplates_.emplace(std::string(fxId), std::move(composed));
    return &itInserted->second;
}

UiLayoutTemplate FxEditorWidget::composeLayoutTemplate_(std::string_view fxId,
                                                        const UiLayoutTemplate& profileTemplate) const {
    UiLayoutTemplate out = *baseLayoutTemplate_;
    UiLayoutNode* body = findNodeById_(out.root, "fx_body");
    if (!body) {
        throw std::runtime_error("FxEditorWidget: base layout does not contain node id='fx_body'");
    }
    body->children.clear();
    body->children.push_back(profileTemplate.root);
    if (profileTemplate.widgetId.empty()) {
        throw std::runtime_error("FxEditorWidget: profile layout has empty widget id for fxId '" + std::string(fxId) + "'");
    }
    return out;
}

WidgetOutput FxEditorWidget::onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;

    if (action == UiGesture::ListUp && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = (current == 0U) ? static_cast<uint16_t>(paramCount - 1U)
                                          : static_cast<uint16_t>(current - 1U);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListDown && paramCount > 0U) {
        navState.cursor = static_cast<uint16_t>((clampParamIndex_(navState.cursor, paramCount) + 1U) % paramCount);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListParent) {
        navState.scene = UiScene::FxList;
        navState.sceneActionIndex = 0;
        UiIntent it{};
        it.type = UiIntentType::Back;
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }
    // Обычные клавиши (не pointer-action):
    // '='/'-' в текущем keymap приходят как TrackSpeedUp/TrackSpeedDown.
    if (action == UiGesture::TrackSpeedUp && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = static_cast<uint16_t>((current + 1U) % paramCount);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::TrackSpeedDown && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = (current == 0U) ? static_cast<uint16_t>(paramCount - 1U)
                                          : static_cast<uint16_t>(current - 1U);
        out.handled = true;
        return out;
    }
    // '['/']' в текущем keymap приходят как BpmDown/BpmUp.
    if ((action == UiGesture::BpmUp || action == UiGesture::BpmDown) &&
        descriptor && paramCount > 0U) {
        const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
        const FxParamDescriptor& def = descriptor->params[selectedParam];
        const float dir = (action == UiGesture::BpmUp) ? 1.0f : -1.0f;
        SlotCache& cache = cacheFor_(track, fxSlot, *descriptor);
        const float next = std::clamp(cache.values[selectedParam] + dir * paramStep_, def.minValue, def.maxValue);
        cache.values[selectedParam] = next;

        UiIntent it{};
        it.type = UiIntentType::SetFxParam;
        it.track = track;
        it.fxSlot = static_cast<uint8_t>(fxSlot);
        it.paramIndex = def.paramIndex;
        it.value = next;
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }
    return {};
}

UiActionCatalog FxEditorWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};

    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;
    const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);

    float selectedValue = 0.0f;
    if (descriptor && paramCount > 0U) {
        selectedValue = descriptor->params[selectedParam].defaultValue;
        if (const SlotCache* cache = cacheForConst_(track, fxSlot, *descriptor)) {
            selectedValue = cache->values[selectedParam];
        }
    }

    auto push = [&out](UiAction a) {
        out.actions.push_back(std::move(a));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxEnabled;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "FX On/Off";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = 1.0f;
        a.state.enabled = (descriptor != nullptr);
        a.state.value = (descriptor && fxEnabledForSlot(rtState.tracks[track], fxSlot)) ? 1.0f : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxParamSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "FX Param";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, paramCount));
        a.def.step = 1.0f;
        a.state.enabled = (paramCount > 0U);
        a.state.value = static_cast<float>(selectedParam + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxParamValue;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "FX Value";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = paramStep_;
        a.state.enabled = (paramCount > 0U);
        a.state.value = selectedValue;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxBack;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Back To FX List";
        a.state.enabled = true;
        push(std::move(a));
    }

    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput FxEditorWidget::onAction(UiAction& action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;

    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;

    auto pushIntent = [&out](UiIntent intent) {
        out.intents.push_back(std::move(intent));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneFxEnabled: {
            if (fxCount == 0U) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const bool enabledNow = fxEnabledForSlot(rtState.tracks[track], fxSlot);
            UiIntent it{};
            it.type = UiIntentType::SetFxEnabled;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(fxSlot);
            it.value = enabledNow ? 0.0f : 1.0f;
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneFxParamSelect: {
            if (paramCount == 0U) {
                break;
            }
            const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.cursor = (current == 0U) ? static_cast<uint16_t>(paramCount - 1U)
                                                  : static_cast<uint16_t>(current - 1U);
            } else if (action.op == UiAction::Op::AdjustNext ||
                       action.op == UiAction::Op::Apply) {
                navState.cursor = static_cast<uint16_t>((current + 1U) % paramCount);
            }
        } break;

        case UiAction::Id::SceneFxParamValue: {
            if (!descriptor || paramCount == 0U) {
                break;
            }
            if (action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
            const FxParamDescriptor& def = descriptor->params[selectedParam];
            const float step = (action.def.step > 0.0f) ? action.def.step : paramStep_;
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;

            SlotCache& cache = cacheFor_(track, fxSlot, *descriptor);
            const float next = std::clamp(cache.values[selectedParam] + dir * step, def.minValue, def.maxValue);
            cache.values[selectedParam] = next;

            UiIntent it{};
            it.type = UiIntentType::SetFxParam;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(fxSlot);
            it.paramIndex = def.paramIndex;
            it.value = next;
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneFxBack: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            navState.scene = UiScene::FxList;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::Back;
            pushIntent(std::move(it));
        } break;

        default:
            out.handled = false;
            break;
    }

    return out;
}

std::string FxEditorWidget::buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const {
    const UiActionCatalog catalog = queryAvailableActions(rtState, navState);
    if (catalog.actions.empty()) {
        return " action:- ";
    }

    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;
    const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);

    const std::size_t idx = std::min<std::size_t>(catalog.currentIndex, catalog.actions.size() - 1U);
    const UiAction& a = catalog.actions[idx];
    const std::string selectedLabel =
        (descriptor && paramCount > 0U) ? std::string(descriptor->params[selectedParam].label) : "-";

    char buf[196]{};
    switch (a.def.id) {
        case UiAction::Id::SceneFxEnabled:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ",
                          a.def.label.c_str(),
                          (a.state.value >= 0.5f) ? "ON" : "OFF");
            break;
        case UiAction::Id::SceneFxParamSelect:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), selectedLabel.c_str());
            break;
        case UiAction::Id::SceneFxParamValue:
            std::snprintf(buf, sizeof(buf), " action:%s [%s] = %.2f ", a.def.label.c_str(), selectedLabel.c_str(), a.state.value);
            break;
        case UiAction::Id::SceneFxBack:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        default:
            std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
            break;
    }
    return std::string(buf);
}

} // namespace avantgarde
