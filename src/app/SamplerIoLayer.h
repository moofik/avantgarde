#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "contracts/IUi.h"
#include "contracts/IUiGestureInput.h"
#include "contracts/UiPreparedLayout.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class MacPrimitiveWindowRenderer;
namespace macos {
class MacPrimitiveWindowInput;
}

// Режимы UI backend.
enum class SamplerUiMode : uint8_t {
    GbWindow = 0
};

// Парсинг CLI-строки режима UI.
bool parseSamplerUiMode(std::string_view raw, SamplerUiMode& out) noexcept;

// Конфигурация IO-слоя.
struct SamplerIoConfig {
    // Выбранный backend рендера.
    SamplerUiMode mode{SamplerUiMode::GbWindow};
    // Явно заданная тема.
    UiTheme theme{UiTheme::Default};
    // true, если тема пришла из CLI.
    bool themeProvided{false};
};

// Слой ввода/вывода:
// - читает input события окна
// - рисует кадр в выбранный backend
class SamplerIoLayer {
public:
    SamplerIoLayer();
    ~SamplerIoLayer();

    // Инициализация renderer backend по конфигу.
    bool init(const SamplerIoConfig& config, std::string& errorOut);

    // Считать накопленные события окна и положить их в общую очередь.
    bool readWindowEvents();
    // Получить следующее событие ввода из общей очереди.
    bool readNextInputEvent(UiGestureEvent& out);

    // Рендер готового состояния/кадра.
    void render(const UiState& state,
                const UiPreparedLayout* preparedLayout);

private:
    // Простейшая SPSC-подобная очередь для handoff input событий между потоками.
    struct InputEventQueue final {
        void push(const UiGestureEvent& ev);
        bool tryPop(UiGestureEvent& out);

        // Защита внутренней очереди.
        std::mutex mutex_{};
        // Накопленные input события.
        std::deque<UiGestureEvent> queue_{};
    };

    // Очередь входных событий из разных источников.
    InputEventQueue inputQueue_{};
    // Текущий renderer backend.
    std::unique_ptr<IUiRenderer> renderer_{};

    // Кэш указателя на window renderer для fast-path window input.
    MacPrimitiveWindowRenderer* windowRenderer_{nullptr};
    // Отдельный сборщик input для window режима (не в renderer).
    std::unique_ptr<macos::MacPrimitiveWindowInput> windowInput_{};
};

} // namespace avantgarde
