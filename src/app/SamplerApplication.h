#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/SamplerEngineLayer.h"
#include "app/SamplerIoLayer.h"
#include "contracts/IPlatform.h"
#include "contracts/UiIntent.h"
#include "service/UiStateComposer.h"
#include "service/UiStateStore.h"
#include "service/ui/UiSceneHost.h"

namespace avantgarde {

// Конфигурация верхнего уровня для запуска приложения.
struct SamplerAppConfig {
    struct StartupClipLoad {
        // Индекс трека, в который нужно загрузить файл.
        uint8_t track{0};
        // Путь к аудиофайлу.
        std::string path{};
    };

    // Параметры аудио/движка.
    SamplerEngineConfig engine{};
    // Платформенный аудиохост (инжекция зависимости в engine-слой).
    std::shared_ptr<IAudioHost> audioHost{};
    // Параметры слоя ввода/вывода.
    SamplerIoConfig io{};
    // Стартовые загрузки клипов (произвольный список пар track/path).
    std::vector<StartupClipLoad> startupClipLoads{};
    // Базовая текстовая ширина GB-кадра.
    uint16_t gbTextWidth{60};
};

// Оркестратор приложения.
// Слой связывает IO и Engine через UI intents/state,
// чтобы они не зависели друг от друга напрямую.
class SamplerApplication {
public:
    SamplerApplication() = default;
    ~SamplerApplication();

    // Полный жизненный цикл приложения:
    // init -> start -> event loop -> stop.
    int run(const SamplerAppConfig& config);

private:
    // Ограничение UI-индекса трека в диапазон [0..N-1].
    uint8_t clampUiTrack_(uint8_t track) const noexcept;

    // Применение интентов, пришедших от scene-виджетов.
    // Возвращает true, если intent реально изменил состояние.
    bool applyIntent_(const UiIntent& intent);
    // Обертка над applyIntent_ с записью undo/redo истории.
    void dispatchIntent_(const UiIntent& intent);
    // Сформировать обратный intent (undo), используя текущее control-состояние.
    bool buildUndoIntent_(const UiIntent& forward, UiIntent& undoOut) const;
    // Выполнить один шаг undo/redo (возвращает true, если применилось изменение).
    bool undoLast_();
    bool redoLast_();
    // Сбросить историю undo/redo (например, при новом запуске).
    void clearHistory_() noexcept;
    // Пересчитать отображаемое состояние одного трека из transport + mute/clip данных.
    void refreshTrackViewState_(uint8_t track) noexcept;
    // Пересчитать состояния всех треков.
    void refreshAllTrackViewStates_() noexcept;
    // Один UI render-pass: telemetry + scene render + backend draw.
    void renderUiOnce_();
    // Обработка одного UI action (клавиша/кнопка).
    bool handleInputEvent_(UiInputAction action);

    // Аудио/RT слой.
    SamplerEngineLayer engine_{};
    // Ввод/рендер слой.
    SamplerIoLayer io_{};

    // Потокобезопасное состояние для UI.
    UiStateStore uiStore_{};
    // Слияние runtime telemetry в UI state snapshot.
    UiStateComposer uiComposer_{};
    // Роутер сцен и виджетов.
    UiSceneHost sceneHost_{};

    // Кэш track state на control-уровне.
    std::vector<UiTrackStateView> tracksCtl_{};
    // Кэш transport state на control-уровне.
    UiTransportState trCtl_{};

    // Пара интентов для истории:
    // undoIntent возвращает состояние назад, redoIntent повторяет исходное действие.
    struct UndoEntry {
        UiIntent undoIntent{};
        UiIntent redoIntent{};
    };
    static constexpr std::size_t kHistoryDepth_ = 4;
    std::deque<UndoEntry> undoStack_{};
    std::deque<UndoEntry> redoStack_{};

    // Синхронизация доступа к sceneHost_ между control/render потоками.
    std::mutex sceneMutex_{};
    // Глобальный флаг остановки фоновых циклов.
    std::atomic<bool> stopUi_{false};
    // Поток обработки input/actions.
    std::thread controlThread_{};
    // Поток периодического рендера для non-window режимов.
    std::thread uiThread_{};
};

} // namespace avantgarde
