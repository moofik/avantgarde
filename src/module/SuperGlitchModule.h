#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "contracts/IAudioModule.h"
#include "module/GlitchVoice.h"
#include "module/PhraseCapture.h"

namespace avantgarde {

// SUPER GLITCH v1:
// - захватывает длинную phrase по музыкальной сетке;
// - режет phrase на subslice;
// - воспроизводит phrase/subslice предсказуемо (smooth/choppy);
// - не пускает live dry в wet-path между циклами.
class SuperGlitchModule final : public IAudioModule {
public:
    enum : uint16_t {
        P_MIX = 0,       // 0..1 dry/wet
        P_SUBSLICE = 1,  // 0..1 -> whole/2/4/8/16
        P_HOLD = 2,      // 0..1 -> gate duty / loop density
        P_SPEED = 3,     // 0..1 -> 0.25x..2.0x (neutral=0.5)
        P_MODE = 4,      // 0..1 -> smooth/choppy
        P_PHRASE = 5,    // 0..1 -> phrase length (1/4..2 bar, hi=4s)
        P_RETRIG = 6,    // 0..1 -> musical retrig interval
        P_REVERSE = 7,   // 0..1 -> bool
        NUM_PARAMS = 8
    };

    SuperGlitchModule() noexcept;

    void init(double sampleRate, std::size_t maxFrames) override;
    void beginBlock() noexcept override;
    void process(const AudioProcessContext& ctx) noexcept override;
    void reset() override;

    std::size_t getParamCount() const override;
    float getParam(std::size_t index) const override;
    void setParam(std::size_t index, float value) override;
    const ParamMeta& getParamMeta(std::size_t index) const override;

private:
    struct Params {
        float mix{0.70f};
        float subslice{0.25f};
        float hold{0.70f};
        float speed{0.50f};
        float mode{0.0f};
        float phrase{0.50f};
        float retrig{0.30f};
        float reverse{0.0f};
    };

    using SliceVoice = GlitchVoiceState;

    struct RtState {
        bool active{false};

        // Phrase state.
        uint32_t phraseStart{0};
        uint32_t phraseLength{1};
        uint32_t subsliceLength{1};
        uint32_t subsliceStart{0};
        float playheadInPhrase{0.0f};
        bool isPhraseCaptured{false};

        // Playback voices + transition.
        SliceVoice current{};
        SliceVoice next{};
        bool inTransition{false};
        uint32_t transitionSamples{0};
        uint32_t transitionPos{0};

        // Musical timing.
        uint32_t retrigIntervalSamples{0};
        uint64_t nextRetrigSample{0};
        uint64_t lastRetrigSample{0};

        // Tone/control params.
        float speed{1.0f};
        float holdDuty{1.0f};
        bool reverse{false};
        bool choppy{false};

        // Pending phrase-level params (applied on boundary only).
        bool pendingPhraseParams{false};
        uint32_t pendingPhraseLength{1};
        uint32_t pendingSubsliceLength{1};
        bool pendingReverse{false};
        bool pendingChoppy{false};
    };

    static float clamp01_(float v) noexcept;
    static uint32_t clampToRangeU32_(uint32_t v, uint32_t lo, uint32_t hi) noexcept;
    static float clampToRangeF_(float v, float lo, float hi) noexcept;
    static float beatsPerBar_(uint8_t tsNum, uint8_t tsDen) noexcept;

    static float mapSpeed_(float speed01) noexcept;
    static float mapHoldDuty_(float hold01) noexcept;
    static bool mapChoppy_(float mode01) noexcept;
    static uint32_t mapPhraseSamples_(float phrase01,
                                      double sampleRate,
                                      float bpm,
                                      uint8_t tsNum,
                                      uint8_t tsDen,
                                      uint32_t ringCapacity) noexcept;
    static uint32_t mapSubsliceSamples_(float subslice01,
                                        uint32_t phraseSamples) noexcept;
    static uint32_t mapRetrigIntervalSamples_(float retrig01,
                                              double sampleRate,
                                              float bpm,
                                              uint8_t tsNum,
                                              uint8_t tsDen) noexcept;

    static uint32_t wrapIndex_(int64_t i, uint32_t size) noexcept;
    static float dcBlock_(float x, float& x1, float& y1) noexcept;

    void startVoice_(const SliceVoice& voice, bool crossfade) noexcept;
    void capturePhraseAtSample_(uint64_t quantizedSample,
                                uint64_t nowSample,
                                uint32_t phraseLength,
                                uint32_t subsliceLength,
                                bool reverse,
                                bool choppy,
                                float speed) noexcept;
    void restartFromCurrentPhrase_(uint64_t boundarySample) noexcept;
    void setActivePhraseParams_(uint32_t phraseLength,
                                uint32_t subsliceLength,
                                bool reverse,
                                bool choppy) noexcept;
    bool hasPendingPhraseDiff_(uint32_t phraseLength,
                               uint32_t subsliceLength,
                               bool reverse,
                               bool choppy) const noexcept;
    void applyPendingPhraseParams_() noexcept;

    double sampleRate_{48000.0};
    uint32_t ringCapacity_{0};
    PhraseCapture phraseCapture_{};

    std::atomic<Params> write_{Params{}};
    Params read_{};
    RtState state_{};

    // Wet post.
    float wetLpL_{0.0f};
    float wetLpR_{0.0f};
    float wetHoldL_{0.0f};
    float wetHoldR_{0.0f};

    // DC blocker.
    float dcXL1_{0.0f};
    float dcYL1_{0.0f};
    float dcXR1_{0.0f};
    float dcYR1_{0.0f};

    float effectiveBpm_{120.0f};
    uint64_t localSampleCounter_{0};

    std::array<ParamMeta, NUM_PARAMS> meta_{};
};

} // namespace avantgarde
