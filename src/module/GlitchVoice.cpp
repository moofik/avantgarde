#include "module/GlitchVoice.h"

#include <cmath>

namespace avantgarde {

float GlitchVoice::renderSample(const GlitchVoiceState& voice,
                                const std::vector<float>& ring,
                                uint32_t ringSize) noexcept {
    if (!voice.active || voice.subsliceLength == 0U || ring.empty() || ringSize == 0U) {
        return 0.0f;
    }
    const float phase = wrapPos_(voice.localPlayhead, voice.subsliceLength);
    const float pos = voice.reverse
                          ? static_cast<float>(voice.subsliceStart) +
                                (static_cast<float>(voice.subsliceLength) - 1.0f - phase)
                          : static_cast<float>(voice.subsliceStart) + phase;
    return readCubic_(ring, pos, ringSize);
}

void GlitchVoice::advance(GlitchVoiceState& voice, uint32_t ringCapacity) noexcept {
    if (!voice.active) {
        return;
    }
    voice.localPlayhead += std::max(0.01f, voice.speed);
    while (voice.localPlayhead >= static_cast<float>(voice.subsliceLength)) {
        voice.localPlayhead -= static_cast<float>(voice.subsliceLength);
        if (!voice.choppy) {
            const uint32_t relStart = wrapIndex_(
                static_cast<int64_t>(voice.subsliceStart) - static_cast<int64_t>(voice.phraseStart),
                voice.phraseLength);
            const uint32_t nextRel = wrapIndex_(static_cast<int64_t>(relStart) + static_cast<int64_t>(voice.subsliceLength),
                                                voice.phraseLength);
            voice.subsliceStart = wrapIndex_(
                static_cast<int64_t>(voice.phraseStart) + static_cast<int64_t>(nextRel),
                ringCapacity);
        }
    }
}

float GlitchVoice::wrapPos_(float pos, uint32_t size) noexcept {
    if (size == 0U) {
        return 0.0f;
    }
    const float f = std::fmod(pos, static_cast<float>(size));
    return (f < 0.0f) ? (f + static_cast<float>(size)) : f;
}

uint32_t GlitchVoice::wrapIndex_(int64_t i, uint32_t size) noexcept {
    if (size == 0U) {
        return 0U;
    }
    const int64_t m = static_cast<int64_t>(size);
    int64_t r = i % m;
    if (r < 0) {
        r += m;
    }
    return static_cast<uint32_t>(r);
}

float GlitchVoice::readCubic_(const std::vector<float>& ring, float pos, uint32_t size) noexcept {
    if (ring.empty() || size == 0U) {
        return 0.0f;
    }
    const float p = wrapPos_(pos, size);
    const int32_t i1 = static_cast<int32_t>(std::floor(p));
    const float t = p - static_cast<float>(i1);
    const int32_t i0 = i1 - 1;
    const int32_t i2 = i1 + 1;
    const int32_t i3 = i1 + 2;

    const float y0 = ring[wrapIndex_(i0, size)];
    const float y1 = ring[wrapIndex_(i1, size)];
    const float y2 = ring[wrapIndex_(i2, size)];
    const float y3 = ring[wrapIndex_(i3, size)];

    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;
    return ((a0 * t + a1) * t + a2) * t + a3;
}

} // namespace avantgarde

