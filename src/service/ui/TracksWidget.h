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
        // Шаг скорости трека для pointer-режима.
        float speedStep{0.05f};
        // Шаг BPM для pointer-режима.
        float bpmStep{1.0f};
    };

    TracksWidget() noexcept;
    // Ctor с явной конфигурацией кадра/заголовка.
    explicit TracksWidget(const Options& options) noexcept;

    // Стабильный идентификатор виджета для хоста сцен.
    const char* id() const noexcept override;
    // Рендер текущего UiState в набор строк.
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    // Обработка input для экрана треков (пока без локальных интентов).
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    // Возвращает scene-local action catalog (tracks screen).
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    // Маппит action (+nav/ui state) в intents.
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    // Формирование строки статуса активного action pointer.
    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;
    // Фактическая ширина рендера рамок.
    uint16_t frameWidth_{60};
    // Текст шапки экрана.
    std::string headerTitle_{"AVANTGARDE"};
    // Конфиг шагов pointer-редактирования.
    float speedStep_{0.05f};
    float bpmStep_{1.0f};
};

} // namespace avantgarde
