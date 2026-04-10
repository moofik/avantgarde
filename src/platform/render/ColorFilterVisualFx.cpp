#include "platform/render/ColorFilterVisualFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace avantgarde {
namespace {

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parseHexColor01(std::string_view raw, float& r, float& g, float& b) {
    if (raw.empty()) {
        return false;
    }
    std::string s(raw);
    if (!s.empty() && s.front() == '#') {
        s.erase(s.begin());
    }
    if (!(s.size() == 6U || s.size() == 8U)) {
        return false;
    }
    auto byteAt = [&](std::size_t i, uint8_t& out) -> bool {
        const int hi = hexNibble(s[i]);
        const int lo = hexNibble(s[i + 1U]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    };
    uint8_t rr = 0U;
    uint8_t gg = 0U;
    uint8_t bb = 0U;
    if (!byteAt(0, rr) || !byteAt(2, gg) || !byteAt(4, bb)) {
        return false;
    }
    r = static_cast<float>(rr) / 255.0f;
    g = static_cast<float>(gg) / 255.0f;
    b = static_cast<float>(bb) / 255.0f;
    return true;
}

} // namespace

std::string ColorFilterVisualFx::buildStateKey_(const VisualFxRequest& request) const {
    if (!request.instanceKey.empty()) {
        return request.instanceKey;
    }
    if (!request.nodeId.empty()) {
        return request.nodeId;
    }
    if (!request.nodeText.empty()) {
        return request.nodeText;
    }
    return "__fx_color_filter__";
}

VisualFxBlockStyle ColorFilterVisualFx::resolve(const VisualFxRequest& request) {
    VisualFxBlockStyle style{};

    const std::string key = buildStateKey_(request);
    const std::string trigger = toLowerAscii(request.effectTrigger);
    const float speed = std::clamp(request.effectSpeed <= 0.0f ? 1.0f : request.effectSpeed, 0.05f, 8.0f);
    bool active = false;

    if (trigger == "change") {
        if (!request.hasValue01) {
            return style;
        }
        const float value01 = std::clamp(request.value01, 0.0f, 1.0f);
        const auto prevIt = lastValue01_.find(key);
        if (prevIt == lastValue01_.end()) {
            // Первый кадр только инициализирует baseline.
            lastValue01_[key] = value01;
        } else {
            const bool changed = (std::fabs(prevIt->second - value01) > 0.0005f);
            prevIt->second = value01;
            if (changed) {
                lastChangeMs_[key] = request.nowMs;
            }
        }
        const uint64_t holdBase = std::clamp<uint64_t>(
            static_cast<uint64_t>(request.effectTriggerOutMs > 0U ? request.effectTriggerOutMs : 1200U),
            10ULL,
            120000ULL);
        const uint64_t holdMs = std::clamp<uint64_t>(
            static_cast<uint64_t>(std::llround(static_cast<double>(holdBase) / static_cast<double>(speed))),
            10ULL,
            120000ULL);
        if (const auto tIt = lastChangeMs_.find(key); tIt != lastChangeMs_.end()) {
            const uint64_t dt = (request.nowMs >= tIt->second) ? (request.nowMs - tIt->second) : 0ULL;
            active = (dt <= holdMs);
        }
    } else if (trigger == "time") {
        uint32_t intervalMs = (request.effectIntervalMs > 0U) ? request.effectIntervalMs : 2200U;
        intervalMs = std::max<uint32_t>(120U, intervalMs);
        const uint64_t interval = static_cast<uint64_t>(intervalMs);
        auto [epochIt, inserted] = timeEpochMs_.emplace(key, request.nowMs);
        if (inserted) {
            epochIt->second = request.nowMs;
        }
        const uint64_t elapsed = (request.nowMs >= epochIt->second) ? (request.nowMs - epochIt->second) : 0ULL;
        if (elapsed < interval) {
            return style;
        }
        const uint64_t phase = (interval > 0U) ? ((elapsed - interval) % interval) : 0ULL;
        uint64_t burstMs = std::clamp<uint64_t>(
            static_cast<uint64_t>(std::llround(320.0 / static_cast<double>(speed))),
            80ULL,
            2000ULL);
        if (interval > 1U) {
            burstMs = std::min<uint64_t>(burstMs, interval - 1U);
        }
        active = (phase < burstMs);
    } else {
        // always / empty trigger
        active = true;
    }

    if (active) {
        style.active = true;
    }
    return style;
}

bool ColorFilterVisualFx::applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) {
    if (!view.pixels || view.width == 0U || view.height == 0U || view.strideBytes < view.width * 4U) {
        return false;
    }
    const VisualFxBlockStyle style = resolve(request);
    if (!style.active) {
        return false;
    }

    float amount = request.effectAmount;
    if (amount <= 0.0f) {
        amount = 1.0f;
    }
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0001f) {
        return false;
    }

    // По умолчанию - нейтральный серый.
    float tintR = 0.5f;
    float tintG = 0.5f;
    float tintB = 0.5f;
    (void)parseHexColor01(request.effectColor, tintR, tintG, tintB);
    tintR = std::clamp(tintR, 0.0f, 1.0f);
    tintG = std::clamp(tintG, 0.0f, 1.0f);
    tintB = std::clamp(tintB, 0.0f, 1.0f);

    bool changed = false;
    for (uint16_t y = 0U; y < view.height; ++y) {
        uint8_t* row = view.pixels + static_cast<std::size_t>(y) * view.strideBytes;
        for (uint16_t x = 0U; x < view.width; ++x) {
            uint8_t* px = row + static_cast<std::size_t>(x) * 4U;
            const float a = static_cast<float>(px[3U]) / 255.0f;
            if (a <= 0.0f) {
                continue;
            }
            const float r = static_cast<float>(px[0U]) / 255.0f;
            const float g = static_cast<float>(px[1U]) / 255.0f;
            const float b = static_cast<float>(px[2U]) / 255.0f;
            const float luma = std::clamp(0.299f * r + 0.587f * g + 0.114f * b, 0.0f, 1.0f);
            const float targetR = luma * tintR;
            const float targetG = luma * tintG;
            const float targetB = luma * tintB;
            const float outR = r + (targetR - r) * amount;
            const float outG = g + (targetG - g) * amount;
            const float outB = b + (targetB - b) * amount;
            const uint8_t rr = static_cast<uint8_t>(std::lround(std::clamp(outR, 0.0f, 1.0f) * 255.0f));
            const uint8_t gg = static_cast<uint8_t>(std::lround(std::clamp(outG, 0.0f, 1.0f) * 255.0f));
            const uint8_t bb = static_cast<uint8_t>(std::lround(std::clamp(outB, 0.0f, 1.0f) * 255.0f));
            changed = changed || rr != px[0U] || gg != px[1U] || bb != px[2U];
            px[0U] = rr;
            px[1U] = gg;
            px[2U] = bb;
        }
    }
    return changed;
}

} // namespace avantgarde

