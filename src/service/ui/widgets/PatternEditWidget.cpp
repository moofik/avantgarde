#include "service/ui/widgets/PatternEditWidget.h"

#include <algorithm>
#include <cstdio>

#include "service/ui/layout/UiNodeComponentComposer.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {
namespace {

constexpr uint32_t kMinPatternBars = 2U;
constexpr uint32_t kMaxPatternBars = 256U;

} // namespace

PatternEditWidget::PatternEditWidget() noexcept = default;

PatternEditWidget::PatternEditWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth) {
    if (options.layoutTemplate.has_value()) {
        layoutTemplate_ = options.layoutTemplate;
        buildLayoutModel_(*options.layoutTemplate);
    }
}

const char* PatternEditWidget::id() const noexcept {
    return "pattern_edit";
}

const char* PatternEditWidget::quantToStr_(SequencerQuantize q) noexcept {
    switch (q) {
        case SequencerQuantize::None: return "NONE";
        case SequencerQuantize::Sixteenth: return "1/16";
        case SequencerQuantize::Eighth: return "1/8";
        case SequencerQuantize::Quarter: return "1/4";
        case SequencerQuantize::Bar: return "BAR";
        default: return "UNK";
    }
}

std::string PatternEditWidget::buildActionStatusLine_(const UiState& rtState,
                                                      const UiNavState& navState) const {
    const UiActionCatalog actions = queryAvailableActions(rtState, navState);
    if (actions.actions.empty()) {
        return " action:- ";
    }
    const uint16_t idx = std::min<uint16_t>(actions.currentIndex, static_cast<uint16_t>(actions.actions.size() - 1U));
    const UiAction& a = actions.actions[idx];
    char buf[192]{};
    if (a.def.valueKind == UiAction::ValueKind::None) {
        std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), " action:%s = %.2f ", a.def.label.c_str(), a.state.value);
    }
    return buf;
}

bool PatternEditWidget::buildPreparedLayout(UiPreparedLayout& out,
                                            const UiState& rtState,
                                            const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const UiSequencerState& seq = rtState.sequencer;
    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 36U);

    UiPreparedParams prepared{};
    char title[128]{};
    const unsigned p = static_cast<unsigned>(seq.patternId == kInvalidPatternId ? 0U : seq.patternId);
    std::snprintf(title, sizeof(title), " %s %02u ", layout_.title.c_str(), p);
    prepared.text["status.scene.title"] = title;

    char meta[192]{};
    std::snprintf(meta, sizeof(meta), " REC:%s PLAY:%s ",
                  rtState.transport.recordEnabled ? "ON" : "OFF",
                  rtState.transport.playing ? "ON" : "OFF");
    prepared.text["pattern.meta"] = meta;

    char len[128]{};
    std::snprintf(len, sizeof(len), " LENGTH:%u BAR ", static_cast<unsigned>(seq.lengthBars));
    prepared.text["pattern.length"] = len;

    char quant[128]{};
    std::snprintf(quant, sizeof(quant), " QUANT:%s ", quantToStr_(seq.quant));
    prepared.text["pattern.quant"] = quant;

    prepared.text["pattern.loop_mode"] =
        std::string(" MODE:") + (seq.resetOnLoop ? "RESET ON LOOP " : "CONTINUE ");

    prepared.text["status.action"] = buildActionStatusLine_(rtState, navState);
    prepared.text["status.keys"] = layout_.keysHint;
    prepared.integer["frame.heightHint"] = 14;

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("pattern_edit")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(14);

    UiNodeComponentComposer::compose(UiScene::PatternEdit, *layoutTemplate_, rtState, navState, prepared, builder);
    out = std::move(builder).build();
    return true;
}

WidgetOutput PatternEditWidget::onGesture(UiGesture action, const UiState&, UiNavState& navState) {
    switch (action) {
        case UiGesture::BackScene:
        case UiGesture::ListParent: {
            UiIntent back{};
            back.type = UiIntentType::Back;
            back.scene = UiScene::Tracks;
            back.resetSceneActionIndex = true;
            return WidgetOutput{true, {back}};
        }
        case UiGesture::ListUp:
            navState.sceneActionIndex = (navState.sceneActionIndex == 0U) ? 2U : static_cast<uint16_t>(navState.sceneActionIndex - 1U);
            return WidgetOutput{true, {}};
        case UiGesture::ListDown:
            navState.sceneActionIndex = static_cast<uint16_t>((navState.sceneActionIndex + 1U) % 3U);
            return WidgetOutput{true, {}};
        default:
            return {};
    }
}

UiActionCatalog PatternEditWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};
    const UiSequencerState& seq = rtState.sequencer;

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerPatternLength;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "Pattern Length";
        a.def.minValue = static_cast<float>(kMinPatternBars);
        a.def.maxValue = static_cast<float>(kMaxPatternBars);
        a.def.step = 2.0f;
        a.state.enabled = true;
        a.state.value = static_cast<float>(std::clamp<uint32_t>(seq.lengthBars, kMinPatternBars, kMaxPatternBars));
        out.actions.push_back(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerQuant;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "Quant";
        a.def.minValue = 0.0f;
        a.def.maxValue = 4.0f;
        a.def.step = 1.0f;
        a.state.enabled = true;
        a.state.value = static_cast<float>(seq.quant);
        out.actions.push_back(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerLoopMode;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Mode";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = 1.0f;
        a.state.enabled = true;
        a.state.value = seq.resetOnLoop ? 1.0f : 0.0f;
        out.actions.push_back(std::move(a));
    }

    out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
    for (std::size_t i = 0; i < out.actions.size(); ++i) {
        out.actions[i].state.selected = (i == out.currentIndex);
    }
    return out;
}

WidgetOutput PatternEditWidget::onAction(UiAction& action,
                                         const UiState& rtState,
                                         UiNavState&) {
    WidgetOutput out{};
    out.handled = true;

    const UiSequencerState& seq = rtState.sequencer;
    auto push = [&out](UiIntent it) {
        out.intents.push_back(std::move(it));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneSequencerPatternLength: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            int next = static_cast<int>(std::clamp<uint32_t>(seq.lengthBars, kMinPatternBars, kMaxPatternBars));
            next += (action.op == UiAction::Op::AdjustPrev) ? -2 : 2;
            next = std::clamp(next, static_cast<int>(kMinPatternBars), static_cast<int>(kMaxPatternBars));
            UiIntent it{};
            it.type = UiIntentType::SequencerSetPatternLengthBars;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;
        case UiAction::Id::SceneSequencerQuant: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const int cur = std::clamp<int>(static_cast<int>(seq.quant), 0, 4);
            const int next = std::clamp<int>(cur + ((action.op == UiAction::Op::AdjustPrev) ? -1 : 1), 0, 4);
            UiIntent it{};
            it.type = UiIntentType::SequencerSetQuant;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;
        case UiAction::Id::SceneSequencerLoopMode: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerSetLoopMode;
            it.value = seq.resetOnLoop ? 0.0f : 1.0f;
            push(std::move(it));
        } break;
        default:
            out.handled = false;
            break;
    }
    return out;
}

void PatternEditWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (!tpl.widgetId.empty()) {
        layout_.title = tpl.widgetId;
    }
    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout_.keysHint = node.text;
        }
    });
    layout_.enabled = true;
}

} // namespace avantgarde

