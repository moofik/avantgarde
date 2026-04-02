#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "contracts/UiLayout.h"
#include "contracts/ui/components/UiComponents.h"

namespace avantgarde {

// Prepared-layout кадр: результат работы виджета перед передачей в renderer.
struct UiPreparedLayout {
    // Идентификатор сцены/виджета.
    std::string sceneId{};
    // Ссылка на исходный TOML layout-template.
    const UiLayoutTemplate* layoutTemplate{nullptr};
    // Базовые ограничения кадра.
    uint16_t frameWidth{60};
    uint16_t frameHeightHint{0};
    // Набор UI-компонентов с уже подставленными значениями из state.
    std::vector<std::unique_ptr<IUiComponent>> components{};
};

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

