#include "service/ui/layout/UiPreparedLayoutAsciiRenderer.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "service/ui/layout/SceneFrameAsciiRenderer.h"
#include "service/ui/layout/UiLayoutEngine.h"

namespace avantgarde {
namespace {

SceneFrameRect toFrameRect(const UiLayoutBox& box) noexcept {
    return SceneFrameRect{
        .x = static_cast<int16_t>(box.rect.x + 1),
        .y = static_cast<int16_t>(box.rect.y + 1),
        .width = box.rect.width,
        .height = box.rect.height,
    };
}

void pushText(SceneFrame& frame,
              const SceneFrameRect& rect,
              uint16_t row,
              const std::string& text) {
    if (row >= rect.height || rect.width == 0U) {
        return;
    }
    frame.texts.push_back(SceneFrameText{
        .x = rect.x,
        .y = static_cast<int16_t>(rect.y + static_cast<int16_t>(row)),
        .text = text,
        .width = rect.width,
    });
}

void pushHLine(SceneFrame& frame,
               const SceneFrameRect& rect,
               uint16_t row,
               const std::string& glyph) {
    if (row >= rect.height || rect.width == 0U || glyph.empty()) {
        return;
    }
    frame.hlines.push_back(SceneFrameHLine{
        .x = rect.x,
        .y = static_cast<int16_t>(rect.y + static_cast<int16_t>(row)),
        .length = rect.width,
        .glyph = glyph,
    });
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

void collectComponentsById(const IUiComponent* component,
                           std::unordered_map<std::string, const IUiComponent*>& out) {
    if (!component) {
        return;
    }
    if (!component->id().empty()) {
        out[std::string(component->id())] = component;
    }

    auto collectSlots = [&out](const auto* view) {
        if (!view) {
            return;
        }
        for (const UiComponentSlot& slot : view->slots) {
            for (const auto& child : slot.components) {
                collectComponentsById(child.get(), out);
            }
        }
    };

    switch (component->type()) {
        case UiComponentType::TrackView:
            collectSlots(dynamic_cast<const UiTrackViewComponent*>(component));
            break;
        case UiComponentType::ManagerView:
            collectSlots(dynamic_cast<const UiManagerViewComponent*>(component));
            break;
        case UiComponentType::FxListView:
            collectSlots(dynamic_cast<const UiFxListViewComponent*>(component));
            break;
        case UiComponentType::FxEditorView:
            collectSlots(dynamic_cast<const UiFxEditorViewComponent*>(component));
            break;
        case UiComponentType::StatusBar:
        case UiComponentType::Text:
        case UiComponentType::Knob:
        case UiComponentType::AnimSlot:
        case UiComponentType::List:
        case UiComponentType::Separator:
        default:
            break;
    }
}

uint16_t estimateInnerHeight(const UiPreparedLayout& prepared) {
    if (prepared.frameHeightHint > 0U) {
        return prepared.frameHeightHint;
    }
    std::size_t maxRows = 0U;
    for (const auto& ptr : prepared.components) {
        if (!ptr) {
            continue;
        }
        if (ptr->type() == UiComponentType::List) {
            const auto* list = dynamic_cast<const UiListComponent*>(ptr.get());
            if (list) {
                maxRows = std::max<std::size_t>(maxRows, list->rows.size());
            }
        }
    }
    const std::size_t base = 8U;
    return static_cast<uint16_t>(std::max<std::size_t>(12U, base + maxRows));
}

} // namespace

std::vector<std::string> UiPreparedLayoutAsciiRenderer::render(const UiPreparedLayout& prepared) {
    if (prepared.layoutTemplate == nullptr) {
        return {};
    }

    const uint16_t width = std::max<uint16_t>(prepared.frameWidth, 4U);
    const uint16_t innerW = static_cast<uint16_t>(width - 2U);
    const uint16_t innerH = estimateInnerHeight(prepared);

    SceneFrame frame{};
    frame.width = width;
    frame.height = static_cast<uint16_t>(innerH + 2U);
    frame.rects.push_back(SceneFrameRect{
        .x = 0,
        .y = 0,
        .width = width,
        .height = frame.height,
    });

    std::unordered_map<std::string, const IUiComponent*> byId{};
    byId.reserve(prepared.components.size());
    for (const auto& c : prepared.components) {
        collectComponentsById(c.get(), byId);
    }

    const UiLayoutEngine::Result layout = UiLayoutEngine::arrange(prepared.layoutTemplate->root, innerW, innerH);
    for (const UiLayoutBox& box : layout.boxes) {
        if (!box.node || box.node->id.empty()) {
            continue;
        }
        const SceneFrameRect rect = toFrameRect(box);
        const auto it = byId.find(box.node->id);
        const IUiComponent* component = (it == byId.end()) ? nullptr : it->second;

        switch (box.node->type) {
            case UiLayoutNodeType::StatusBar:
            case UiLayoutNodeType::Text: {
                if (const auto* s = dynamic_cast<const UiStatusBarComponent*>(component)) {
                    pushText(frame, rect, 0U, s->text);
                } else if (const auto* t = dynamic_cast<const UiTextComponent*>(component)) {
                    pushText(frame, rect, 0U, t->text);
                } else if (!box.node->text.empty()) {
                    pushText(frame, rect, 0U, box.node->text);
                }
            } break;

            case UiLayoutNodeType::List: {
                if (const auto* list = dynamic_cast<const UiListComponent*>(component)) {
                    for (std::size_t i = 0; i < rect.height; ++i) {
                        if (i >= list->rows.size()) {
                            pushText(frame, rect, static_cast<uint16_t>(i), " ");
                            continue;
                        }
                        const bool selected = (list->selectedRow >= 0) && (static_cast<std::size_t>(list->selectedRow) == i);
                        pushText(frame,
                                 rect,
                                 static_cast<uint16_t>(i),
                                 markerPrefix(list->marker, selected) + list->rows[i]);
                    }
                }
            } break;

            case UiLayoutNodeType::Separator: {
                if (const auto* sep = dynamic_cast<const UiSeparatorComponent*>(component)) {
                    const std::string glyph = (sep->style == UiSeparatorComponent::Style::Heavy) ? "═" : "─";
                    pushHLine(frame, rect, 0U, glyph);
                }
            } break;

            case UiLayoutNodeType::Spacer: {
                // Backward compatibility: старые шаблоны использовали spacer
                // и для list, и для separator.
                if (const auto* sep = dynamic_cast<const UiSeparatorComponent*>(component)) {
                    const std::string glyph = (sep->style == UiSeparatorComponent::Style::Heavy) ? "═" : "─";
                    pushHLine(frame, rect, 0U, glyph);
                    break;
                }
                if (const auto* list = dynamic_cast<const UiListComponent*>(component)) {
                    for (std::size_t i = 0; i < rect.height; ++i) {
                        if (i >= list->rows.size()) {
                            pushText(frame, rect, static_cast<uint16_t>(i), " ");
                            continue;
                        }
                        const bool selected = (list->selectedRow >= 0) && (static_cast<std::size_t>(list->selectedRow) == i);
                        pushText(frame,
                                 rect,
                                 static_cast<uint16_t>(i),
                                 markerPrefix(list->marker, selected) + list->rows[i]);
                    }
                }
            } break;

            case UiLayoutNodeType::TrackView:
            case UiLayoutNodeType::ManagerView:
            case UiLayoutNodeType::FxListView:
            case UiLayoutNodeType::FxEditorView:
                // Функциональные view-ноды работают как контейнеры layout-а.
                // Их дочерние элементы рендерятся по собственным node-id.
                break;

            case UiLayoutNodeType::Knob: {
                const auto* knob = dynamic_cast<const UiKnobComponent*>(component);
                if (!knob) {
                    break;
                }
                frame.knobs.push_back(SceneFrameKnob{
                    .x = rect.x,
                    .y = rect.y,
                    .label = knob->label,
                    .value01 = std::clamp(knob->value01, 0.0f, 1.0f),
                    .selected = knob->selected,
                });
            } break;

            case UiLayoutNodeType::AnimSlot: {
                const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component);
                if (!anim) {
                    break;
                }
                frame.animSlots.push_back(SceneFrameAnimSlot{
                    .x = rect.x,
                    .y = rect.y,
                    .width = std::max<uint16_t>(rect.width, 4U),
                    .height = std::max<uint16_t>(rect.height, 3U),
                    .label = anim->label,
                });
            } break;

            case UiLayoutNodeType::Column:
            case UiLayoutNodeType::Row:
            case UiLayoutNodeType::Unknown:
            default:
                break;
        }
    }

    return SceneFrameAsciiRenderer::render(frame);
}

} // namespace avantgarde
