#pragma once

#include <cstdint>
#include <string>

namespace avantgarde::raspi {

// Общая конфигурация RPi UI backend.
struct RpiUiConfig {
    uint16_t width{640};
    uint16_t height{480};
    // Поворот конечного кадра в градусах: 0 / 90 / 180 / 270.
    uint16_t rotateDeg{0};
    bool headless{true};
    // Путь к Linux evdev устройству (например /dev/input/event0).
    // Пустая строка отключает чтение физического ввода.
    std::string inputDevice{"/dev/input/event0"};
};

} // namespace avantgarde::raspi
