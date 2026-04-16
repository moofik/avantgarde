#include "service/ui/widgets/ContextMenuWidgetBase.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "service/ui/layout/UiNodeComponentComposer.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

ContextMenuWidgetBase::ContextMenuWidgetBase(std::string widgetId,
                                             UiScene scene,
                                             uint16_t frameWidth,
                                             std::string defaultTitle,
                                             std::string defaultKeysHint,
                                             std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : widgetId_(std::move(widgetId)),
      scene_(scene),
      frameWidth_(frameWidth) {
    layout_.title = std::move(defaultTitle);
    layout_.keysHint = std::move(defaultKeysHint);
    if (layoutTemplate.has_value()) {
        layoutTemplate_ = std::move(layoutTemplate);
        buildLayoutModel_(*layoutTemplate_);
    }
}

const char* ContextMenuWidgetBase::id() const noexcept {
    return widgetId_.c_str();
}

bool ContextMenuWidgetBase::buildPreparedLayout(UiPreparedLayout& out,
                                                const UiState& rtState,
                                                const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const uint16_t safeFrameWidth = std::max<uint16_t>(frameWidth_, 32U);
    std::vector<MenuItem> items = buildMenuItems_(rtState, navState);
    const std::size_t itemCount = items.size();
    const uint16_t selectedIndex = clampIndex_(navState.sceneActionIndex, itemCount);
    const MenuItem* selected = (itemCount > 0U) ? &items[selectedIndex] : nullptr;

    UiPreparedParams prepared{};
    prepared.text["status.scene.title"] = " " + layout_.title + " ";
    prepared.text["status.action"] =
        (selected != nullptr) ? selected->status : std::string(" action:- ");
    prepared.text["status.keys"] = layout_.keysHint;
    prepared.integer["menu_list.selectedRow"] = static_cast<int32_t>(selectedIndex);

    std::vector<std::string> rows{};
    rows.reserve(std::max<std::size_t>(itemCount, 1U));
    if (items.empty()) {
        rows.emplace_back("-");
    } else {
        for (const MenuItem& item : items) {
            rows.push_back(item.label);
        }
    }
    prepared.rows["menu_list"] = std::move(rows);

    UiPreparedLayoutBuilder builder{};
    builder.sceneId(widgetId_)
        .templateRef(&(*layoutTemplate_))
        .frameWidth(safeFrameWidth)
        .frameHeightHint(frameHeightHint_(itemCount));

    UiNodeComponentComposer::compose(scene_, *layoutTemplate_, rtState, navState, prepared, builder);

    out = std::move(builder).build();
    return true;
}

WidgetOutput ContextMenuWidgetBase::onGesture(UiGesture action,
                                              const UiState& rtState,
                                              UiNavState& navState) {
    const std::vector<MenuItem> items = buildMenuItems_(rtState, navState);
    const std::size_t count = items.size();

    switch (action) {
        case UiGesture::ListUp:
            navState.sceneActionIndex = wrapPrev_(navState.sceneActionIndex, count);
            navState.cursor = navState.sceneActionIndex;
            return WidgetOutput{true, {}};
        case UiGesture::ListDown:
            navState.sceneActionIndex = wrapNext_(navState.sceneActionIndex, count);
            navState.cursor = navState.sceneActionIndex;
            return WidgetOutput{true, {}};
        case UiGesture::ListEnter: {
            if (items.empty()) {
                return WidgetOutput{true, {}};
            }
            const uint16_t idx = clampIndex_(navState.sceneActionIndex, count);
            if (!items[idx].enabled) {
                return WidgetOutput{true, {}};
            }
            return applyMenuItem_(items[idx].actionId, rtState, navState);
        }
        default:
            return {};
    }
}

UiActionCatalog ContextMenuWidgetBase::queryAvailableActions(const UiState& rtState,
                                                             const UiNavState& navState) const {
    UiActionCatalog out{};
    const std::vector<MenuItem> items = buildMenuItems_(rtState, navState);
    if (items.empty()) {
        return out;
    }

    for (const MenuItem& item : items) {
        UiAction action{};
        action.def.id = item.actionId;
        action.def.scope = UiAction::Scope::Scene;
        action.def.execution = actionExecution_();
        action.def.valueKind = UiAction::ValueKind::None;
        action.def.label = item.label;
        action.state.enabled = item.enabled;
        out.actions.push_back(std::move(action));
    }

    out.currentIndex = clampIndex_(navState.sceneActionIndex, out.actions.size());
    for (std::size_t i = 0; i < out.actions.size(); ++i) {
        out.actions[i].state.selected = (i == out.currentIndex);
    }
    return out;
}

WidgetOutput ContextMenuWidgetBase::onAction(UiAction& action,
                                             const UiState& rtState,
                                             UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;
    if (action.op != UiAction::Op::Apply &&
        action.op != UiAction::Op::Press) {
        return out;
    }

    const std::vector<MenuItem> items = buildMenuItems_(rtState, navState);
    const auto it = std::find_if(items.begin(), items.end(), [&](const MenuItem& item) {
        return item.actionId == action.def.id;
    });
    if (it == items.end()) {
        out.handled = false;
        return out;
    }
    if (!it->enabled) {
        return out;
    }
    return applyMenuItem_(it->actionId, rtState, navState);
}

UiAction::Execution ContextMenuWidgetBase::actionExecution_() const noexcept {
    return UiAction::Execution::ApplyRequired;
}

uint16_t ContextMenuWidgetBase::frameHeightHint_(std::size_t) const noexcept {
    return 9U;
}

uint16_t ContextMenuWidgetBase::frameWidth() const noexcept {
    return frameWidth_;
}

uint16_t ContextMenuWidgetBase::clampIndex_(uint16_t index, std::size_t count) noexcept {
    if (count == 0U) {
        return 0U;
    }
    const uint16_t last = static_cast<uint16_t>(count - 1U);
    return (index > last) ? last : index;
}

uint16_t ContextMenuWidgetBase::wrapPrev_(uint16_t index, std::size_t count) noexcept {
    if (count == 0U) {
        return 0U;
    }
    const uint16_t safe = clampIndex_(index, count);
    return (safe == 0U) ? static_cast<uint16_t>(count - 1U) : static_cast<uint16_t>(safe - 1U);
}

uint16_t ContextMenuWidgetBase::wrapNext_(uint16_t index, std::size_t count) noexcept {
    if (count == 0U) {
        return 0U;
    }
    const uint16_t safe = clampIndex_(index, count);
    return static_cast<uint16_t>((safe + 1U) % count);
}

void ContextMenuWidgetBase::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    if (tpl.widgetId != widgetId_) {
        return;
    }
    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (node.type == UiLayoutNodeType::StatusBar &&
            !node.text.empty()) {
            layout_.title = node.text;
        }
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout_.keysHint = node.text;
        }
    });
    layout_.enabled = true;
}

} // namespace avantgarde

