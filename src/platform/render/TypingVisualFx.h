#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "platform/render/IVisualFx.h"

namespace avantgarde {

// Typing FX: имитирует "печать" текста в стиле 8-bit диалогов.
// speed управляет скоростью раскрытия, trigger=change перезапускает печать при изменении value.
class TypingVisualFx final : public IVisualFx {
public:
    std::string_view id() const noexcept override { return "typing"; }
    VisualFxBlockStyle resolve(const VisualFxRequest& request) override;
    bool applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) override;

private:
    std::string buildStateKey_(const VisualFxRequest& request) const;
    uint32_t computeDurationMs_(const VisualFxRequest& request, float speed) const;

    // Последний управляющий value для trigger=change.
    std::unordered_map<std::string, float> lastValue01_{};
    // Epoch запуска текущего "печатающего" прохода.
    std::unordered_map<std::string, uint64_t> startMs_{};
    // Epoch для time-trigger режима.
    std::unordered_map<std::string, uint64_t> timeEpochMs_{};
};

} // namespace avantgarde

