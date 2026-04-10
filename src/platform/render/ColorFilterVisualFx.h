#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "platform/render/IVisualFx.h"

namespace avantgarde {

// Универсальный color-filter FX для любых RGBA-блоков (иконки/текст/анимации).
// effect_amount управляет силой фильтра:
//   0.0 = без изменений
//   1.0 = полный перевод в целевой цветовой фильтр.
// effect_color задает целевой оттенок (обычно серый, например "#808080").
class ColorFilterVisualFx final : public IVisualFx {
public:
    std::string_view id() const noexcept override { return "color_filter"; }
    VisualFxBlockStyle resolve(const VisualFxRequest& request) override;
    bool applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) override;

private:
    std::string buildStateKey_(const VisualFxRequest& request) const;

    // Состояния для trigger=change.
    std::unordered_map<std::string, float> lastValue01_{};
    std::unordered_map<std::string, uint64_t> lastChangeMs_{};
    // Эпоха для trigger=time.
    std::unordered_map<std::string, uint64_t> timeEpochMs_{};
};

} // namespace avantgarde

