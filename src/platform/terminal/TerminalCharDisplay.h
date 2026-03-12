#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "contracts/IDisplay.h"

namespace avantgarde {

class TerminalCharDisplay final : public IDisplay {
public:
    explicit TerminalCharDisplay(uint16_t width = 64, uint16_t height = 16);
    ~TerminalCharDisplay() override;

    uint16_t width() const noexcept override { return width_; }
    uint16_t height() const noexcept override { return height_; }

    void beginFrame() noexcept override;
    void clear(char fill = ' ') noexcept override;
    void drawText(uint16_t x, uint16_t y, std::string_view text) noexcept override;
    void drawBar(uint16_t x, uint16_t y, uint16_t width, float value01) noexcept override;
    void present() noexcept override;

private:
    uint16_t width_{0};
    uint16_t height_{0};
    bool ansiCapable_{false};
    bool enteredAltScreen_{false};
    std::vector<std::string> rows_;
    std::string lastFrame_;
};

} // namespace avantgarde
