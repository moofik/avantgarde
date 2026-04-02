#include "service/ui/layout/UiNodeComponentComposer.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "service/ui/UiBindResolver.h"

namespace avantgarde {
namespace {

bool isContainerNode(UiLayoutNodeType type) noexcept {
    return type == UiLayoutNodeType::Column ||
           type == UiLayoutNodeType::Row ||
           type == UiLayoutNodeType::Unknown;
}

void markDescendantsConsumed(const UiLayoutNode& node,
                             std::unordered_set<const UiLayoutNode*>& consumed) {
    for (const UiLayoutNode& child : node.children) {
        consumed.insert(&child);
        markDescendantsConsumed(child, consumed);
    }
}

std::vector<std::string> keyCandidates(const UiLayoutNode& node) {
    std::vector<std::string> keys{};
    if (!node.bind.empty()) {
        keys.push_back(node.bind);
    }
    if (!node.id.empty()) {
        keys.push_back(node.id);
    }
    return keys;
}

std::optional<std::string> findTextByKeys(const UiPreparedParams& params,
                                          const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findText(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<int32_t> findIntegerByKeys(const UiPreparedParams& params,
                                         const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findInteger(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<bool> findFlagByKeys(const UiPreparedParams& params,
                                   const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findFlag(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> findRowsByKeys(const UiPreparedParams& params,
                                                       const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findRows(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::string resolveCanonicalBind(UiScene scene,
                                 UiLayoutNodeType nodeType,
                                 const UiLayoutNode& node) {
    if (node.bind.empty()) {
        return node.id;
    }
    if (nodeType == UiLayoutNodeType::Knob ||
        nodeType == UiLayoutNodeType::AnimSlot ||
        nodeType == UiLayoutNodeType::StatusBar) {
        const UiBindResolution resolved = UiBindResolver::resolve(scene, nodeType, node.bind);
        if (resolved.ok) {
            return resolved.canonical;
        }
    }
    return node.bind;
}

UiSeparatorComponent::Style parseSeparatorStyle(const std::string& raw) {
    std::string lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "light" || lower == "thin") {
        return UiSeparatorComponent::Style::Light;
    }
    return UiSeparatorComponent::Style::Heavy;
}

UiListComponent::Marker parseListMarker(const std::string& raw) {
    std::string lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "none") {
        return UiListComponent::Marker::None;
    }
    if (lower == "dot") {
        return UiListComponent::Marker::Dot;
    }
    return UiListComponent::Marker::Arrow;
}

std::unique_ptr<IUiComponent> buildNodeComponent(UiScene scene,
                                                 const UiLayoutNode& node,
                                                 const UiPreparedParams& params,
                                                 std::unordered_set<const UiLayoutNode*>& consumed);

template <typename TViewBuilder>
void fillViewSlots(TViewBuilder& view,
                   UiScene scene,
                   const UiLayoutNode& root,
                   const UiPreparedParams& params,
                   std::unordered_set<const UiLayoutNode*>& consumed) {
    auto visit = [&](const auto& self, const UiLayoutNode& cur) -> void {
        if (isContainerNode(cur.type)) {
            for (const UiLayoutNode& child : cur.children) {
                self(self, child);
            }
            return;
        }
        if (cur.id.empty()) {
            for (const UiLayoutNode& child : cur.children) {
                self(self, child);
            }
            return;
        }

        if (auto nested = buildNodeComponent(scene, cur, params, consumed)) {
            view.addToSlot(cur.id, std::move(nested));
        }
    };

    for (const UiLayoutNode& child : root.children) {
        visit(visit, child);
    }
}

std::unique_ptr<IUiComponent> buildNodeComponent(UiScene scene,
                                                 const UiLayoutNode& node,
                                                 const UiPreparedParams& params,
                                                 std::unordered_set<const UiLayoutNode*>& consumed) {
    if (node.id.empty()) {
        return nullptr;
    }

    switch (node.type) {
        case UiLayoutNodeType::StatusBar:
        case UiLayoutNodeType::Text: {
            const auto keys = keyCandidates(node);
            auto value = findTextByKeys(params, keys);
            if (!value.has_value() && !node.text.empty()) {
                value = node.text;
            }
            if (!value.has_value()) {
                throw std::runtime_error("UiNodeComponentComposer: unresolved text/status value for node '" + node.id + "'");
            }
            if (node.type == UiLayoutNodeType::StatusBar) {
                UiStatusBarBuilder b(node.id);
                b.text(std::move(*value));
                return std::move(b).build();
            }
            UiTextBuilder b(node.id);
            b.text(std::move(*value));
            return std::move(b).build();
        }

        case UiLayoutNodeType::List: {
            const auto keys = keyCandidates(node);
            const auto rowsOpt = findRowsByKeys(params, keys);
            std::vector<std::string> rows = rowsOpt.has_value() ? *rowsOpt : std::vector<std::string>{};

            std::vector<std::string> selectedKeys{};
            for (const std::string& key : keys) {
                selectedKeys.push_back(key + ".selectedRow");
                selectedKeys.push_back(key + ".selected");
            }
            const int32_t selectedRow = findIntegerByKeys(params, selectedKeys).value_or(-1);

            std::vector<std::string> markerKeys{};
            for (const std::string& key : keys) {
                markerKeys.push_back(key + ".marker");
            }
            UiListComponent::Marker marker = UiListComponent::Marker::Arrow;
            if (auto markerText = findTextByKeys(params, markerKeys); markerText.has_value()) {
                marker = parseListMarker(*markerText);
            }

            UiListBuilder b(node.id);
            b.rows(std::move(rows)).selectedRow(selectedRow).marker(marker);
            return std::move(b).build();
        }

        case UiLayoutNodeType::Separator: {
            const auto keys = keyCandidates(node);
            std::vector<std::string> styleKeys{};
            for (const std::string& key : keys) {
                styleKeys.push_back(key + ".style");
            }
            UiSeparatorComponent::Style style = UiSeparatorComponent::Style::Heavy;
            if (auto styleText = findTextByKeys(params, styleKeys); styleText.has_value()) {
                style = parseSeparatorStyle(*styleText);
            }
            UiSeparatorBuilder b(node.id);
            b.style(style);
            return std::move(b).build();
        }

        case UiLayoutNodeType::Spacer: {
            // Backward-compat: старые layout-шаблоны использовали spacer
            // и как list-тело (например id="tracks_body"), и как separator.
            const auto keys = keyCandidates(node);
            const auto rowsOpt = findRowsByKeys(params, keys);
            if (rowsOpt.has_value()) {
                std::vector<std::string> rows = *rowsOpt;
                std::vector<std::string> selectedKeys{};
                for (const std::string& key : keys) {
                    selectedKeys.push_back(key + ".selectedRow");
                    selectedKeys.push_back(key + ".selected");
                }
                const int32_t selectedRow = findIntegerByKeys(params, selectedKeys).value_or(-1);
                UiListBuilder b(node.id);
                b.rows(std::move(rows))
                    .selectedRow(selectedRow)
                    .marker(UiListComponent::Marker::Arrow);
                return std::move(b).build();
            }

            std::vector<std::string> styleKeys{};
            for (const std::string& key : keys) {
                styleKeys.push_back(key + ".style");
            }
            UiSeparatorComponent::Style style = UiSeparatorComponent::Style::Heavy;
            if (auto styleText = findTextByKeys(params, styleKeys); styleText.has_value()) {
                style = parseSeparatorStyle(*styleText);
            }
            UiSeparatorBuilder b(node.id);
            b.style(style);
            return std::move(b).build();
        }

        case UiLayoutNodeType::Knob: {
            const std::string key = resolveCanonicalBind(scene, UiLayoutNodeType::Knob, node);
            const float value01 = std::clamp(params.findNumber(key).value_or(0.0f), 0.0f, 1.0f);

            const std::vector<std::string> selectedKeys{
                key + ".selected",
                node.id + ".selected",
            };
            const bool selected = findFlagByKeys(params, selectedKeys).value_or(false);

            const std::vector<std::string> labelKeys{
                key + ".label",
                node.id + ".label",
            };
            const std::string label = findTextByKeys(params, labelKeys)
                                          .value_or(!node.label.empty() ? node.label : key);
            UiKnobBuilder b(node.id);
            b.label(label).value01(value01).selected(selected);
            return std::move(b).build();
        }

        case UiLayoutNodeType::AnimSlot: {
            const std::string key = resolveCanonicalBind(scene, UiLayoutNodeType::AnimSlot, node);
            const float intensity01 = std::clamp(params.findNumber(key).value_or(0.0f), 0.0f, 1.0f);
            const std::vector<std::string> labelKeys{
                key + ".label",
                node.id + ".label",
            };
            const std::vector<std::string> animKeyKeys{
                key + ".animKey",
                node.id + ".animKey",
            };
            const std::string label = findTextByKeys(params, labelKeys).value_or(key);
            const std::string animKey = findTextByKeys(params, animKeyKeys).value_or(key);
            UiAnimSlotBuilder b(node.id);
            b.label(label).animKey(animKey).intensity01(intensity01);
            return std::move(b).build();
        }

        case UiLayoutNodeType::TrackView: {
            UiTrackViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, params, consumed);
            return std::move(view).build();
        }
        case UiLayoutNodeType::ManagerView: {
            UiManagerViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, params, consumed);
            return std::move(view).build();
        }
        case UiLayoutNodeType::FxListView: {
            UiFxListViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, params, consumed);
            return std::move(view).build();
        }
        case UiLayoutNodeType::FxEditorView: {
            UiFxEditorViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, params, consumed);
            return std::move(view).build();
        }

        case UiLayoutNodeType::Column:
        case UiLayoutNodeType::Row:
        case UiLayoutNodeType::Unknown:
        default:
            return nullptr;
    }
}

} // namespace

void UiNodeComponentComposer::compose(UiScene scene,
                                      const UiLayoutTemplate& layoutTemplate,
                                      const UiPreparedParams& params,
                                      UiPreparedLayoutBuilder& builder) {
    std::unordered_set<const UiLayoutNode*> consumed{};
    layoutTemplate.forEachNode([&](const UiLayoutNode& node) {
        if (consumed.contains(&node) || node.id.empty() || isContainerNode(node.type)) {
            return;
        }
        if (auto component = buildNodeComponent(scene, node, params, consumed)) {
            builder.addComponent(std::move(component));
        }
    });
}

} // namespace avantgarde
