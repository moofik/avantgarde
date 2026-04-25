#pragma once

#include <cstdint>
#include <vector>

namespace avantgarde {

struct GlitchVoiceState {
    bool active{false};
    uint32_t phraseStart{0};
    uint32_t phraseLength{1};
    uint32_t subsliceStart{0};
    uint32_t subsliceLength{1};
    float localPlayhead{0.0f};
    float speed{1.0f};
    bool reverse{false};
    bool choppy{false};
};

// Helper: рендер/advance голоса (без владения ring).
class GlitchVoice final {
public:
    static float renderSample(const GlitchVoiceState& voice,
                              const std::vector<float>& ring,
                              uint32_t ringSize) noexcept;

    static void advance(GlitchVoiceState& voice, uint32_t ringCapacity) noexcept;

private:
    static float wrapPos_(float pos, uint32_t size) noexcept;
    static uint32_t wrapIndex_(int64_t i, uint32_t size) noexcept;
    static float readCubic_(const std::vector<float>& ring, float pos, uint32_t size) noexcept;
};

} // namespace avantgarde

