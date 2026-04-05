#include "platform/render/PreparedLayoutUtils.h"

#include <algorithm>

namespace avantgarde::render {

namespace {

void collectSlots(const std::vector<UiComponentSlot>& slots, UiComponentIndex& out) {
    for (const UiComponentSlot& slot : slots) {
        for (const auto& child : slot.components) {
            collectComponentsById(child.get(), out);
        }
    }
}

} // namespace

void collectComponentsById(const IUiComponent* component, UiComponentIndex& out) {
    if (!component) {
        return;
    }
    if (!component->id().empty()) {
        out[std::string(component->id())] = component;
    }

    switch (component->type()) {
        case UiComponentType::TrackView: {
            const auto* view = dynamic_cast<const UiTrackViewComponent*>(component);
            if (view) {
                collectSlots(view->slots, out);
            }
        } break;
        case UiComponentType::ManagerView: {
            const auto* view = dynamic_cast<const UiManagerViewComponent*>(component);
            if (view) {
                collectSlots(view->slots, out);
            }
        } break;
        case UiComponentType::FxListView: {
            const auto* view = dynamic_cast<const UiFxListViewComponent*>(component);
            if (view) {
                collectSlots(view->slots, out);
            }
        } break;
        case UiComponentType::FxEditorView: {
            const auto* view = dynamic_cast<const UiFxEditorViewComponent*>(component);
            if (view) {
                collectSlots(view->slots, out);
            }
        } break;
        case UiComponentType::StatusBar:
        case UiComponentType::Text:
        case UiComponentType::Knob:
        case UiComponentType::Switch:
        case UiComponentType::AnimSlot:
        case UiComponentType::List:
        case UiComponentType::Separator:
        default:
            break;
    }
}

UiComponentIndex buildComponentIndex(const UiPreparedLayout& prepared) {
    UiComponentIndex out{};
    out.reserve(prepared.components.size());
    for (const auto& component : prepared.components) {
        collectComponentsById(component.get(), out);
    }
    return out;
}

uint16_t estimateInnerHeight(const UiPreparedLayout& prepared,
                             uint16_t minInnerHeight,
                             uint16_t baseRows) {
    if (prepared.frameHeightHint > 0U) {
        return prepared.frameHeightHint;
    }

    std::size_t maxRows = 0U;
    for (const auto& ptr : prepared.components) {
        if (!ptr || ptr->type() != UiComponentType::List) {
            continue;
        }
        const auto* list = dynamic_cast<const UiListComponent*>(ptr.get());
        if (list) {
            maxRows = std::max<std::size_t>(maxRows, list->rows.size());
        }
    }

    const std::size_t estimated = static_cast<std::size_t>(baseRows) + maxRows;
    return static_cast<uint16_t>(
        std::max<std::size_t>(static_cast<std::size_t>(minInnerHeight), estimated));
}

std::string markerPrefix(UiListComponent::Marker marker, bool selected) {
    if (!selected) {
        return "  ";
    }
    switch (marker) {
        case UiListComponent::Marker::Arrow: return "> ";
        case UiListComponent::Marker::Dot: return "* ";
        case UiListComponent::Marker::None:
        default:
            return "  ";
    }
}

} // namespace avantgarde::render

