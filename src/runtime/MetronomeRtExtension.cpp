#include "runtime/MetronomeRtExtension.h"

#include <algorithm>
#include <cmath>

namespace avantgarde {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kMinBpm = 20.0f;
constexpr float kMaxBpm = 300.0f;

float clampBpm(float bpm) noexcept {
    if (!std::isfinite(bpm)) {
        return 120.0f;
    }
    return std::clamp(bpm, kMinBpm, kMaxBpm);
}

uint8_t sanitizeDen(uint8_t den) noexcept {
    switch (den) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
            return den;
        default:
            return 4;
    }
}

uint32_t stepsPerBar16(uint8_t tsNum, uint8_t tsDen) noexcept {
    const uint8_t safeNum = (tsNum == 0u) ? 4u : tsNum;
    const uint8_t safeDen = sanitizeDen(tsDen);
    const double steps = (static_cast<double>(safeNum) * 16.0) / static_cast<double>(safeDen);
    return static_cast<uint32_t>(std::max<int>(1, static_cast<int>(std::lround(steps))));
}

} // namespace

MetronomeRtExtension::MetronomeRtExtension(double sampleRate, uint32_t numOutChannels) noexcept
    : sampleRate_((std::isfinite(sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0),
      numOutChannels_(std::max<uint32_t>(1u, numOutChannels)) {}

void MetronomeRtExtension::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        for (Voice& v : voices_) {
            v = Voice{};
        }
    }
}

bool MetronomeRtExtension::enabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

MetronomeRtExtension::TickAccent
MetronomeRtExtension::classifyTick_(uint64_t tickIndex, uint8_t tsNum, uint8_t tsDen) noexcept {
    const uint32_t barSteps = stepsPerBar16(tsNum, tsDen);
    const uint32_t stepInBar = static_cast<uint32_t>(tickIndex % static_cast<uint64_t>(barSteps));
    if (stepInBar == 0U) {
        return TickAccent::Bar;
    }
    // Каждые 4 шага сетки 1/16 = четвертная доля.
    if ((stepInBar % 4U) == 0U) {
        return TickAccent::Beat;
    }
    return TickAccent::Subdivision;
}

void MetronomeRtExtension::triggerVoice_(TickAccent accent) noexcept {
    Voice& v = voices_[nextVoice_];
    nextVoice_ = (nextVoice_ + 1U) % kMaxVoices;

    float freq = 1200.0f;
    float gain = 0.06f;
    float durationMs = 18.0f;
    if (accent == TickAccent::Bar) {
        freq = 2200.0f;
        gain = 0.10f;
        durationMs = 24.0f;
    } else if (accent == TickAccent::Beat) {
        freq = 1700.0f;
        gain = 0.08f;
        durationMs = 20.0f;
    }

    const float samples = std::max<float>(1.0f, static_cast<float>(sampleRate_ * (durationMs / 1000.0f)));
    v.active = true;
    v.phase = 0.0f;
    v.phaseInc = kTwoPi * (freq / static_cast<float>(sampleRate_));
    v.env = 1.0f;
    v.gain = gain;
    v.decayPerSample = std::pow(0.001f, 1.0f / samples);
}

float MetronomeRtExtension::renderVoicesSample_() noexcept {
    float sum = 0.0f;
    for (Voice& v : voices_) {
        if (!v.active) {
            continue;
        }
        sum += std::sinf(v.phase) * v.env * v.gain;
        v.phase += v.phaseInc;
        if (v.phase >= kTwoPi) {
            v.phase -= kTwoPi;
        }
        v.env *= v.decayPerSample;
        if (v.env <= 1e-4f) {
            v = Voice{};
        }
    }
    return sum;
}

void MetronomeRtExtension::onBlockEnd(const AudioProcessContext& ctx) noexcept {
    if (!enabled()) {
        return;
    }
    if (!ctx.transportValid || !ctx.transportPlaying) {
        return;
    }
    if (!ctx.out || ctx.nframes == 0U) {
        return;
    }

    const float bpm = clampBpm(ctx.transportBpm);

    // AVANTGARDE_METRONOME_GRID_TUNE:
    // Базовая сетка метронома/раннего секвенсора = 1/16 нота (1/4 beat).
    // Для 1/8 поставь 2.0, для 1/32 поставь 8.0.
    constexpr double kSubStepsPerBeat = 1.0;

    const double beatSamplesD = std::max(1.0, std::round(sampleRate_ * 60.0 / static_cast<double>(bpm)));
    const uint64_t subStepSamples = static_cast<uint64_t>(
        std::max(1.0, std::round(beatSamplesD / kSubStepsPerBeat)));

    const uint64_t blockStart = ctx.transportSampleTime;
    const uint64_t blockEnd = blockStart + static_cast<uint64_t>(ctx.nframes);
    uint64_t tick = ((blockStart + subStepSamples - 1U) / subStepSamples) * subStepSamples;

    std::array<uint32_t, kMaxTicksPerBlock> tickOffsets{};
    std::array<TickAccent, kMaxTicksPerBlock> tickAccents{};
    std::size_t tickCount = 0U;
    while (tick < blockEnd && tickCount < kMaxTicksPerBlock) {
        tickOffsets[tickCount] = static_cast<uint32_t>(tick - blockStart);
        const uint64_t tickIndex = tick / subStepSamples;
        tickAccents[tickCount] = classifyTick_(tickIndex, ctx.transportTsNum, ctx.transportTsDen);
        ++tickCount;
        tick += subStepSamples;
    }

    std::size_t nextTickIdx = 0U;
    const uint32_t outCh = std::max<uint32_t>(1U, numOutChannels_);
    for (std::size_t i = 0; i < ctx.nframes; ++i) {
        while (nextTickIdx < tickCount && tickOffsets[nextTickIdx] == static_cast<uint32_t>(i)) {
            triggerVoice_(tickAccents[nextTickIdx]);
            ++nextTickIdx;
        }
        const float click = renderVoicesSample_();
        for (uint32_t ch = 0; ch < outCh; ++ch) {
            if (!ctx.out[ch]) {
                continue;
            }
            ctx.out[ch][i] += click;
        }
    }
}

} // namespace avantgarde

