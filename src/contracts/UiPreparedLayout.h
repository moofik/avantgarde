#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "contracts/UiLayout.h"
#include "contracts/ui/components/UiComponents.h"

namespace avantgarde {

enum class UiHudPosition : uint8_t {
    TopCenter = 0,
    Center
};

// Подготовленный runtime-вид HUD-оверлея для одного кадра.
// Это уже "что рисовать", без знания очередей/таймеров/событий.
struct UiHudOverlayView {
    bool visible{false};
    UiHudPosition position{UiHudPosition::TopCenter};
    std::string text{};

    uint16_t width{20};
    uint16_t height{5};
    uint16_t padding{1};
    std::string font{"gothic"};
    float fontSize{0.0f};
    UiLayoutAlign align{UiLayoutAlign::Center};
    UiLayoutJustify justify{UiLayoutJustify::Center};
    bool textWrap{true};
    // Цепочка текстовых визуальных эффектов HUD (typing/glitch/glow/...).
    // Рендерер применяет только поддерживаемые им эффекты.
    std::vector<UiLayoutNode::EffectSpec> textEffects{};
    // Уникальный id жизненного цикла текущего HUD-сообщения.
    // Используется visual-fx слоем как стабильный instance key,
    // чтобы trigger=change корректно перезапускался для каждого уведомления.
    uint64_t fxInstanceId{0U};

    std::string textColor{"#E3D4E8"};
    std::string borderColor{"#8F6E95"};
    std::string backgroundColor{"#130D16D8"};

    float opacity{1.0f};
    float scale{1.0f};
    float glow01{0.0f};
    float glitch01{0.0f};
};

// Prepared-layout кадр: результат работы виджета перед передачей в renderer.
struct UiPreparedLayout {
    // Идентификатор сцены/виджета.
    std::string sceneId{};
    // Ссылка на исходный layout-template.
    const UiLayoutTemplate* layoutTemplate{nullptr};
    // Базовые ограничения кадра.
    uint16_t frameWidth{60};
    uint16_t frameHeightHint{0};
    // Набор UI-компонентов с уже подставленными значениями из state.
    std::vector<std::unique_ptr<IUiComponent>> components{};
    // Всплывающий HUD-оверлей поверх сцены.
    UiHudOverlayView hud{};
};

// Вернуть px-размер root-ноды в символьной сетке, если он задан как фиксированный.
inline bool resolveLayoutRootPxSize(const UiLayoutSize& size, uint16_t& out) noexcept {
    if (size.unit != UiLayoutSize::Unit::Px || size.value <= 0.0f) {
        return false;
    }
    const int rounded = static_cast<int>(std::lround(size.value));
    if (rounded <= 0) {
        return false;
    }
    out = static_cast<uint16_t>(std::clamp(rounded, 1, 65535));
    return true;
}

// Итоговая ширина кадра в символах (приоритет: [layout].width px -> prepared.frameWidth).
inline uint16_t resolvePreparedFrameWidth(const UiPreparedLayout& prepared,
                                          uint16_t minWidth = 4U) noexcept {
    uint16_t width = prepared.frameWidth;
    if (prepared.layoutTemplate != nullptr) {
        uint16_t fromTemplate = 0U;
        if (resolveLayoutRootPxSize(prepared.layoutTemplate->root.width, fromTemplate)) {
            width = fromTemplate;
        }
    }
    return std::max<uint16_t>(width, minWidth);
}

// Итоговая внутренняя высота кадра в символах
// (приоритет: [layout].height px -> prepared.frameHeightHint -> fallbackInnerHeight).
inline uint16_t resolvePreparedInnerHeight(const UiPreparedLayout& prepared,
                                           uint16_t fallbackInnerHeight,
                                           uint16_t minHeight = 1U) noexcept {
    uint16_t inner = (prepared.frameHeightHint > 0U) ? prepared.frameHeightHint : fallbackInnerHeight;
    if (prepared.layoutTemplate != nullptr) {
        uint16_t fromTemplate = 0U;
        if (resolveLayoutRootPxSize(prepared.layoutTemplate->root.height, fromTemplate)) {
            inner = fromTemplate;
        }
    }
    return std::max<uint16_t>(inner, minHeight);
}

// Builder верхнего уровня для сборки UiPreparedLayout.
class UiPreparedLayoutBuilder final {
public:
    UiPreparedLayoutBuilder& sceneId(std::string value) {
        layout_.sceneId = std::move(value);
        return *this;
    }

    UiPreparedLayoutBuilder& templateRef(const UiLayoutTemplate* value) {
        layout_.layoutTemplate = value;
        return *this;
    }

    UiPreparedLayoutBuilder& frameWidth(uint16_t value) {
        layout_.frameWidth = value;
        return *this;
    }

    UiPreparedLayoutBuilder& frameHeightHint(uint16_t value) {
        layout_.frameHeightHint = value;
        return *this;
    }

    UiPreparedLayoutBuilder& addComponent(std::unique_ptr<IUiComponent> component) {
        if (component) {
            layout_.components.push_back(std::move(component));
        }
        return *this;
    }

    template <typename TBuilder>
    UiPreparedLayoutBuilder& addComponent(TBuilder builder) {
        return addComponent(std::move(builder).build());
    }

    UiPreparedLayout build() && {
        return std::move(layout_);
    }

private:
    UiPreparedLayout layout_{};
};

} // namespace avantgarde
