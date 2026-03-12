#pragma once

#include <cstdint>
#include <string_view>

namespace avantgarde {

// Minimal low-res display abstraction. Concrete adapters can target
// terminal preview, SPI LCD/OLED on Raspberry Pi, etc.
struct IDisplay {
    virtual ~IDisplay() = default;

    virtual uint16_t width() const noexcept = 0;
    virtual uint16_t height() const noexcept = 0;

    virtual void beginFrame() noexcept = 0;
    virtual void clear(char fill = ' ') noexcept = 0;
    virtual void drawText(uint16_t x, uint16_t y, std::string_view text) noexcept = 0;
    virtual void drawBar(uint16_t x, uint16_t y, uint16_t width, float value01) noexcept = 0;
    virtual void present() noexcept = 0;
};

} // namespace avantgarde
