#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/SamplerEngineLayer.h"
#include "app/HistoryTransactionManager.h"
#include "app/SamplerIoLayer.h"
#include "app/UiIntentApplier.h"
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

    // Применить intent виджета через ActionApplier и корректно записать в историю.
    void dispatchWidgetIntent_(const UiIntent& intent);
    // Пересчитать отображаемое состояние одного трека из transport + mute/clip данных.
    void refreshTrackViewState_(uint8_t track) noexcept;
    // Пересчитать состояния всех треков.
    void refreshAllTrackViewStates_() noexcept;
    // Один UI render-pass: telemetry + scene render + backend draw.
    void renderUiOnce_();
    // Обработка одного UI-жеста (клавиша/энкодер/кнопка).
    bool handleGesture_(UiGesture action);

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

    // Слой применения intent'ов к engine/ui-state (без знаний о ввода/сценах).
    UiIntentApplier intentApplier_{};
    // История и транзакции undo/redo.
    HistoryTransactionManager history_{4};

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
