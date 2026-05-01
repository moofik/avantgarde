#pragma once

#include <cstdint>
#include <chrono>
#include <string>

#include "contracts/UiPreparedLayout.h"
#include "platform/render/VisualFxProcessor.h"
#include "platform/raspi/RpiPixelCanvas.h"

namespace avantgarde::raspi {

// Контекст painter-а для RPi primitive renderer.
struct RpiPrimitiveScenePaintContext {
    RpiPixelCanvas* canvas{nullptr};
    uint64_t frameTick{0U};
    uint64_t nowMs{0U};
    std::chrono::steady_clock::time_point startTs{};
    std::string cwd{};
    VisualFxProcessor* visualFx{nullptr};
};

// Полная отрисовка prepared-layout кадра в пиксельный canvas.
void renderPreparedLayoutScene(const RpiPrimitiveScenePaintContext& ctx,
                               const UiPreparedLayout& prepared);

} // namespace avantgarde::raspi
