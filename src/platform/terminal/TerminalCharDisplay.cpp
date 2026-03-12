#include "platform/terminal/TerminalCharDisplay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <unistd.h>

namespace avantgarde {

TerminalCharDisplay::TerminalCharDisplay(uint16_t width, uint16_t height)
    : width_(std::max<uint16_t>(width, 16)),
      height_(std::max<uint16_t>(height, 8)),
      rows_(height_, std::string(width_, ' ')) {}

TerminalCharDisplay::~TerminalCharDisplay() {
    if (enteredAltScreen_) {
        std::printf("\x1b[?25h\x1b[?1049l");
        std::fflush(stdout);
    }
}

void TerminalCharDisplay::beginFrame() noexcept {
    if (ansiCapable_ || enteredAltScreen_) {
        return;
    }
    const char* term = std::getenv("TERM");
    ansiCapable_ = (isatty(fileno(stdout)) != 0) && term && std::string_view(term) != "dumb";
    if (ansiCapable_) {
        std::printf("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
        std::fflush(stdout);
        enteredAltScreen_ = true;
    }
}

void TerminalCharDisplay::clear(char fill) noexcept {
    for (auto& row : rows_) {
        std::fill(row.begin(), row.end(), fill);
    }
}

void TerminalCharDisplay::drawText(uint16_t x, uint16_t y, std::string_view text) noexcept {
    if (y >= height_ || x >= width_) {
        return;
    }
    std::string& row = rows_[y];
    const std::size_t maxLen = static_cast<std::size_t>(width_ - x);
    const std::size_t len = std::min(maxLen, text.size());
    row.replace(static_cast<std::size_t>(x), len, text.substr(0, len));
}

void TerminalCharDisplay::drawBar(uint16_t x, uint16_t y, uint16_t width, float value01) noexcept {
    if (y >= height_ || x >= width_) {
        return;
    }
    const uint16_t w = std::min<uint16_t>(width, static_cast<uint16_t>(width_ - x));
    if (w == 0) {
        return;
    }

    const float v = std::clamp(value01, 0.0f, 1.0f);
    const uint16_t filled = static_cast<uint16_t>(v * static_cast<float>(w));
    for (uint16_t i = 0; i < w; ++i) {
        rows_[y][x + i] = (i < filled) ? '#' : '.';
    }
}

void TerminalCharDisplay::present() noexcept {
    std::string frame;
    frame.reserve(static_cast<std::size_t>(height_) * (static_cast<std::size_t>(width_) + 1U));
    for (const auto& row : rows_) {
        frame += row;
        frame.push_back('\n');
    }
    if (frame == lastFrame_) {
        return;
    }
    lastFrame_ = frame;

    if (ansiCapable_) {
        std::printf("\x1b[H");
        std::printf("%s", frame.c_str());
    } else {
        // Fallback mode for consoles without ANSI cursor control.
        std::printf("\n----- UI FRAME -----\n%s", frame.c_str());
    }
    std::fflush(stdout);
}

} // namespace avantgarde
