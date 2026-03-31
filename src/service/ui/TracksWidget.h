#pragma once

#include <cstdint>
#include <string>

#include "contracts/IUiWidget.h"

namespace avantgarde {

// Виджет основного экрана (транспорт + треки).
// Отрисовка делегирована GbFrameComposer, но точка входа в scene-layer
// остается единообразной: любой экран — это IUiWidget.
class TracksWidget final : public IUiWidget {
public:
    struct Options {
        // Текстовая ширина кадра (в символах).
        uint16_t frameWidth{60};
        // Заголовок в верхней рамке.
        std::string headerTitle{"AVANTGARDE"};
    };

    TracksWidget() noexcept;
    // Ctor с явной конфигурацией кадра/заголовка.
    explicit TracksWidget(const Options& options) noexcept;

    // Стабильный идентификатор виджета для хоста сцен.
    const char* id() const noexcept override;
    // Рендер текущего UiState в набор строк.
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    // Обработка input для экрана треков (пока без локальных интентов).
    WidgetOutput onInput(UiInputAction action, const UiState& rtState, UiNavState& navState) override;

private:
    // Фактическая ширина рендера рамок.
    uint16_t frameWidth_{60};
    // Текст шапки экрана.
    std::string headerTitle_{"AVANTGARDE"};
};

} // namespace avantgarde
