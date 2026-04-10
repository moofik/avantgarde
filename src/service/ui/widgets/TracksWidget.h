#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

// Виджет основного экрана (транспорт + треки).
// Отрисовка делегирована prepared-layout рендереру, но точка входа в scene-layer
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
        // Декларативный JSON-лейаут (опционально).
        std::optional<UiLayoutTemplate> layoutTemplate{};
    };

    TracksWidget() noexcept;
    // Ctor с явной конфигурацией кадра/заголовка.
    explicit TracksWidget(const Options& options) noexcept;

    // Стабильный идентификатор виджета для хоста сцен.
    const char* id() const noexcept override;
    // Подготовка vNext layout-модели (без геометрии и рендера).
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    // Обработка input для экрана треков (пока без локальных интентов).
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    // Возвращает scene-local action catalog (tracks screen).
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    // Маппит action (+nav/ui state) в intents.
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    struct LayoutModel {
        // true, если модель собрана из JSON-шаблона.
        bool enabled{false};
        // Кастомный заголовок главной рамки.
        std::string title{};
        // Кастомная строка подсказки клавиш.
        std::string keysHint{};
        // Канонические write-target для scene действий.
        std::string targetTrackSpeed{"param.track.selected.speed"};
        std::string targetTrackGain{"param.track.selected.gain"};
        std::string targetTrackPlaybackProfile{"param.track.selected.playback_profile"};
        std::string targetTrackMute{"param.track.selected.mute"};
        std::string targetTrackArm{"param.track.selected.arm"};
        std::string targetTransportBpm{"param.transport.bpm"};
        std::string targetTransportQuant{"param.transport.quant"};
    };

    // Формирование строки статуса активного action pointer.
    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;
    // Подготовить карту параметров кадра (bind/id -> value) для composer-слоя.
    UiPreparedParams buildPreparedLayoutParams_(const UiState& rtState, const UiNavState& navState) const;
    // Построить runtime-модель из JSON-шаблона tracks-экрана.
    void buildLayoutModel_(const UiLayoutTemplate& tpl);
    // Фактическая ширина рендера рамок.
    uint16_t frameWidth_{60};
    // Текст шапки экрана.
    std::string headerTitle_{"AVANTGARDE"};
    // Конфиг шагов pointer-редактирования.
    float bpmStep_{1.0f};
    // Runtime-модель JSON-представления.
    LayoutModel layout_{};
    // Полный шаблон layout-дерева для fallback renderer.
    std::optional<UiLayoutTemplate> layoutTemplate_{};
};

} // namespace avantgarde
