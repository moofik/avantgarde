#pragma once

#include <cstdint>
#include <vector>

#include "platform/render/VisualFxProcessor.h"

namespace avantgarde::render {

// Один срез текста в локальных координатах drawRect.
struct TextSliceSpan {
    float y{0.0f};
    float height{0.0f};
    float offsetX{0.0f};
};

// Готовый план отрисовки glitch-текста:
// базовый слой, split-слои и набор срезов.
struct TextGlitchPlan {
    bool active{false};
    float baseOffsetX{0.0f};
    float splitPx{0.0f};
    float alpha{1.0f};
    std::vector<TextSliceSpan> slices{};
};

// Строит план отрисовки glitch на основе style.
// Вся математика эффекта живет здесь, а не в renderer.
TextGlitchPlan buildTextGlitchPlan(const VisualFxTextGeometryStyle& style, float totalHeightPx);

} // namespace avantgarde::render
