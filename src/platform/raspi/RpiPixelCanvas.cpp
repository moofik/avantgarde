#include "platform/raspi/RpiPixelCanvas.h"

#include <algorithm>
#include <cmath>

namespace avantgarde::raspi {
namespace {

uint8_t clampU8(int value) noexcept {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

void RpiPixelCanvas::resize(uint16_t w, uint16_t h) {
    width_ = std::max<uint16_t>(1U, w);
    height_ = std::max<uint16_t>(1U, h);
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), pack(RpiRgba{0, 0, 0, 255}));
}

void RpiPixelCanvas::clear(RpiRgba color) {
    std::fill(pixels_.begin(), pixels_.end(), pack(color));
}

void RpiPixelCanvas::fillRect(const RpiRectI& rect, RpiRgba color, float alpha01) {
    const RpiRectI clipped = clip(rect);
    if (clipped.w <= 0 || clipped.h <= 0) {
        return;
    }
    for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
        for (int x = clipped.x; x < clipped.x + clipped.w; ++x) {
            blendPixel(row + static_cast<std::size_t>(x), color, alpha01);
        }
    }
}

void RpiPixelCanvas::strokeRect(const RpiRectI& rect, RpiRgba color, int thickness, float alpha01) {
    if (thickness <= 0) {
        return;
    }
    const int t = std::max(1, thickness);
    fillRect(RpiRectI{rect.x, rect.y, rect.w, t}, color, alpha01);
    fillRect(RpiRectI{rect.x, rect.y + rect.h - t, rect.w, t}, color, alpha01);
    fillRect(RpiRectI{rect.x, rect.y, t, rect.h}, color, alpha01);
    fillRect(RpiRectI{rect.x + rect.w - t, rect.y, t, rect.h}, color, alpha01);
}

void RpiPixelCanvas::line(int x0, int y0, int x1, int y1, RpiRgba color, float alpha01) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        setPixel(x0, y0, color, alpha01);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void RpiPixelCanvas::circle(int cx, int cy, int r, RpiRgba color, float alpha01) {
    if (r <= 0) {
        return;
    }
    int x = r;
    int y = 0;
    int d = 1 - x;
    while (y <= x) {
        setPixel(cx + x, cy + y, color, alpha01);
        setPixel(cx + y, cy + x, color, alpha01);
        setPixel(cx - y, cy + x, color, alpha01);
        setPixel(cx - x, cy + y, color, alpha01);
        setPixel(cx - x, cy - y, color, alpha01);
        setPixel(cx - y, cy - x, color, alpha01);
        setPixel(cx + y, cy - x, color, alpha01);
        setPixel(cx + x, cy - y, color, alpha01);
        ++y;
        if (d <= 0) {
            d += 2 * y + 1;
        } else {
            --x;
            d += 2 * (y - x) + 1;
        }
    }
}

uint32_t RpiPixelCanvas::pack(RpiRgba c) noexcept {
    return (static_cast<uint32_t>(c.r) << 24U) |
           (static_cast<uint32_t>(c.g) << 16U) |
           (static_cast<uint32_t>(c.b) << 8U) |
           static_cast<uint32_t>(c.a);
}

RpiRgba RpiPixelCanvas::unpack(uint32_t v) noexcept {
    return RpiRgba{
        static_cast<uint8_t>((v >> 24U) & 0xFFU),
        static_cast<uint8_t>((v >> 16U) & 0xFFU),
        static_cast<uint8_t>((v >> 8U) & 0xFFU),
        static_cast<uint8_t>(v & 0xFFU)};
}

RpiRgba RpiPixelCanvas::blendOver(RpiRgba src, RpiRgba dst, float alpha01) noexcept {
    const float a = std::clamp(alpha01, 0.0f, 1.0f) * (static_cast<float>(src.a) / 255.0f);
    const float ia = 1.0f - a;
    return RpiRgba{
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.r) * a + static_cast<float>(dst.r) * ia))),
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.g) * a + static_cast<float>(dst.g) * ia))),
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.b) * a + static_cast<float>(dst.b) * ia))),
        255};
}

RpiRectI RpiPixelCanvas::clip(const RpiRectI& in) const noexcept {
    RpiRectI out = in;
    if (out.x < 0) {
        out.w += out.x;
        out.x = 0;
    }
    if (out.y < 0) {
        out.h += out.y;
        out.y = 0;
    }
    const int maxW = static_cast<int>(width_);
    const int maxH = static_cast<int>(height_);
    if (out.x + out.w > maxW) {
        out.w = maxW - out.x;
    }
    if (out.y + out.h > maxH) {
        out.h = maxH - out.y;
    }
    out.w = std::max(0, out.w);
    out.h = std::max(0, out.h);
    return out;
}

void RpiPixelCanvas::setPixel(int x, int y, RpiRgba color, float alpha01) {
    if (x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) {
        return;
    }
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    blendPixel(index, color, alpha01);
}

void RpiPixelCanvas::blendPixel(std::size_t index, RpiRgba color, float alpha01) {
    const RpiRgba dst = unpack(pixels_[index]);
    pixels_[index] = pack(blendOver(color, dst, alpha01));
}

} // namespace avantgarde::raspi
