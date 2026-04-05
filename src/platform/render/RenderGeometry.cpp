#include "platform/render/RenderGeometry.h"

#include <algorithm>

#include "platform/render/PreparedLayoutUtils.h"

namespace avantgarde::render {

UiFrameMetrics computeFrameMetrics(const UiPreparedLayout& prepared,
                                   float canvasWidthPx,
                                   float canvasHeightPx,
                                   float baseCellWidthPx,
                                   float baseCellHeightPx,
                                   float marginPx,
                                   float minAvailPx,
                                   float minCellWidthPx,
                                   float minCellHeightPx) {
    UiFrameMetrics out{};
    out.canvasWidthPx = std::max(0.0f, canvasWidthPx);
    out.canvasHeightPx = std::max(0.0f, canvasHeightPx);
    out.marginPx = std::max(0.0f, marginPx);

    out.frameWidthChars = resolvePreparedFrameWidth(prepared, 8U);
    out.innerHeightChars = resolvePreparedInnerHeight(
        prepared,
        estimateInnerHeight(prepared),
        1U);
    out.frameHeightChars = static_cast<uint16_t>(out.innerHeightChars + 2U);

    const float availW =
        std::max(minAvailPx, out.canvasWidthPx - out.marginPx * 2.0f);
    const float availH =
        std::max(minAvailPx, out.canvasHeightPx - out.marginPx * 2.0f);

    // Адаптивный масштаб символьной сетки.
    out.cellWidthPx = std::clamp(
        std::min(baseCellWidthPx, availW / static_cast<float>(out.frameWidthChars)),
        minCellWidthPx,
        baseCellWidthPx);
    out.cellHeightPx = std::clamp(
        std::min(baseCellHeightPx, availH / static_cast<float>(out.frameHeightChars)),
        minCellHeightPx,
        baseCellHeightPx);

    out.frameWidthPx = out.marginPx * 2.0f +
                       static_cast<float>(out.frameWidthChars) * out.cellWidthPx;
    out.frameHeightPx = out.marginPx * 2.0f +
                        static_cast<float>(out.frameHeightChars) * out.cellHeightPx;

    out.offsetXPx = std::max(0.0f, (out.canvasWidthPx - out.frameWidthPx) * 0.5f);
    out.offsetYPx = std::max(0.0f, (out.canvasHeightPx - out.frameHeightPx) * 0.5f);
    return out;
}

UiRectPx charsToPixelsTopDown(const UiFrameMetrics& metrics,
                              int16_t x,
                              int16_t y,
                              uint16_t w,
                              uint16_t h) {
    const float pxW =
        std::max(1.0f, static_cast<float>(w) * metrics.cellWidthPx);
    const float pxH =
        std::max(1.0f, static_cast<float>(h) * metrics.cellHeightPx);
    const float pxX =
        metrics.offsetXPx + metrics.marginPx + static_cast<float>(x) * metrics.cellWidthPx;
    const float pxY =
        metrics.offsetYPx + metrics.marginPx + static_cast<float>(y) * metrics.cellHeightPx;
    return UiRectPx{pxX, pxY, pxW, pxH};
}

UiRectPx toBottomUp(const UiRectPx& topDownRect, float canvasHeightPx) {
    const float bottomY = canvasHeightPx - (topDownRect.y + topDownRect.h);
    return UiRectPx{
        topDownRect.x,
        bottomY,
        topDownRect.w,
        topDownRect.h};
}

} // namespace avantgarde::render
