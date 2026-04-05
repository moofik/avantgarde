#pragma once

#include <cstdint>

#include "contracts/UiPreparedLayout.h"

namespace avantgarde::render {

// Прямоугольник в пикселях.
struct UiRectPx {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};
};

// Метрики кадра: символьная сетка, масштаб и оффсеты.
// Координатная система top-down (y растет вниз).
struct UiFrameMetrics {
    uint16_t frameWidthChars{8U};
    uint16_t innerHeightChars{12U};
    uint16_t frameHeightChars{14U};

    float canvasWidthPx{0.0f};
    float canvasHeightPx{0.0f};
    float marginPx{14.0f};

    float cellWidthPx{12.0f};
    float cellHeightPx{18.0f};

    float frameWidthPx{0.0f};
    float frameHeightPx{0.0f};
    float offsetXPx{0.0f};
    float offsetYPx{0.0f};
};

// Считает геометрию кадра (масштаб сетки + центрирование) под текущий canvas.
UiFrameMetrics computeFrameMetrics(const UiPreparedLayout& prepared,
                                   float canvasWidthPx,
                                   float canvasHeightPx,
                                   float baseCellWidthPx = 12.0f,
                                   float baseCellHeightPx = 18.0f,
                                   float marginPx = 14.0f,
                                   float minAvailPx = 120.0f,
                                   float minCellWidthPx = 6.0f,
                                   float minCellHeightPx = 10.0f);

// Переводит прямоугольник из символьной сетки в пиксели (top-down).
UiRectPx charsToPixelsTopDown(const UiFrameMetrics& metrics,
                              int16_t x,
                              int16_t y,
                              uint16_t w,
                              uint16_t h);

// Конвертирует top-down прямоугольник в bottom-up систему координат.
UiRectPx toBottomUp(const UiRectPx& topDownRect, float canvasHeightPx);

} // namespace avantgarde::render

