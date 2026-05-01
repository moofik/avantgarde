#pragma once

#include <cstdint>
#include <vector>

namespace avantgarde::raspi {

struct RpiRgba {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

struct RpiRectI {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

class RpiPixelCanvas final {
public:
    void resize(uint16_t w, uint16_t h);

    [[nodiscard]] uint16_t width() const noexcept { return width_; }
    [[nodiscard]] uint16_t height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<uint32_t>& pixels() const noexcept { return pixels_; }

    void clear(RpiRgba color);
    void fillRect(const RpiRectI& rect, RpiRgba color, float alpha01 = 1.0f);
    void strokeRect(const RpiRectI& rect, RpiRgba color, int thickness = 1, float alpha01 = 1.0f);
    void line(int x0, int y0, int x1, int y1, RpiRgba color, float alpha01 = 1.0f);
    void circle(int cx, int cy, int r, RpiRgba color, float alpha01 = 1.0f);

private:
    static uint32_t pack(RpiRgba c) noexcept;
    static RpiRgba unpack(uint32_t v) noexcept;
    static RpiRgba blendOver(RpiRgba src, RpiRgba dst, float alpha01) noexcept;

    [[nodiscard]] RpiRectI clip(const RpiRectI& in) const noexcept;
    void setPixel(int x, int y, RpiRgba color, float alpha01);
    void blendPixel(std::size_t index, RpiRgba color, float alpha01);

    uint16_t width_{1};
    uint16_t height_{1};
    std::vector<uint32_t> pixels_{};
};

} // namespace avantgarde::raspi

