#include "service/ui/FxEditorWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace avantgarde {
namespace {

constexpr const char* kFrameTop = "╔";
constexpr const char* kFrameTopRight = "╗";
constexpr const char* kFrameMid = "╠";
constexpr const char* kFrameMidRight = "╣";
constexpr const char* kFrameBottom = "╚";
constexpr const char* kFrameBottomRight = "╝";
constexpr const char* kFrameVert = "║";
constexpr const char* kFrameH = "═";

std::string repeatToken(std::string_view token, std::size_t count) {
    std::string out;
    out.reserve(token.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += token;
    }
    return out;
}

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

FxEditorWidget::FxEditorWidget(uint16_t frameWidth, float paramStep) noexcept
    : frameWidth_(frameWidth),
      paramStep_(paramStep > 0.0f ? paramStep : 0.05f) {}

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

void FxEditorWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    const std::size_t width = frameWidth_ < 34 ? 34 : frameWidth_;
    const std::size_t inner = width - 2U;

    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;
    const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
    SlotCache* cache = descriptor ? &cacheFor_(track, fxSlot, *descriptor) : nullptr;

    out.clear();
    out.lines.reserve(14);

    out.lines.push_back(std::string(kFrameTop) + repeatToken(kFrameH, inner) + kFrameTopRight);
    {
        char title[192]{};
        std::snprintf(title,
                      sizeof(title),
                      " FX EDITOR T%u S%u (%s) ",
                      static_cast<unsigned>(track + 1U),
                      static_cast<unsigned>(fxSlot + 1U),
                      descriptor ? std::string(descriptor->displayName).c_str() : "No FX");
        out.lines.push_back(std::string(kFrameVert) + padRight(title, inner) + kFrameVert);
    }
    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);

    if (!descriptor || paramCount == 0U || !cache) {
        out.lines.push_back(std::string(kFrameVert) + padRight(" no fx params in current slot ", inner) + kFrameVert);
        for (std::size_t i = 0; i < 6; ++i) {
            out.lines.push_back(std::string(kFrameVert) + padRight(" ", inner) + kFrameVert);
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
            out.lines.push_back(std::string(kFrameVert) + padRight(line, inner) + kFrameVert);
        }
    }

    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);
    out.lines.push_back(std::string(kFrameVert) + padRight(buildActionStatusLine_(rtState, navState), inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameVert) + padRight(" keys [j/k focus] [/? adj] [o apply] [esc] ", inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameBottom) + repeatToken(kFrameH, inner) + kFrameBottomRight);
}

WidgetOutput FxEditorWidget::onInput(UiInputAction action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t fxSlot = clampFx_(navState.selectedFx, fxCount);
    const FxDescriptor* descriptor = resolveDescriptor_(rtState, track, fxSlot);
    const std::size_t paramCount = descriptor ? descriptor->paramCount : 0U;

    if (action == UiInputAction::ListUp && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = (current == 0U) ? static_cast<uint16_t>(paramCount - 1U)
                                          : static_cast<uint16_t>(current - 1U);
        out.handled = true;
        return out;
    }
    if (action == UiInputAction::ListDown && paramCount > 0U) {
        navState.cursor = static_cast<uint16_t>((clampParamIndex_(navState.cursor, paramCount) + 1U) % paramCount);
        out.handled = true;
        return out;
    }
    if (action == UiInputAction::ListParent) {
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
    if (action == UiInputAction::TrackSpeedUp && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = static_cast<uint16_t>((current + 1U) % paramCount);
        out.handled = true;
        return out;
    }
    if (action == UiInputAction::TrackSpeedDown && paramCount > 0U) {
        const uint16_t current = clampParamIndex_(navState.cursor, paramCount);
        navState.cursor = (current == 0U) ? static_cast<uint16_t>(paramCount - 1U)
                                          : static_cast<uint16_t>(current - 1U);
        out.handled = true;
        return out;
    }
    // '['/']' в текущем keymap приходят как BpmDown/BpmUp.
    if ((action == UiInputAction::BpmUp || action == UiInputAction::BpmDown) &&
        descriptor && paramCount > 0U) {
        const uint16_t selectedParam = clampParamIndex_(navState.cursor, paramCount);
        const FxParamDescriptor& def = descriptor->params[selectedParam];
        const float dir = (action == UiInputAction::BpmUp) ? 1.0f : -1.0f;
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
