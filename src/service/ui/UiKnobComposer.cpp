#include "service/ui/UiKnobComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <array>

namespace avantgarde {

UiKnobComposer::UiKnobComposer(uint16_t width, uint16_t height)
    : width_(width),
      height_(height),
      lines_(height, std::string(width, ' ')) {}

void UiKnobComposer::drawKnob(const UiKnobModel& model) {
    if (width_ == 0 || height_ == 0) {
        return;
    }

    constexpr int kDialW = 7;
    constexpr int kDialH = 7;
    constexpr float kCx = 3.0f;
    constexpr float kCy = 3.0f;
    constexpr float kR = 3.0f;
    constexpr float kRingThickness = 0.8f;

    std::array<std::string, kDialH> dialRows{};
    for (int y = 0; y < kDialH; ++y) {
        dialRows[static_cast<std::size_t>(y)] = std::string(kDialW, ' ');
    }

    // Рисуем кольцо крутилки.
    for (int y = 0; y < kDialH; ++y) {
        for (int x = 0; x < kDialW; ++x) {
            const float dx = static_cast<float>(x) - kCx;
            const float dy = static_cast<float>(y) - kCy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(dist - kR) <= kRingThickness) {
                dialRows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                    model.selected ? '@' : 'o';
            }
        }
    }

    // Маркер (игла): угол от -135 до +135 градусов.
    const float clamped = std::clamp(model.value01, 0.0f, 1.0f);
    const float angleDeg = -135.0f + clamped * 270.0f;
    const float angleRad = angleDeg * 3.14159265358979323846f / 180.0f;
    const int x1 = static_cast<int>(std::lround(kCx + std::cos(angleRad) * 2.0f));
    const int y1 = static_cast<int>(std::lround(kCy + std::sin(angleRad) * 2.0f));

    // Простая линия центра к маркеру (DDA).
    constexpr int kSteps = 4;
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const int px = static_cast<int>(std::lround(kCx + (static_cast<float>(x1) - kCx) * t));
        const int py = static_cast<int>(std::lround(kCy + (static_cast<float>(y1) - kCy) * t));
        if (px >= 0 && px < kDialW && py >= 0 && py < kDialH) {
            dialRows[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = '*';
        }
    }
    // Центр крутилки фиксируем отдельно.
    dialRows[3][3] = model.selected ? '#' : '+';

    for (int y = 0; y < kDialH; ++y) {
        put_(model.x, static_cast<uint16_t>(model.y + y), dialRows[static_cast<std::size_t>(y)]);
    }

    std::string value = formatValue_(clamped);
    if (value.size() < kDialW) {
        const std::size_t padL = (kDialW - value.size()) / 2U;
        value = std::string(padL, ' ') + value;
        if (value.size() < kDialW) {
            value.append(kDialW - value.size(), ' ');
        }
    } else if (value.size() > kDialW) {
        value = value.substr(0, kDialW);
    }
    put_(model.x, static_cast<uint16_t>(model.y + kDialH), value);
    put_(model.x, static_cast<uint16_t>(model.y + kDialH + 1), fitLabel_(model.label, kDialW));
}

const std::vector<std::string>& UiKnobComposer::lines() const noexcept {
    return lines_;
}

std::string UiKnobComposer::formatValue_(float value01) {
    const float clamped = std::clamp(value01, 0.0f, 1.0f);
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%1.2f", clamped);
    std::string out = buf;
    if (out.size() < 5) {
        out.append(5U - out.size(), ' ');
    } else if (out.size() > 5) {
        out = out.substr(0, 5);
    }
    return out;
}

std::string UiKnobComposer::fitLabel_(const std::string& label, std::size_t width) {
    if (width == 0) {
        return {};
    }
    if (label.size() >= width) {
        return label.substr(0, width);
    }
    std::string out = label;
    out.append(width - out.size(), ' ');
    return out;
}

void UiKnobComposer::put_(uint16_t x, uint16_t y, const std::string& text) {
    if (y >= height_ || x >= width_) {
        return;
    }
    std::string& row = lines_[y];
    const std::size_t maxWrite = std::min<std::size_t>(text.size(), width_ - x);
    for (std::size_t i = 0; i < maxWrite; ++i) {
        row[x + i] = text[i];
    }
}

} // namespace avantgarde
