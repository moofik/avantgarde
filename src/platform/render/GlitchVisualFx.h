#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "platform/render/IVisualFx.h"

namespace avantgarde {

// Универсальный glitch FX. Не привязан к тексту:
// возвращает block-style, который можно применить к любому ROI.
class GlitchVisualFx final : public IVisualFx {
public:
    std::string_view id() const noexcept override { return "glitch"; }
    VisualFxBlockStyle resolve(const VisualFxRequest& request) override;

private:
    std::string buildStateKey_(const VisualFxRequest& request) const;

    // Последнее значение управляющего параметра [0..1] для trigger=change.
    std::unordered_map<std::string, float> lastValue01_{};
    // Время последнего изменения value (ms).
    std::unordered_map<std::string, uint64_t> lastChangeMs_{};
    // Эпоха запуска time-trigger для конкретного инстанса.
    std::unordered_map<std::string, uint64_t> timeEpochMs_{};
};

} // namespace avantgarde

