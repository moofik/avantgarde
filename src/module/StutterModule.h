#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/IAudioModule.h"

namespace avantgarde {

// BPM-синхронный stutter/gate модуль.
//
// Идея:
// - Gate и Retrigger независимы.
// - При Retrigger=Off модуль работает как темпо-синхронный gate.
// - При Retrigger>Off модуль читает короткие фрагменты из предыдущего подшага,
//   создавая repeat/stutter характер.
class StutterModule final : public IAudioModule {
public:
    enum : uint16_t {
        P_WET = 0,       // 0..1 dry/wet
        P_RATE = 1,      // 0..1 -> музыкальная длительность шага
        P_GATE = 2,      // 0..1 -> доля открытого окна в шаге
        // 0..1 -> дискретные ступени retrig:
        // OFF, 1, 2, 4, 8, 16.
        // Значение N означает "N перезахватов за 1 bar".
        P_RETRIGGER = 3,
        NUM_PARAMS = 4
    };

    StutterModule() noexcept;

    // ---- IAudioModule ----
    void init(double sampleRate, std::size_t maxFrames) override;
    void beginBlock() noexcept override;
    void process(const AudioProcessContext& ctx) noexcept override;
    void reset() override;

    // ---- IParameterized ----
    std::size_t getParamCount() const override;
    float getParam(std::size_t index) const override;
    void setParam(std::size_t index, float value) override;
    const ParamMeta& getParamMeta(std::size_t index) const override;

private:
    struct Params {
        float wet{0.70f};
        float rate{0.65f};
        float gate{0.60f};
        float retrigger{0.0f};
    };

    static float clamp01_(float v) noexcept;
    static float mapRateToBeats_(float rate01) noexcept;
    static uint8_t mapRetriggerCount_(float retrig01) noexcept;

    float sampleAtDelay_(const std::vector<float>& ring, std::size_t writePos, std::size_t delay) const noexcept;
    float gateEnvelope_(std::size_t indexInSubStep, std::size_t gateSamples, std::size_t subStepSamples) const noexcept;

    double sampleRate_{48000.0};
    std::size_t ringSize_{0};
    std::vector<float> ringL_{};
    std::vector<float> ringR_{};
    std::size_t writePos_{0};
    uint64_t localSampleCounter_{0};

    std::atomic<Params> write_{Params{}};
    Params read_{};
    std::array<ParamMeta, NUM_PARAMS> meta_{};
};

} // namespace avantgarde
