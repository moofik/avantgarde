#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "platform/render/IVisualFx.h"

namespace avantgarde {

// Мягкий glow FX для живой подсветки текста/лейблов.
// Делает легкий ореол и микропульсацию без "нарезки" полос как у glitch.
class GlowVisualFx final : public IVisualFx {
public:
    std::string_view id() const noexcept override { return "glow"; }
    VisualFxBlockStyle resolve(const VisualFxRequest& request) override;
    bool applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) override;

private:
    std::string buildStateKey_(const VisualFxRequest& request) const;

    // Последнее значение [0..1] для trigger=change.
    std::unordered_map<std::string, float> lastValue01_{};
    // Время последнего изменения value (ms).
    std::unordered_map<std::string, uint64_t> lastChangeMs_{};
    // Эпоха старта time-trigger для конкретной ноды/инстанса.
    std::unordered_map<std::string, uint64_t> timeEpochMs_{};
};

} // namespace avantgarde
