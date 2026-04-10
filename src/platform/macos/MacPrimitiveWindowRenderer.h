#pragma once

#include <string>

#include "contracts/IUi.h"
#include "contracts/UiPreparedLayout.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

// macOS renderer на процедурной графике.
// Рендерит UiPreparedLayout примитивами:
// линии, прямоугольники, круги, текстовые подписи.
class MacPrimitiveWindowRenderer final : public IUiRenderer {
public:
    explicit MacPrimitiveWindowRenderer(UiTheme theme);
    ~MacPrimitiveWindowRenderer() override;

    // Базовый контракт IUiRenderer. Для layout-driven режима основной путь —
    // renderPreparedLayout(...), поэтому этот метод используется как fallback.
    void render(const UiState& state) override;

    // Основной путь: отрисовать подготовленный кадр напрямую из UiPreparedLayout.
    void renderPreparedLayout(const UiPreparedLayout& prepared);

    // Pump событий окна (должен вызываться из main thread).
    void pumpEvents() noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace avantgarde
