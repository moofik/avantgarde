#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "contracts/IDisplay.h"
#include "contracts/IUi.h"
#include "contracts/IUiInput.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class MacGbWindowRenderer;

// Режимы UI backend.
enum class SamplerUiMode : uint8_t {
    Ansi = 0,
    LowRes,
    Gb,
    GbWindow
};

// Парсинг CLI-строки режима UI.
bool parseSamplerUiMode(std::string_view raw, SamplerUiMode& out) noexcept;

// Конфигурация IO-слоя.
struct SamplerIoConfig {
    // Выбранный backend рендера.
    SamplerUiMode mode{SamplerUiMode::Ansi};
    // Явно заданная тема.
    UiTheme theme{UiTheme::Default};
    // true, если тема пришла из CLI.
    bool themeProvided{false};
    // Текстовая ширина GB-кадра.
    uint16_t gbTextWidth{60};
};

// Слой ввода/вывода:
// - читает input события (terminal/window)
// - рисует кадр в выбранный backend
class SamplerIoLayer {
public:
    SamplerIoLayer() = default;
    ~SamplerIoLayer();

    // Инициализация renderer backend по конфигу.
    bool init(const SamplerIoConfig& config, std::string& errorOut);
    // Запуск фонового terminal input потока.
    void startTerminalInput(std::atomic<bool>& stopFlag);
    // Остановка terminal input потока.
    void stopTerminalInput() noexcept;

    // Pump оконных событий (актуально для gb-window).
    bool pumpWindowInput();
    // Получить следующее накопленное input событие.
    bool pollInput(UiInputEvent& out);

    // true, если рендер нужно выполнять в main thread (AppKit).
    bool renderOnMainThread() const noexcept;
    // Рендер готового состояния/кадра.
    void render(const UiState& state, const std::string& sceneFrame, bool showHeaderOverlay);

private:
    // Простейшая SPSC-подобная очередь для handoff input событий между потоками.
    struct InputEventQueue final {
        void push(const UiInputEvent& ev);
        bool tryPop(UiInputEvent& out);

        // Защита внутренней очереди.
        std::mutex mutex_{};
        // Накопленные input события.
        std::deque<UiInputEvent> queue_{};
    };

    // Очередь входных событий из разных источников.
    InputEventQueue inputQueue_{};
    // Display нужен lowres backend'у.
    std::unique_ptr<IDisplay> display_{};
    // Текущий renderer backend.
    std::unique_ptr<IUiRenderer> renderer_{};
    // Фоновый поток terminal input.
    std::thread terminalInputThread_{};

    // Кэш указателя на window renderer для fast-path window input.
    MacGbWindowRenderer* windowRenderer_{nullptr};
};

} // namespace avantgarde
