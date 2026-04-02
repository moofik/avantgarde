#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "contracts/UiLayout.h"
#include "service/ui/layout/SceneFrame.h"

namespace avantgarde {

// Размещенный узел декларативного layout-дерева.
// rect всегда задан в локальных координатах корневого layout (без внешней рамки SceneFrame).
struct UiLayoutBox {
    const UiLayoutNode* node{nullptr};
    SceneFrameRect rect{};
    uint16_t depth{0};
};

// Универсальный layout-движок для fallback/UI-template режима.
// Отвечает только за measure/arrange, не знает ничего о runtime-состоянии сцен.
class UiLayoutEngine final {
public:
    struct NodeMetrics {
        uint16_t minWidth{0};
        uint16_t minHeight{0};
    };

    // Кастомная оценка intrinsic размера leaf-узла.
    // Для container-узлов (row/column) движок считает размер сам.
    using MeasureFn = std::function<NodeMetrics(const UiLayoutNode&)>;

    struct Result {
        std::vector<UiLayoutBox> boxes{};
    };

    // Полный layout-pass: measure + arrange.
    // width/height задаются в ячейках внутренней области (без внешней рамки кадра).
    static Result arrange(const UiLayoutNode& root,
                          uint16_t width,
                          uint16_t height,
                          const MeasureFn& measure = {});

    // Поиск первого размещенного узла по id.
    static const UiLayoutBox* findById(const Result& result, std::string_view id) noexcept;

    // Поиск всех узлов заданного типа.
    static std::vector<const UiLayoutBox*> findByType(const Result& result, UiLayoutNodeType type);
};

} // namespace avantgarde
