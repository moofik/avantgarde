#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/IAudioModule.h"

namespace avantgarde {

// Buffer FX (SUPER GLITCH base):
// - пишет вход в ring-buffer;
// - читает из буфера отдельным read pointer;
// - поддерживает музыкальные slice/retrig/reverse/speed/jitter;
// - dry/wet микс.
//
// Важно:
// - все аллокации выполняются только в init();
// - process() не делает аллокаций и не использует lock.
class BufferFxModule final : public IAudioModule {
public:
    enum : uint16_t {
        P_MIX = 0,          // 0..1 dry/wet
        P_SLICE_SIZE = 1,   // 0..1 -> musical slice (1/64..1/4 bar)
        P_REPEAT = 2,       // 0..1 -> repeat count per retrig trigger (1..32)
        P_SPEED = 3,        // 0..1 -> 0.25x..2.0x
        P_JITTER = 4,       // 0..1 (v2: disabled, reserved for future modes)
        P_BUFFER_SIZE = 5,  // 0..1 -> musical buffer size (1/4..2 bars)
        P_RETRIG = 6,       // 0..1 -> musical retrigger division (off..1/2 bar incl triplets)
        P_REVERSE = 7,      // 0..1 -> bool
        NUM_PARAMS = 8
    };

    BufferFxModule() noexcept;

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
        float mix{0.70f};
        float sliceSize{0.50f};
        float repeat{0.15f};
        float reverse{0.0f};
        float speed{0.50f};
        float jitter{0.0f};
        float bufferSize{0.30f};
        float retrig{0.30f};
    };

    struct SliceVoice {
        bool active{false};
        uint32_t sliceStart{0};
        uint32_t sliceLength{1};
        uint32_t repeatsLeft{1};
        float localPlayhead{0.0f}; // [0..sliceLength), растет в "sample-space" с учетом speed
        bool reverse{false};
        float speed{1.0f};
    };

    struct RtState {
        bool active{false};
        // Текущая "зафиксированная" конфигурация фразы.
        // Эти параметры применяются не мгновенно, а на границе новой фразы.
        uint32_t bufferSizeSamples{0};
        uint32_t sliceSamples{0};
        uint32_t repeatCount{1};
        // Текущий "основной" голос slice playback.
        SliceVoice current{};
        // Следующий голос для плавной склейки при retrig/reverse transition.
        SliceVoice next{};
        bool inTransition{false};
        uint32_t transitionSamples{0};
        uint32_t transitionPos{0};

        bool reverseParam{false};
        float speedParam{1.0f};
        float jitterAmount{0.0f};
        uint64_t nextRetrigSample{0};
        // Внешний таймер retrigger (музыкальная сетка).
        // Независим от repeat/slice/speed.
        uint32_t retrigIntervalSamples{0};

        // Pending-конфигурация: выставляется из UI, но применяется только
        // на границе фразы (retrig boundary / конец голоса в retrig=off).
        bool pendingPhraseParams{false};
        uint32_t pendingBufferSizeSamples{0};
        uint32_t pendingSliceSamples{0};
        uint32_t pendingRepeatCount{1};
        uint32_t pendingRetrigIntervalSamples{0};
        bool pendingReverseParam{false};
    };

    static float clamp01_(float v) noexcept;
    static uint32_t clampToRangeU32_(uint32_t v, uint32_t lo, uint32_t hi) noexcept;
    static float clampToRangeF_(float v, float lo, float hi) noexcept;

    static float mapSpeed_(float speed01) noexcept;
    static uint32_t mapRepeatCount_(float repeat01) noexcept;
    static uint32_t effectiveRepeatCount_(uint32_t requestedRepeat,
                                          uint32_t retrigIntervalSamples,
                                          uint32_t sliceLengthSamples,
                                          float speed) noexcept;
    static uint32_t mapRetrigIntervalSamples_(float retrig01,
                                              double sampleRate,
                                              float bpm,
                                              uint8_t tsNum,
                                              uint8_t tsDen) noexcept;
    static uint32_t mapBufferSizeSamples_(float bufferSize01,
                                          double sampleRate,
                                          float bpm,
                                          uint8_t tsNum,
                                          uint8_t tsDen) noexcept;
    static float beatsPerBar_(uint8_t tsNum, uint8_t tsDen) noexcept;
    static uint32_t mapSliceSamples_(float slice01,
                                     double sampleRate,
                                     float bpm,
                                     uint8_t tsNum,
                                     uint8_t tsDen) noexcept;

    static float wrapPos_(float pos, uint32_t size) noexcept;
    static uint32_t wrapIndex_(int64_t i, uint32_t size) noexcept;
    static float readCubic_(const std::vector<float>& ring, float pos, uint32_t size) noexcept;
    static float findTransientScore_(float prev, float cur) noexcept;

    static float renderVoiceSample_(const SliceVoice& voice,
                                    const std::vector<float>& ring,
                                    uint32_t ringSize) noexcept;
    static void advanceVoice_(SliceVoice& voice) noexcept;
    void setActivePhraseParams_(uint32_t bufferSizeSamples,
                                uint32_t sliceSamples,
                                uint32_t repeatCount,
                                uint32_t retrigIntervalSamples,
                                bool reverse) noexcept;
    bool hasPendingPhraseDiff_(uint32_t bufferSizeSamples,
                               uint32_t sliceSamples,
                               uint32_t repeatCount,
                               uint32_t retrigIntervalSamples,
                               bool reverse) const noexcept;
    // Применяет pending-параметры на границе фразы.
    // @return true, если сменился размер буфера и writePos нужно нормализовать.
    bool applyPendingPhraseParams_() noexcept;
    void startVoice_(const SliceVoice& voice, bool crossfade) noexcept;
    void triggerSliceAtSample_(uint64_t quantizedSample,
                               uint64_t nowSample,
                               uint32_t bufferSizeSamples,
                               uint32_t sliceSamples,
                               uint32_t repeatCount,
                               bool reverse,
                               float speed,
                               float jitterAmount) noexcept;

    // One-pole DC blocker на wet path для уменьшения low-freq click/drift.
    static float dcBlock_(float x, float& x1, float& y1) noexcept;

    double sampleRate_{48000.0};
    uint32_t ringCapacity_{0};
    std::vector<float> ringL_{};
    std::vector<float> ringR_{};
    uint32_t writePos_{0};
    uint32_t rngState_{0x13572468u};

    std::atomic<Params> write_{Params{}};
    Params read_{};
    RtState state_{};
    float dcXL1_{0.0f};
    float dcYL1_{0.0f};
    float dcXR1_{0.0f};
    float dcYR1_{0.0f};
    float wetLpL_{0.0f};
    float wetLpR_{0.0f};
    float effectiveBpm_{120.0f};
    uint64_t localSampleCounter_{0};

    std::array<ParamMeta, NUM_PARAMS> meta_{};
};

} // namespace avantgarde
