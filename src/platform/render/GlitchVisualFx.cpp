#include "platform/render/GlitchVisualFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace avantgarde {
namespace {

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

} // namespace

std::string GlitchVisualFx::buildStateKey_(const VisualFxRequest& request) const {
    if (!request.instanceKey.empty()) {
        return request.instanceKey;
    }
    if (!request.nodeId.empty()) {
        return request.nodeId;
    }
    if (!request.nodeText.empty()) {
        return request.nodeText;
    }
    return "__fx__";
}

VisualFxBlockStyle GlitchVisualFx::resolve(const VisualFxRequest& request) {
    VisualFxBlockStyle style{};
    const std::string key = buildStateKey_(request);

    uint32_t intervalMs = (request.effectIntervalMs > 0U) ? request.effectIntervalMs : 2200U;
    intervalMs = std::max<uint32_t>(420U, intervalMs);
    const uint64_t interval = static_cast<uint64_t>(intervalMs);

    float amount = request.effectAmount;
    if (amount <= 0.0f) {
        amount = 0.22f;
    }
    amount = std::clamp(amount, 0.05f, 1.0f);

    float speed = request.effectSpeed;
    if (speed <= 0.0f) {
        speed = 1.0f;
    }
    speed = std::clamp(speed, 0.01f, 8.0f);

    bool active = false;
    const std::string trigger = toLowerAscii(request.effectTrigger);
    if (trigger == "change") {
        if (request.hasValue01) {
            const float value01 = std::clamp(request.value01, 0.0f, 1.0f);
            const auto prevIt = lastValue01_.find(key);
            const bool changed = (prevIt == lastValue01_.end()) ||
                                 (std::fabs(prevIt->second - value01) > 0.0005f);
            lastValue01_[key] = value01;
            if (changed) {
                lastChangeMs_[key] = request.nowMs;
            }

            // В режиме change длительность удержания после последнего изменения
            // должна быть предсказуемой и задаваться из layout (effect_trigger_out).
            const uint64_t holdMs = std::clamp<uint64_t>(
                static_cast<uint64_t>(request.effectTriggerOutMs > 0U ? request.effectTriggerOutMs
                                                                       : 1000U),
                10ULL,
                120000ULL);
            const auto tIt = lastChangeMs_.find(key);
            if (tIt != lastChangeMs_.end()) {
                const uint64_t dt =
                    (request.nowMs >= tIt->second) ? (request.nowMs - tIt->second) : 0ULL;
                active = (dt <= holdMs);
            }
        }
    } else {
        // time/always trigger:
        // interval задает только период старта, а длительность вспышки вычисляется отдельно.
        auto [epochIt, inserted] = timeEpochMs_.emplace(key, request.nowMs);
        if (inserted) {
            epochIt->second = request.nowMs;
        }
        const uint64_t elapsed =
            (request.nowMs >= epochIt->second) ? (request.nowMs - epochIt->second) : 0ULL;
        const uint64_t phase = (interval > 0U) ? (elapsed % interval) : 0ULL;

        const double slowFactor = 1.0 / static_cast<double>(speed);
        uint64_t burstMs = std::clamp<uint64_t>(
            static_cast<uint64_t>(std::llround((120.0 + static_cast<double>(amount) * 280.0) *
                                               (0.75 + 0.50 * slowFactor))),
            80ULL,
            1400ULL);
        if (interval > 1U) {
            burstMs = std::min<uint64_t>(burstMs, interval - 1U);
        }
        active = (phase < burstMs);
    }

    if (!active) {
        return style;
    }

    style.active = true;
    const uint64_t stateStepMs = std::clamp<uint64_t>(
        static_cast<uint64_t>(std::llround(300.0 / static_cast<double>(speed))),
        40ULL,
        4000ULL);
    const uint64_t tick = request.nowMs / stateStepMs;
    const uint64_t seed = static_cast<uint64_t>(std::hash<std::string>{}(key));
    const auto rnd01 = [&](uint64_t salt) -> float {
        const uint64_t mixed = mix64(seed ^ (tick + salt * 0x9e3779b97f4a7c15ULL));
        return static_cast<float>(mixed & 0xFFFFULL) / 65535.0f;
    };

    style.offsetX = (rnd01(11U) - 0.5f) * amount * 0.85f;
    style.offsetY = 0.0f;
    style.splitPx = 0.45f + amount * 1.35f;
    style.alpha = 0.88f + rnd01(3U) * 0.12f;
    style.sliceCount = (amount < 0.20f) ? 2U : ((amount < 0.55f) ? 3U : 4U);
    style.alternatePhase = ((tick & 1ULL) != 0ULL);
    return style;
}

} // namespace avantgarde
