#include "service/ui/layout/UiNodeComponentComposer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "service/ui/UiCapabilityService.h"
#include "service/ui/UiBindResolver.h"

namespace avantgarde {
namespace {

// Контейнерные ноды задают структуру (row/column), но не дают конечный компонент.
bool isContainerNode(UiLayoutNodeType type) noexcept {
    return type == UiLayoutNodeType::Column ||
           type == UiLayoutNodeType::Row ||
           type == UiLayoutNodeType::Unknown;
}

// После сборки view-компонента его внутренние дочерние ноды
// не должны повторно добавляться как верхнеуровневые компоненты.
void markDescendantsConsumed(const UiLayoutNode& node,
                             std::unordered_set<const UiLayoutNode*>& consumed) {
    for (const UiLayoutNode& child : node.children) {
        consumed.insert(&child);
        markDescendantsConsumed(child, consumed);
    }
}

// Порядок приоритета lookup:
// 1) bind (канонический ключ параметра),
// 2) id ноды (локальный fallback).
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

// Ищем первое доступное текстовое значение по списку ключей.
std::optional<std::string> findTextByKeys(const UiPreparedParams& params,
                                          const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findText(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Ищем первое целочисленное значение по списку ключей.
std::optional<int32_t> findIntegerByKeys(const UiPreparedParams& params,
                                         const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findInteger(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Ищем первое числовое значение по списку ключей.
std::optional<float> findNumberByKeys(const UiPreparedParams& params,
                                      const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findNumber(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Ищем булев флаг по списку ключей.
std::optional<bool> findFlagByKeys(const UiPreparedParams& params,
                                   const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findFlag(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Ищем список строк (обычно для list-компонента).
std::optional<std::vector<std::string>> findRowsByKeys(const UiPreparedParams& params,
                                                       const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findRows(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Ищем waveform-пики по списку ключей.
std::optional<std::vector<float>> findWaveByKeys(const UiPreparedParams& params,
                                                 const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (auto value = params.findWave(key); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

// Для bind-чувствительных нод пытаемся получить канонический bind через resolver.
// Если bind пустой, используем id ноды.
std::string resolveCanonicalBind(UiScene scene,
                                 UiLayoutNodeType nodeType,
                                 const UiLayoutNode& node) {
    if (node.bind.empty()) {
        return node.id;
    }
    if (nodeType == UiLayoutNodeType::Knob ||
        nodeType == UiLayoutNodeType::Switch ||
        nodeType == UiLayoutNodeType::Icon ||
        nodeType == UiLayoutNodeType::AnimSlot ||
        nodeType == UiLayoutNodeType::Waveform ||
        nodeType == UiLayoutNodeType::StatusBar) {
        const UiBindResolution resolved = UiBindResolver::resolve(scene, nodeType, node.bind);
        if (resolved.ok) {
            return resolved.canonical;
        }
    }
    return node.bind;
}

// Нормализация стиля разделителя из строкового параметра.
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

// Нормализация маркера списка из строкового параметра.
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

void applyNodePresentationState(const UiScene scene,
                                const UiLayoutNode& node,
                                const UiState& state,
                                const UiNavState& nav,
                                const UiPreparedParams& params,
                                IUiComponent& component) {
    const bool visible = UiCapabilityService::evaluateCondition(
        scene, node, node.visibleIf, state, nav, params, true);
    const bool disabledMatched =
        !node.disabled.ifExpr.empty() &&
        UiCapabilityService::evaluateCondition(scene, node, node.disabled.ifExpr, state, nav, params, false);

    const bool hasActiveExpr = !node.active.ifExpr.empty();
    const bool hasInactiveExpr = !node.inactive.ifExpr.empty();
    const bool activeMatched =
        hasActiveExpr &&
        UiCapabilityService::evaluateCondition(scene, node, node.active.ifExpr, state, nav, params, false);
    const bool inactiveMatched =
        hasInactiveExpr &&
        UiCapabilityService::evaluateCondition(scene, node, node.inactive.ifExpr, state, nav, params, false);

    component.setVisible(visible);
    IUiComponent::VisualState visualState = IUiComponent::VisualState::Active;
    if (disabledMatched) {
        visualState = IUiComponent::VisualState::Disabled;
    } else if (activeMatched) {
        visualState = IUiComponent::VisualState::Active;
    } else if (inactiveMatched) {
        visualState = IUiComponent::VisualState::Inactive;
    } else if (hasActiveExpr && !hasInactiveExpr) {
        // Совместимая семантика для случая, когда задан только active.if:
        // false по active.if трактуется как переход в INACTIVE.
        visualState = IUiComponent::VisualState::Inactive;
    }

    component.setEnabled(visualState != IUiComponent::VisualState::Disabled);
    component.setVisualState(visualState);

    const float baseOpacity = std::clamp(node.opacity, 0.0f, 1.0f);
    const float stateOpacity = (visualState == IUiComponent::VisualState::Disabled)
                                   ? std::clamp(node.disabled.opacity, 0.0f, 1.0f)
                                   : (visualState == IUiComponent::VisualState::Inactive)
                                         ? std::clamp(node.inactive.opacity, 0.0f, 1.0f)
                                         : std::clamp(node.active.opacity, 0.0f, 1.0f);
    component.setOpacity(baseOpacity * stateOpacity);
}

std::unique_ptr<IUiComponent> buildNodeComponent(UiScene scene,
                                                 const UiLayoutNode& node,
                                                 const UiState& state,
                                                 const UiNavState& nav,
                                                 const UiPreparedParams& params,
                                                 std::unordered_set<const UiLayoutNode*>& consumed);

template <typename TViewBuilder>
void fillViewSlots(TViewBuilder& view,
                   UiScene scene,
                   const UiLayoutNode& root,
                   const UiState& state,
                   const UiNavState& nav,
                   const UiPreparedParams& params,
                   std::unordered_set<const UiLayoutNode*>& consumed) {
    // Рекурсивно обходим все дочерние ноды view-контейнера.
    // Каждую ноду с id пытаемся собрать в отдельный компонент и
    // кладем в slot одноименного имени.
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

        if (auto nested = buildNodeComponent(scene, cur, state, nav, params, consumed)) {
            view.addToSlot(cur.id, std::move(nested));
        }
    };

    // Начинаем обход только с дочерних узлов контейнера.
    for (const UiLayoutNode& child : root.children) {
        visit(visit, child);
    }
}

std::unique_ptr<IUiComponent> buildNodeComponent(UiScene scene,
                                                 const UiLayoutNode& node,
                                                 const UiState& state,
                                                 const UiNavState& nav,
                                                 const UiPreparedParams& params,
                                                 std::unordered_set<const UiLayoutNode*>& consumed) {
    // Нода без id не может быть адресована ни bind-ами, ни slot-ами.
    if (node.id.empty()) {
        return nullptr;
    }

    switch (node.type) {
        case UiLayoutNodeType::StatusBar:
        case UiLayoutNodeType::Text: {
            // Для текста разрешаем два источника:
            // 1) prepared params (динамический runtime state),
            // 2) статический node.text в шаблоне.
            const auto keys = keyCandidates(node);
            auto value = findTextByKeys(params, keys);
            if (!value.has_value() && !node.text.empty()) {
                value = node.text;
            }
            // Безопасный fallback: отсутствие bind-значения не должно ронять UI-цикл.
            // Для таких случаев оставляем компонент пустым (или с node.text, если он задан).
            if (!value.has_value()) {
                value = std::string{};
            }
            // StatusBar и Text используют разные типы компонентов,
            // хотя оба собираются из одной строки.
            if (node.type == UiLayoutNodeType::StatusBar) {
                UiStatusBarBuilder b(node.id);
                b.text(std::move(*value));
                auto component = std::move(b).build();
                applyNodePresentationState(scene, node, state, nav, params, *component);
                return component;
            }
            UiTextBuilder b(node.id);
            b.text(std::move(*value));
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::List: {
            // rows могут отсутствовать: тогда отрисуется пустой список.
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
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Separator: {
            // separator стиль берется из "<key>.style", если параметр задан.
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
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Spacer: {
            // Spacer — чисто layout-геометрия без отдельного визуального компонента.
            return nullptr;
        }

        case UiLayoutNodeType::Knob: {
            // 1) Резолв bind ключа.
            const std::string key = resolveCanonicalBind(scene, UiLayoutNodeType::Knob, node);
            // 2) Нормализация значения в диапазон [0..1].
            const float value01 = std::clamp(params.findNumber(key).value_or(0.0f), 0.0f, 1.0f);

            // 3) Признак selected нужен для визуальной подсветки активного параметра.
            const std::vector<std::string> selectedKeys{
                key + ".selected",
                node.id + ".selected",
            };
            const bool selected = findFlagByKeys(params, selectedKeys).value_or(false);

            // 4) label приоритетно берем из params, затем из шаблона.
            const std::vector<std::string> labelKeys{
                key + ".label",
                node.id + ".label",
            };
            const std::string label = findTextByKeys(params, labelKeys)
                                          .value_or(!node.label.empty() ? node.label : key);
            UiKnobBuilder b(node.id);
            b.label(label).value01(value01).selected(selected);
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Switch: {
            const std::string key = resolveCanonicalBind(scene, UiLayoutNodeType::Switch, node);
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

            std::vector<std::string> options = node.options;
            if (options.empty()) {
                // Дефолт для бинарного свитча.
                options = {"OFF", "ON"};
            }

            uint16_t selectedIndex = 0U;
            const std::vector<std::string> selectedIndexKeys{
                key + ".selectedIndex",
                node.id + ".selectedIndex",
            };
            if (auto fromParams = findIntegerByKeys(params, selectedIndexKeys); fromParams.has_value()) {
                selectedIndex = static_cast<uint16_t>(std::max<int32_t>(0, *fromParams));
            } else if (options.size() > 1U) {
                // Если индекс не передан явно, вычисляем его из value01.
                const float scaled = value01 * static_cast<float>(options.size() - 1U);
                selectedIndex = static_cast<uint16_t>(std::lround(scaled));
            }
            if (!options.empty()) {
                // Защита от выхода за границы массива options.
                selectedIndex = std::min<uint16_t>(selectedIndex, static_cast<uint16_t>(options.size() - 1U));
            }

            UiSwitchBuilder b(node.id);
            b.label(label)
                .options(std::move(options))
                .selectedIndex(selectedIndex)
                .selected(selected);
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Icon: {
            // path берется в порядке приоритета:
            // 1) prepared params по bind/id (динамический путь),
            // 2) node.path из шаблона.
            const auto keys = keyCandidates(node);
            auto pathValue = findTextByKeys(params, keys);
            if (!pathValue.has_value() && !node.assetPath.empty()) {
                pathValue = node.assetPath;
            }
            UiIconBuilder b(node.id);
            b.path(pathValue.value_or(std::string{}));
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::AnimSlot: {
            // AnimSlot получает не только intensity, но и семантический animKey,
            // по которому рендерер ищет источник анимации.
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
            const std::string label = findTextByKeys(params, labelKeys)
                                          .value_or(!node.label.empty() ? node.label : key);
            const std::string animKey = findTextByKeys(params, animKeyKeys).value_or(key);
            UiAnimSlotBuilder b(node.id);
            b.label(label).animKey(animKey).intensity01(intensity01);
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Waveform: {
            const std::vector<std::string> keys = keyCandidates(node);
            const std::vector<float> peaks = findWaveByKeys(params, keys).value_or(std::vector<float>{});

            std::vector<std::string> trimStartKeys{};
            std::vector<std::string> trimEndKeys{};
            for (const std::string& k : keys) {
                trimStartKeys.push_back(k + ".trim_start");
                trimEndKeys.push_back(k + ".trim_end");
            }
            const float trimStart01 = std::clamp(findNumberByKeys(params, trimStartKeys).value_or(0.0f), 0.0f, 0.99f);
            const float trimEnd01 = std::clamp(findNumberByKeys(params, trimEndKeys).value_or(1.0f), 0.01f, 1.0f);

            UiWaveformBuilder b(node.id);
            b.peaks01(peaks).trimStart01(trimStart01).trimEnd01(trimEnd01);
            auto component = std::move(b).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::TrackView: {
            // Функциональный view-компонент.
            // Внутри может содержать произвольные вложенные слоты (text, knob, list и т.д.).
            UiTrackViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, state, nav, params, consumed);
            auto component = std::move(view).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }
        case UiLayoutNodeType::ManagerView: {
            UiManagerViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, state, nav, params, consumed);
            auto component = std::move(view).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }
        case UiLayoutNodeType::FxListView: {
            UiFxListViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, state, nav, params, consumed);
            auto component = std::move(view).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }
        case UiLayoutNodeType::FxEditorView: {
            UiFxEditorViewBuilder view(node.id);
            markDescendantsConsumed(node, consumed);
            fillViewSlots(view, scene, node, state, nav, params, consumed);
            auto component = std::move(view).build();
            applyNodePresentationState(scene, node, state, nav, params, *component);
            return component;
        }

        case UiLayoutNodeType::Column:
        case UiLayoutNodeType::Row:
        case UiLayoutNodeType::Unknown:
        default:
            // Чисто структурные ноды верхнего уровня не превращаем в компонент.
            return nullptr;
    }
}

} // namespace

void UiNodeComponentComposer::compose(UiScene scene,
                                      const UiLayoutTemplate& layoutTemplate,
                                      const UiState& state,
                                      const UiNavState& nav,
                                      const UiPreparedParams& params,
                                      UiPreparedLayoutBuilder& builder) {
    // Множество уже "поглощенных" нод. Нуженo, чтобы дочерние ноды
    // view-контейнеров не добавились второй раз на верхнем уровне.
    std::unordered_set<const UiLayoutNode*> consumed{};
    layoutTemplate.forEachNode([&](const UiLayoutNode& node) {
        if (consumed.contains(&node) || node.id.empty() || isContainerNode(node.type)) {
            return;
        }
        // Каждая валидная нода транслируется в один typed-компонент.
        if (auto component = buildNodeComponent(scene, node, state, nav, params, consumed)) {
            builder.addComponent(std::move(component));
        }
    });
}

} // namespace avantgarde
