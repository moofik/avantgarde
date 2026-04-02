#include "service/ui/FxEditorWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "service/ui/UiBindResolver.h"
#include "service/ui/layout/SceneFrameAsciiRenderer.h"

namespace avantgarde {
namespace {

std::string padRight(const std::string& s, std::size_t width) {
    if (s.size() >= width) {
        return s.substr(0, width);
    }
    std::string out = s;
    out.append(width - s.size(), ' ');
    return out;
}

std::string progressBar(float value01, std::size_t width) {
    const std::size_t safeWidth = width == 0U ? 1U : width;
    const float clamped = std::clamp(value01, 0.0f, 1.0f);
    const std::size_t filled = static_cast<std::size_t>(std::lround(clamped * static_cast<float>(safeWidth)));
    std::string out;
    out.reserve(safeWidth + 2U);
    out.push_back('[');
    for (std::size_t i = 0; i < safeWidth; ++i) {
        out.push_back(i < filled ? '#' : '.');
    }
    out.push_back(']');
    return out;
}

} // namespace

FxEditorWidget::FxEditorWidget(uint16_t frameWidth,
                               float paramStep,
                               std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : frameWidth_(frameWidth),
      paramStep_(paramStep > 0.0f ? paramStep : 0.05f) {
    if (layoutTemplate.has_value()) {
        layoutTemplate_ = layoutTemplate;
        buildLayoutModel_(*layoutTemplate);
    }
}

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
    if (fxSlot < tr.fxChainIds.size()) {
        return &FxRegistry::findOrFallback(tr.fxChainIds[fxSlot]);
    }
    // Backward-compat fallback: если у старого состояния нет fxChainIds,
    // считаем слот профилем реверба, чтобы редактор оставался рабочим.
    return &FxRegistry::findOrFallback(FxRegistry::kReverbSchroederId);
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
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 34U);
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
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
    const std::string titlePrefix = !layout_.title.empty() ? layout_.title : "FX EDITOR";
    std::snprintf(title,
                  sizeof(title),
                  " %s T%u S%u (%s) ",
                  titlePrefix.c_str(),
                  static_cast<unsigned>(track + 1U),
                  static_cast<unsigned>(fxSlot + 1U),
                  descriptor ? std::string(descriptor->displayName).c_str() : "No FX");

    const std::string keys = !layout_.keysHint.empty()
                                 ? layout_.keysHint
                                 : " keys [j/k focus] [/? adjust] [o apply] [esc back] ";

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("fx_editor")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(12U)
        .addComponent(UiStatusBarBuilder("header_title").text(title))
        .addComponent(UiSeparatorBuilder("sep_top").style(UiSeparatorComponent::Style::Heavy));

    const std::string viewId = layout_.viewNodeId.empty() ? std::string("fx_view") : layout_.viewNodeId;
    UiFxEditorViewBuilder view(viewId);

    if (!layout_.metaNodeId.empty()) {
        char meta[192]{};
        std::snprintf(meta,
                      sizeof(meta),
                      " fx:%s  params:%u  sel:%u ",
                      descriptor ? std::string(descriptor->displayName).c_str() : "none",
                      static_cast<unsigned>(paramCount),
                      static_cast<unsigned>(selectedParam + 1U));
        view.addToSlot(layout_.metaNodeId, UiTextBuilder(layout_.metaNodeId).text(meta));
    }

    if (descriptor && paramCount > 0U) {
        for (const LayoutKnob& knobCfg : layout_.knobs) {
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
    }

    if (layout_.anim.enabled && !layout_.anim.nodeId.empty()) {
        float intensity01 = 0.0f;
        if (descriptor && paramCount > 0U) {
            const uint16_t idx = clampParamIndex_(selectedParam, paramCount);
            const FxParamDescriptor& def = descriptor->params[idx];
            const float value = std::clamp(resolveValue(*descriptor, idx), def.minValue, def.maxValue);
            const float range = std::max(1e-6f, def.maxValue - def.minValue);
            intensity01 = std::clamp((value - def.minValue) / range, 0.0f, 1.0f);
        }
        view.addToSlot(layout_.anim.nodeId,
                       UiAnimSlotBuilder(layout_.anim.nodeId)
                           .label(layout_.anim.bindCanonical)
                           .animKey(layout_.anim.bindCanonical)
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

    const std::size_t width = frameWidth_ < 34 ? 34 : frameWidth_;
    const std::size_t inner = width - 2U;

    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;
    const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
    SlotCache* cache = descriptor ? &cacheFor_(track, fxSlot, *descriptor) : nullptr;

    SceneFrame frame{};
    frame.width = static_cast<uint16_t>(width);
    frame.height = 14;
    frame.rects.push_back(SceneFrameRect{
        .x = 0,
        .y = 0,
        .width = static_cast<uint16_t>(width),
        .height = frame.height,
    });

    int y = 1;
    {
        char title[192]{};
        const std::string titlePrefix = layout_.enabled ? layout_.title : "FX EDITOR";
        std::snprintf(title,
                      sizeof(title),
                      " %s T%u S%u (%s) ",
                      titlePrefix.c_str(),
                      static_cast<unsigned>(track + 1U),
                      static_cast<unsigned>(fxSlot + 1U),
                      descriptor ? std::string(descriptor->displayName).c_str() : "No FX");
        frame.texts.push_back(SceneFrameText{
            .x = 1,
            .y = static_cast<int16_t>(y++),
            .text = padRight(title, inner)});
    }
    frame.hlines.push_back(SceneFrameHLine{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .length = static_cast<uint16_t>(inner),
        .glyph = "═"});

    if (!descriptor || paramCount == 0U || !cache) {
        frame.texts.push_back(SceneFrameText{
            .x = 1,
            .y = static_cast<int16_t>(y++),
            .text = padRight(" no fx params in current slot ", inner)});
        for (std::size_t i = 0; i < 6; ++i) {
            frame.texts.push_back(SceneFrameText{
                .x = 1,
                .y = static_cast<int16_t>(y++),
                .text = padRight(" ", inner)});
        }
    } else {
        if (layout_.enabled && !layout_.knobs.empty()) {
            constexpr std::size_t kBarWidth = 10;
            constexpr std::size_t kRows = 7;
            std::size_t rendered = 0;
            for (const LayoutKnob& knob : layout_.knobs) {
                if (rendered >= kRows) {
                    break;
                }
                const uint16_t idx = resolveKnobParam_(knob, selectedParam, paramCount);
                const FxParamDescriptor& def = descriptor->params[idx];
                const float value = std::clamp(cache->values[idx], def.minValue, def.maxValue);
                const float range = std::max(1e-6f, def.maxValue - def.minValue);
                const float norm = (value - def.minValue) / range;
                const bool selected = (idx == selectedParam);

                const std::string label = !knob.label.empty() ? knob.label : std::string(def.label);
                char line[192]{};
                std::snprintf(line,
                              sizeof(line),
                              " %c %-10s %5.2f %s",
                              selected ? '>' : ' ',
                              label.c_str(),
                              value,
                              progressBar(norm, kBarWidth).c_str());
                frame.texts.push_back(SceneFrameText{
                    .x = 1,
                    .y = static_cast<int16_t>(y++),
                    .text = padRight(line, inner)});
                ++rendered;
            }

            if (layout_.anim.enabled && rendered < kRows) {
                char animLine[192]{};
                std::snprintf(animLine,
                              sizeof(animLine),
                              "   anim:%s %ux%u ",
                              layout_.anim.bindCanonical.c_str(),
                              static_cast<unsigned>(layout_.anim.width),
                              static_cast<unsigned>(layout_.anim.height));
                frame.texts.push_back(SceneFrameText{
                    .x = 1,
                    .y = static_cast<int16_t>(y++),
                    .text = padRight(animLine, inner)});
                ++rendered;
            }

            for (; rendered < kRows; ++rendered) {
                frame.texts.push_back(SceneFrameText{
                    .x = 1,
                    .y = static_cast<int16_t>(y++),
                    .text = padRight(" ", inner)});
            }
        } else {
            constexpr std::size_t kListRows = 7;
            constexpr std::size_t kBarWidth = 14;
            const std::size_t start = (paramCount > kListRows && selectedParam >= kListRows)
                                          ? static_cast<std::size_t>(selectedParam + 1U - kListRows)
                                          : 0U;

            for (std::size_t row = 0; row < kListRows; ++row) {
                const std::size_t idx = start + row;
                std::string line = " ";
                if (idx < paramCount) {
                    const bool selected = (idx == selectedParam);
                    const FxParamDescriptor& def = descriptor->params[idx];
                    const float value = std::clamp(cache->values[idx], def.minValue, def.maxValue);
                    const float range = std::max(1e-6f, def.maxValue - def.minValue);
                    const float norm = (value - def.minValue) / range;
                    char head[96]{};
                    std::snprintf(head,
                                  sizeof(head),
                                  "%c P%u %-8s %0.2f ",
                                  selected ? '>' : ' ',
                                  static_cast<unsigned>(idx + 1U),
                                  std::string(def.label).c_str(),
                                  value);
                    line += head;
                    line += progressBar(norm, kBarWidth);
                }
                frame.texts.push_back(SceneFrameText{
                    .x = 1,
                    .y = static_cast<int16_t>(y++),
                    .text = padRight(line, inner)});
            }
        }
    }

    frame.hlines.push_back(SceneFrameHLine{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .length = static_cast<uint16_t>(inner),
        .glyph = "═"});
    frame.texts.push_back(SceneFrameText{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .text = padRight(buildActionStatusLine_(rtState, navState), inner)});
    const std::string keysHint = (layout_.enabled && !layout_.keysHint.empty())
                                     ? layout_.keysHint
                                     : " keys [j/k focus] [/? adj] [o apply] [esc] ";
    frame.texts.push_back(SceneFrameText{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .text = padRight(keysHint, inner)});

    out.lines = SceneFrameAsciiRenderer::render(frame);
}

uint16_t FxEditorWidget::resolveKnobParam_(const LayoutKnob& knob,
                                           uint16_t selectedParam,
                                           std::size_t paramCount) const noexcept {
    if (paramCount == 0U) {
        return 0U;
    }
    if (knob.paramIndex < 0) {
        return clampParamIndex_(selectedParam, paramCount);
    }
    return clampParamIndex_(static_cast<uint16_t>(knob.paramIndex), paramCount);
}

void FxEditorWidget::collectNodes_(const UiLayoutNode& root,
                                   std::vector<const UiLayoutNode*>& out) noexcept {
    out.push_back(&root);
    for (const UiLayoutNode& child : root.children) {
        collectNodes_(child, out);
    }
}

void FxEditorWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "fx_editor") {
        return;
    }

    std::vector<const UiLayoutNode*> nodes{};
    collectNodes_(tpl.root, nodes);

    for (const UiLayoutNode* n : nodes) {
        if (!n) {
            continue;
        }
        if (n->type == UiLayoutNodeType::FxEditorView && !n->id.empty()) {
            layout_.viewNodeId = n->id;
        }
        if (n->type == UiLayoutNodeType::StatusBar && !n->text.empty()) {
            layout_.title = n->text;
        }
        if (n->type == UiLayoutNodeType::Text &&
            n->bind == "status.fx.meta" &&
            !n->id.empty()) {
            layout_.metaNodeId = n->id;
        }
        if (n->type == UiLayoutNodeType::Text &&
            n->id == "keys_hint" &&
            !n->text.empty()) {
            layout_.keysHint = n->text;
        }
        if (n->type == UiLayoutNodeType::Knob) {
            const UiBindResolution resolved = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob, n->bind);
            if (!resolved.ok) {
                continue;
            }
            LayoutKnob k{};
            k.nodeId = n->id;
            k.label = n->label;
            k.paramIndex = resolved.paramIndex;
            k.bindCanonical = resolved.canonical;
            layout_.knobs.push_back(std::move(k));
        }
        if (n->type == UiLayoutNodeType::AnimSlot && !layout_.anim.enabled) {
            const UiBindResolution resolved = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, n->bind);
            if (!resolved.ok) {
                continue;
            }
            layout_.anim.enabled = true;
            layout_.anim.nodeId = n->id.empty() ? "fx_anim" : n->id;
            layout_.anim.bindCanonical = resolved.canonical;
            if (n->width.unit == UiLayoutSize::Unit::Px && n->width.value > 0.0f) {
                layout_.anim.width = static_cast<uint16_t>(n->width.value);
            }
            if (n->height.unit == UiLayoutSize::Unit::Px && n->height.value > 0.0f) {
                layout_.anim.height = static_cast<uint16_t>(n->height.value);
            }
            if (layout_.anim.width == 0U) {
                layout_.anim.width = 128;
            }
            if (layout_.anim.height == 0U) {
                layout_.anim.height = 128;
            }
        }
    }

    if (!layout_.knobs.empty() || layout_.anim.enabled || !layout_.title.empty()) {
        layout_.enabled = true;
    }
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
