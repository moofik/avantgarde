#include "platform/render/GlowVisualFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string_view>
#include <vector>

namespace avantgarde {
namespace {

constexpr float kPi = 3.14159265358979323846f;

uint64_t mix64(uint64_t x) {
    x ^= x >> 33U;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33U;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33U;
    return x;
}

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

bool parseHexColor01(std::string_view raw, float& r, float& g, float& b, float& a) {
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
    uint8_t rr = 0, gg = 0, bb = 0, aa = 255;
    if (!byteAt(0, rr) || !byteAt(2, gg) || !byteAt(4, bb)) {
        return false;
    }
    if (s.size() == 8U && !byteAt(6, aa)) {
        return false;
    }
    r = static_cast<float>(rr) / 255.0f;
    g = static_cast<float>(gg) / 255.0f;
    b = static_cast<float>(bb) / 255.0f;
    a = static_cast<float>(aa) / 255.0f;
    return true;
}

} // namespace

std::string GlowVisualFx::buildStateKey_(const VisualFxRequest& request) const {
    if (!request.instanceKey.empty()) {
        return request.instanceKey;
    }
    if (!request.nodeId.empty()) {
        return request.nodeId;
    }
    if (!request.nodeText.empty()) {
        return request.nodeText;
    }
    return "__fx_glow__";
}

VisualFxBlockStyle GlowVisualFx::resolve(const VisualFxRequest& request) {
    VisualFxBlockStyle style{};
    GlowVisualFxPayload payload{};
    const std::string key = buildStateKey_(request);

    float amount = request.effectAmount;
    if (amount <= 0.0f) {
        // Для glow по умолчанию делаем более заметный уровень,
        // иначе на low-contrast UI эффект почти не читается.
        amount = 0.32f;
    }
    amount = std::clamp(amount, 0.05f, 1.0f);

    float speed = request.effectSpeed;
    if (speed <= 0.0f) {
        speed = 1.0f;
    }
    speed = std::clamp(speed, 0.05f, 8.0f);

    bool active = false;
    float phase01 = 0.0f;

    const bool changeTrigger = (toLowerAscii(request.effectTrigger) == "change");
    if (changeTrigger) {
        // Для trigger=change эффект разрешен только при наличии управляющего value.
        // Иначе (например статичный text без bind) НЕ уходим в time-mode, чтобы
        // не появлялось бесконечное мигание после единичного триггера.
        if (!request.hasValue01) {
            return style;
        }
            const float value01 = std::clamp(request.value01, 0.0f, 1.0f);
            const auto prevIt = lastValue01_.find(key);
            if (prevIt == lastValue01_.end()) {
                // Первый кадр только задает baseline: без автозапуска эффекта.
                lastValue01_[key] = value01;
            } else {
                const bool changed = (std::fabs(prevIt->second - value01) > 0.0005f);
                prevIt->second = value01;
                if (changed) {
                    lastChangeMs_[key] = request.nowMs;
                }
            }

            const uint64_t holdBase = std::clamp<uint64_t>(
                static_cast<uint64_t>(request.effectTriggerOutMs > 0U ? request.effectTriggerOutMs : 1400U),
                10ULL,
                120000ULL);
            const uint64_t holdMs = std::clamp<uint64_t>(
                static_cast<uint64_t>(std::llround(static_cast<double>(holdBase) / static_cast<double>(speed))),
                10ULL,
                120000ULL);
            if (const auto tIt = lastChangeMs_.find(key); tIt != lastChangeMs_.end()) {
                const uint64_t dt = (request.nowMs >= tIt->second) ? (request.nowMs - tIt->second) : 0ULL;
                active = (dt <= holdMs);
                if (active) {
                    phase01 = static_cast<float>(static_cast<double>(dt) / static_cast<double>(holdMs));
                }
            }
    } else {
        uint32_t intervalMs = (request.effectIntervalMs > 0U) ? request.effectIntervalMs : 2400U;
        intervalMs = std::max<uint32_t>(320U, intervalMs);
        const uint64_t interval = static_cast<uint64_t>(intervalMs);

        auto [epochIt, inserted] = timeEpochMs_.emplace(key, request.nowMs);
        if (inserted) {
            epochIt->second = request.nowMs;
        }
        const uint64_t elapsed = (request.nowMs >= epochIt->second) ? (request.nowMs - epochIt->second) : 0ULL;
        if (elapsed < interval) {
            return style;
        }

        const uint64_t elapsedAfterWarmup = elapsed - interval;
        const uint64_t phase = (interval > 0U) ? (elapsedAfterWarmup % interval) : 0ULL;

        const double slowFactor = 1.0 / static_cast<double>(speed);
        uint64_t burstMs = std::clamp<uint64_t>(
            static_cast<uint64_t>(std::llround((320.0 + static_cast<double>(amount) * 420.0) *
                                               (0.75 + 0.60 * slowFactor))),
            180ULL,
            2600ULL);
        if (interval > 1U) {
            burstMs = std::min<uint64_t>(burstMs, interval - 1U);
        }

        active = (phase < burstMs);
        if (active) {
            phase01 = static_cast<float>(static_cast<double>(phase) /
                                         static_cast<double>(std::max<uint64_t>(1ULL, burstMs)));
        }
    }

    if (!active) {
        return style;
    }

    phase01 = std::clamp(phase01, 0.0f, 1.0f);
    const float pulse = 0.5f + 0.5f * std::sin(phase01 * kPi);
    const uint64_t seed = static_cast<uint64_t>(std::hash<std::string>{}(key));
    const float seedPhase = static_cast<float>(mix64(seed) & 0xFFFFULL) / 65535.0f * 2.0f * kPi;
    const float t = static_cast<float>(static_cast<double>(request.nowMs) * 0.001 *
                                       static_cast<double>(speed));

    payload.phase01 = phase01;
    payload.offsetX = std::sin(t * 2.0f * kPi * 0.85f + seedPhase) * amount * 0.06f;
    // splitPx для glow интерпретируется как радиус "ореола",
    // поэтому держим диапазон заметно выше, чем у glitch.
    payload.radiusPx = 1.20f + amount * (1.70f + 1.25f * pulse);
    payload.alpha = std::clamp(0.82f + 0.10f * pulse + 0.15f * amount, 0.50f, 1.0f);
    const float env = std::clamp(std::sin(phase01 * kPi), 0.0f, 1.0f);
    const float envSmooth = env * env * (3.0f - 2.0f * env);
    const float radiusBase = std::clamp(payload.radiusPx * (0.62f + 0.40f * payload.alpha), 0.7f, 4.0f);
    const float radius = std::max(0.6f, radiusBase * (0.65f + 1.05f * envSmooth));
    payload.nearRadiusPx = radius * 0.80f;
    payload.farRadiusPx = radius * 1.35f;
    payload.nearAlpha =
        std::clamp((0.04f + 0.11f * payload.alpha) * (0.35f + 0.85f * envSmooth), 0.01f, 0.22f);
    payload.farAlpha =
        std::clamp((0.02f + 0.07f * payload.alpha) * (0.30f + 0.75f * envSmooth), 0.005f, 0.14f);
    float cr = 0.0f, cg = 0.0f, cb = 0.0f, ca = 1.0f;
    if (parseHexColor01(request.effectColor, cr, cg, cb, ca)) {
        payload.hasTint = true;
        payload.tintR = cr;
        payload.tintG = cg;
        payload.tintB = cb;
        payload.tintA = std::clamp(ca, 0.0f, 1.0f);
    }
    style.active = true;
    style.payload = payload;
    return style;
}

bool GlowVisualFx::applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) {
    if (!view.pixels || view.width == 0U || view.height == 0U || view.strideBytes < 4U) {
        return false;
    }
    const VisualFxBlockStyle style = resolve(request);
    if (!style.active) {
        return false;
    }
    const auto* glow = std::get_if<GlowVisualFxPayload>(&style.payload);
    if (!glow) {
        return false;
    }

    const float tr = glow->hasTint ? std::clamp(glow->tintR, 0.0f, 1.0f) : 1.0f;
    const float tg = glow->hasTint ? std::clamp(glow->tintG, 0.0f, 1.0f) : 1.0f;
    const float tb = glow->hasTint ? std::clamp(glow->tintB, 0.0f, 1.0f) : 1.0f;
    const float ta = glow->hasTint ? std::clamp(glow->tintA, 0.0f, 1.0f) : 1.0f;

    const int farR = std::clamp<int>(static_cast<int>(std::lround(glow->farRadiusPx)), 1, 10);
    const int nearR = std::clamp<int>(static_cast<int>(std::lround(glow->nearRadiusPx)), 1, 10);
    const float farA = std::clamp(glow->farAlpha * ta, 0.0f, 1.0f);
    const float nearA = std::clamp(glow->nearAlpha * ta, 0.0f, 1.0f);

    const std::size_t bytes = static_cast<std::size_t>(view.strideBytes) * view.height;
    std::vector<uint8_t> src(bytes, 0U);
    std::copy(view.pixels, view.pixels + bytes, src.begin());

    auto addHalo = [&](int radius, float alphaMul) {
        if (radius <= 0 || alphaMul <= 0.0f) {
            return;
        }
        for (uint16_t y = 0; y < view.height; ++y) {
            for (uint16_t x = 0; x < view.width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * view.strideBytes + static_cast<std::size_t>(x) * 4U;
                if (src[idx + 3U] == 0U) {
                    continue;
                }
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int yy = static_cast<int>(y) + dy;
                    if (yy < 0 || yy >= static_cast<int>(view.height)) {
                        continue;
                    }
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int xx = static_cast<int>(x) + dx;
                        if (xx < 0 || xx >= static_cast<int>(view.width)) {
                            continue;
                        }
                        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        if (d > static_cast<float>(radius)) {
                            continue;
                        }
                        const float falloff = 1.0f - (d / static_cast<float>(radius));
                        const float a = alphaMul * falloff * 0.55f;
                        if (a <= 0.001f) {
                            continue;
                        }
                        const std::size_t didx =
                            static_cast<std::size_t>(yy) * view.strideBytes + static_cast<std::size_t>(xx) * 4U;
                        // Glow должен оставлять исходный текст/иконку резкими и не перекрашивать core:
                        // рисуем ореол только по фону вокруг непрозрачного source.
                        if (src[didx + 3U] != 0U) {
                            continue;
                        }
                        const float rr = static_cast<float>(view.pixels[didx + 0U]) + 255.0f * tr * a;
                        const float gg = static_cast<float>(view.pixels[didx + 1U]) + 255.0f * tg * a;
                        const float bb = static_cast<float>(view.pixels[didx + 2U]) + 255.0f * tb * a;
                        const float aa = static_cast<float>(view.pixels[didx + 3U]) + 255.0f * a * 0.4f;
                        view.pixels[didx + 0U] = static_cast<uint8_t>(std::clamp(rr, 0.0f, 255.0f));
                        view.pixels[didx + 1U] = static_cast<uint8_t>(std::clamp(gg, 0.0f, 255.0f));
                        view.pixels[didx + 2U] = static_cast<uint8_t>(std::clamp(bb, 0.0f, 255.0f));
                        view.pixels[didx + 3U] = static_cast<uint8_t>(std::clamp(aa, 0.0f, 255.0f));
                    }
                }
            }
        }
    };

    addHalo(farR, farA);
    addHalo(nearR, nearA);
    return true;
}

} // namespace avantgarde
