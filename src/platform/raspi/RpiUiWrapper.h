#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "contracts/IUi.h"
#include "contracts/IUiGestureInput.h"
#include "contracts/UiPreparedLayout.h"

namespace avantgarde::raspi {

// Конфигурация RPi-обертки UI.
// Это boundary-класс для будущих реальных backend-ов (framebuffer/DRM + GPIO/input).
struct RpiUiWrapperConfig {
    uint16_t width{640};
    uint16_t height{480};
    bool headless{true};
    // Путь к Linux evdev устройству (например /dev/input/event0).
    // Пустая строка отключает чтение физического ввода.
    std::string inputDevice{"/dev/input/event0"};
};

// RPi renderer-обертка (пока no-op/stub).
// Важно: интерфейс оставляем platform-neutral, чтобы позже заменить внутренности
// на DRM/SDL/FBO отрисовку без изменения app-слоя.
class RpiUiRenderer final : public IUiRenderer {
public:
    explicit RpiUiRenderer(RpiUiWrapperConfig config) noexcept;
    void render(const UiState& state) override;
    void renderPreparedLayout(const UiPreparedLayout& prepared) noexcept;

private:
    RpiUiWrapperConfig config_{};
    bool warned_{false};
};

// Источник low-level input для RPi.
// Сейчас это безопасный stub без внешних зависимостей.
class RpiPrimitiveInput final {
public:
    RpiPrimitiveInput() = default;
    ~RpiPrimitiveInput();

    bool init(const RpiUiWrapperConfig& config, std::string& errorOut);
    void shutdown() noexcept;

    bool pollPlatformEvents() noexcept;
    bool readNextInputEvent(PrimitiveInputEvent& out) noexcept;

    // Хелпер для тестов/эмулятора: положить сырой event вручную.
    void pushSynthetic(const PrimitiveInputEvent& ev);

private:
#if defined(__linux__)
    bool handleLinuxKeyEvent_(uint16_t code, int32_t value, uint64_t timestampMs) noexcept;
    PrimitiveControl mapLinuxKeyCode_(uint16_t code) const noexcept;
#endif
    std::mutex mutex_{};
    std::deque<PrimitiveInputEvent> queue_{};
#if defined(__linux__)
    int inputFd_{-1};
    bool shiftHeld_{false};
#endif
};

// Унифицированная обертка для подключения RPi UI backend в SamplerIoLayer.
class RpiUiWrapper final {
public:
    bool init(const RpiUiWrapperConfig& config, std::string& errorOut);

    bool pollEvents() noexcept;
    bool readNextInputEvent(PrimitiveInputEvent& out) noexcept;

    void render(const UiState& state, const UiPreparedLayout* preparedLayout);

private:
    RpiUiWrapperConfig config_{};
    RpiUiRenderer renderer_{RpiUiWrapperConfig{}};
    RpiPrimitiveInput input_{};
};

} // namespace avantgarde::raspi
