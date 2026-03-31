#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "app/SamplerEngineLayer.h"
#include "app/SamplerIoLayer.h"
#include "contracts/UiIntent.h"
#include "service/UiStateComposer.h"
#include "service/UiStateStore.h"
#include "service/ui/UiSceneHost.h"

namespace avantgarde {

// Конфигурация верхнего уровня для запуска приложения.
struct SamplerAppConfig {
    // Параметры аудио/движка.
    SamplerEngineConfig engine{};
    // Параметры слоя ввода/вывода.
    SamplerIoConfig io{};
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
    // Ограничение UI-индекса трека в диапазон [0..1].
    static uint8_t clampUiTrack_(uint8_t track) noexcept;

    // Применение интентов, пришедших от scene-виджетов.
    void handleIntent_(const UiIntent& intent);
    // Агрегированный флаг "хотя бы один трек проигрывается".
    bool recomputePlaying_() const noexcept;
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
    std::array<UiTrackStateView, 2> tracksCtl_{};
    // Кэш transport state на control-уровне.
    UiTransportState trCtl_{};

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
