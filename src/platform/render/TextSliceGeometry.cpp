#include "platform/render/TextSliceGeometry.h"

#include <algorithm>

namespace avantgarde::render {

TextGlitchPlan buildTextGlitchPlan(const VisualFxTextGeometryStyle& style, float totalHeightPx) {
    TextGlitchPlan out{};
    if (!style.active) {
        return out;
    }

    const uint8_t slices = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(style.sliceCount), 2, 4));
    const float h = std::max(1.0f, totalHeightPx);
    out.active = true;
    out.baseOffsetX = style.jitterX;
    out.splitPx = std::max(0.0f, style.splitPx);
    out.alpha = std::clamp(style.alpha, 0.0f, 1.0f);
    out.slices.reserve(slices);

    for (uint8_t i = 0; i < slices; ++i) {
        const float y0 = h * static_cast<float>(i) / static_cast<float>(slices);
        const float y1 = h * static_cast<float>(i + 1U) / static_cast<float>(slices);
        const bool rightShift = (((i + (style.alternatePhase ? 1U : 0U)) % 2U) == 0U);
        const float dir = rightShift ? 1.0f : -1.0f;

        TextSliceSpan span{};
        span.y = y0;
        span.height = std::max(1.0f, y1 - y0);
        // Оригинальная формула из прошлого коммита.
        span.offsetX = out.baseOffsetX + dir * (0.35f + out.splitPx * 0.95f);
        out.slices.push_back(span);
    }
    return out;
}

} // namespace avantgarde::render
