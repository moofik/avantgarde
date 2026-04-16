#include "module/BufferFxModule.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace avantgarde {
namespace {

constexpr float kMinBpm = 20.0f;
constexpr float kMaxBpm = 300.0f;
constexpr float kMinSpeed = 0.25f;
constexpr float kMaxSpeed = 2.0f;
constexpr uint32_t kMinRepeat = 1U;
constexpr uint32_t kMaxRepeat = 32U;
constexpr uint32_t kMinSliceSamples = 8U;
constexpr uint32_t kMaxCrossfadeSamples = 128U;
constexpr float kDcBlockR = 0.995f;
constexpr float kSaturationDriveBase = 1.15f;
constexpr float kWetSmoothAlpha = 0.25f;
// Компромисс памяти/функционала: максимум 8 секунд буфера на один инстанс.
constexpr double kMaxBufferSeconds = 8.0;

// Musical fractions in bars.
constexpr std::array<float, 5> kSliceBars = {
    1.0f / 64.0f, // 1/64 bar
    1.0f / 32.0f, // 1/32 bar
    1.0f / 16.0f, // 1/16 bar
    1.0f / 8.0f,  // 1/8 bar
    1.0f / 4.0f,  // 1/4 bar
};

constexpr std::array<float, 4> kBufferBars = {
    1.0f / 4.0f, // 1/4 bar
    1.0f / 2.0f, // 1/2 bar
    1.0f,        // 1 bar
    2.0f,        // 2 bars
};

// Retrig divisions in bars:
// Off, 1/32, 1/16, 1/8, 1/6(T), 1/4, 1/3(T), 1/2
constexpr std::array<float, 8> kRetrigBars = {
    0.0f,
    1.0f / 32.0f,
    1.0f / 16.0f,
    1.0f / 8.0f,
    1.0f / 6.0f,
    1.0f / 4.0f,
    1.0f / 3.0f,
    1.0f / 2.0f,
};

} // namespace

BufferFxModule::BufferFxModule() noexcept {
    meta_[P_MIX] = {"Mix", 0.0f, 1.0f, false, "norm"};
    meta_[P_SLICE_SIZE] = {"Slice", 0.0f, 1.0f, false, "sync"};
    meta_[P_REPEAT] = {"Repeat", 0.0f, 1.0f, false, "enum"};
    meta_[P_SPEED] = {"Speed", 0.0f, 1.0f, false, "rate"};
    meta_[P_JITTER] = {"Jitter (Off)", 0.0f, 1.0f, false, "norm"};
    meta_[P_BUFFER_SIZE] = {"Buffer", 0.0f, 1.0f, false, "sync"};
    meta_[P_RETRIG] = {"Retrig", 0.0f, 1.0f, false, "sync"};
    meta_[P_REVERSE] = {"Reverse", 0.0f, 1.0f, false, "bool"};
}

void BufferFxModule::init(double sampleRate, std::size_t /*maxFrames*/) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    ringCapacity_ = std::max<uint32_t>(
        1024U,
        static_cast<uint32_t>(std::lround(sampleRate_ * kMaxBufferSeconds)));
    ringL_.assign(ringCapacity_, 0.0f);
    ringR_.assign(ringCapacity_, 0.0f);
    writePos_ = 0U;
    localSampleCounter_ = 0U;
    effectiveBpm_ = 120.0f;
    rngState_ = 0x13572468u;
    read_ = write_.load(std::memory_order_relaxed);
    state_ = RtState{};
}

void BufferFxModule::beginBlock() noexcept {
    read_ = write_.load(std::memory_order_relaxed);
}

void BufferFxModule::process(const AudioProcessContext& ctx) noexcept {
    if (!ctx.in || !ctx.out || ringCapacity_ == 0U || ringL_.empty() || ringR_.empty()) {
        return;
    }

    const float* inL = ctx.in[0];
    const float* inR = ctx.in[1] ? ctx.in[1] : ctx.in[0];
    float* outL = ctx.out[0];
    float* outR = ctx.out[1] ? ctx.out[1] : ctx.out[0];
    if (!inL || !outL) {
        return;
    }

    if (ctx.transportValid) {
        effectiveBpm_ = clampToRangeF_(ctx.transportBpm, kMinBpm, kMaxBpm);
    }
    const float bpm = effectiveBpm_;
    const uint32_t mappedBufferSizeSamples = std::min<uint32_t>(
        ringCapacity_,
        mapBufferSizeSamples_(read_.bufferSize, sampleRate_, bpm, ctx.transportTsNum, ctx.transportTsDen));
    const uint32_t mappedSliceSamplesRaw = mapSliceSamples_(
        read_.sliceSize,
        sampleRate_,
        bpm,
        ctx.transportTsNum,
        ctx.transportTsDen);
    const uint32_t mappedSliceSamples = std::max<uint32_t>(
        kMinSliceSamples,
        std::min<uint32_t>(mappedSliceSamplesRaw, std::max<uint32_t>(kMinSliceSamples, mappedBufferSizeSamples - 1U)));
    const uint32_t mappedRepeatCount = mapRepeatCount_(read_.repeat);
    const uint32_t mappedRetrigInterval = mapRetrigIntervalSamples_(
        read_.retrig,
        sampleRate_,
        bpm,
        ctx.transportTsNum,
        ctx.transportTsDen);
    const bool mappedReverseParam = (read_.reverse >= 0.5f);
    const float speed = mapSpeed_(read_.speed);
    // JITTER POLICY v2:
    // jitter временно отключен в DSP, чтобы не ломать groove-предсказуемость.
    const float jitter = 0.0f;
    const float mix = clamp01_(read_.mix);
    const float dry = 1.0f - mix;

    if (!state_.active) {
        setActivePhraseParams_(
            mappedBufferSizeSamples,
            mappedSliceSamples,
            mappedRepeatCount,
            mappedRetrigInterval,
            mappedReverseParam);
    } else if (hasPendingPhraseDiff_(
                   mappedBufferSizeSamples,
                   mappedSliceSamples,
                   mappedRepeatCount,
                   mappedRetrigInterval,
                   mappedReverseParam)) {
        state_.pendingPhraseParams = true;
        state_.pendingBufferSizeSamples = mappedBufferSizeSamples;
        state_.pendingSliceSamples = mappedSliceSamples;
        state_.pendingRepeatCount = mappedRepeatCount;
        state_.pendingRetrigIntervalSamples = mappedRetrigInterval;
        state_.pendingReverseParam = mappedReverseParam;
    } else {
        state_.pendingPhraseParams = false;
    }

    if (writePos_ >= state_.bufferSizeSamples) {
        writePos_ %= state_.bufferSizeSamples;
    }

    for (std::size_t i = 0; i < ctx.nframes; ++i) {
        const float xL = inL[i];
        const float xR = inR ? inR[i] : xL;

        ringL_[writePos_] = xL;
        ringR_[writePos_] = xR;
        writePos_ = (writePos_ + 1U >= state_.bufferSizeSamples) ? 0U : writePos_ + 1U;

        const uint64_t absSample = (ctx.transportValid ? ctx.transportSampleTime : localSampleCounter_) + i;

        const auto anyVoiceActive = [this]() noexcept {
            return state_.current.active || (state_.inTransition && state_.next.active);
        };

        if (!state_.active) {
            state_.active = true;
            state_.speedParam = speed;
            state_.jitterAmount = jitter;

            if (state_.retrigIntervalSamples > 0U) {
                // First-trigger policy v2 (musical default): WaitNextGrid.
                state_.nextRetrigSample =
                    ((absSample / static_cast<uint64_t>(state_.retrigIntervalSamples)) + 1ULL) *
                    static_cast<uint64_t>(state_.retrigIntervalSamples);
            } else {
                triggerSliceAtSample_(
                    absSample,
                    absSample,
                    state_.bufferSizeSamples,
                    state_.sliceSamples,
                    state_.repeatCount,
                    state_.reverseParam,
                    state_.speedParam,
                    state_.jitterAmount);
                state_.nextRetrigSample = 0U;
            }
        } else {
            state_.speedParam = speed;
            state_.jitterAmount = jitter;

            if (state_.retrigIntervalSamples > 0U) {
                while (absSample >= state_.nextRetrigSample) {
                    const uint64_t boundarySample = state_.nextRetrigSample;

                    const bool bufferChanged = applyPendingPhraseParams_();
                    if (bufferChanged && writePos_ >= state_.bufferSizeSamples) {
                        writePos_ %= state_.bufferSizeSamples;
                    }

                    triggerSliceAtSample_(
                        boundarySample,
                        absSample,
                        state_.bufferSizeSamples,
                        state_.sliceSamples,
                        state_.repeatCount,
                        state_.reverseParam,
                        state_.speedParam,
                        state_.jitterAmount);

                    if (state_.retrigIntervalSamples > 0U) {
                        state_.nextRetrigSample = boundarySample + static_cast<uint64_t>(state_.retrigIntervalSamples);
                    } else {
                        state_.nextRetrigSample = 0U;
                        break;
                    }
                }
            } else if (!anyVoiceActive()) {
                // Граница фразы в retrig=off: сначала применяем pending-параметры,
                // затем либо ждем сетку (если retrig включился), либо стартуем сразу.
                const bool bufferChanged = applyPendingPhraseParams_();
                if (bufferChanged && writePos_ >= state_.bufferSizeSamples) {
                    writePos_ %= state_.bufferSizeSamples;
                }

                if (state_.retrigIntervalSamples > 0U) {
                    state_.nextRetrigSample =
                        ((absSample / static_cast<uint64_t>(state_.retrigIntervalSamples)) + 1ULL) *
                        static_cast<uint64_t>(state_.retrigIntervalSamples);
                } else {
                    triggerSliceAtSample_(
                        absSample,
                        absSample,
                        state_.bufferSizeSamples,
                        state_.sliceSamples,
                        state_.repeatCount,
                        state_.reverseParam,
                        state_.speedParam,
                        state_.jitterAmount);
                }
            }
        }

        float wetL = xL;
        float wetR = xR;

        if (state_.active && anyVoiceActive()) {
            float curL = 0.0f;
            float curR = 0.0f;
            if (state_.current.active) {
                curL = renderVoiceSample_(state_.current, ringL_, state_.bufferSizeSamples);
                curR = renderVoiceSample_(state_.current, ringR_, state_.bufferSizeSamples);
                advanceVoice_(state_.current);
            }

            if (state_.inTransition && state_.next.active) {
                const float nxtL = renderVoiceSample_(state_.next, ringL_, state_.bufferSizeSamples);
                const float nxtR = renderVoiceSample_(state_.next, ringR_, state_.bufferSizeSamples);
                advanceVoice_(state_.next);

                const float a = (state_.transitionSamples > 0U)
                    ? clampToRangeF_(
                          static_cast<float>(state_.transitionPos) /
                          static_cast<float>(state_.transitionSamples),
                          0.0f,
                          1.0f)
                    : 1.0f;
                wetL = (1.0f - a) * curL + a * nxtL;
                wetR = (1.0f - a) * curR + a * nxtR;

                ++state_.transitionPos;
                if (!state_.next.active || state_.transitionPos >= state_.transitionSamples) {
                    state_.current = state_.next;
                    state_.next = SliceVoice{};
                    state_.inTransition = false;
                    state_.transitionSamples = 0U;
                    state_.transitionPos = 0U;
                }
            } else {
                wetL = curL;
                wetR = curR;
            }

            const float drive = kSaturationDriveBase + 1.5f * state_.jitterAmount;
            wetL = std::tanh(wetL * drive);
            wetR = std::tanh(wetR * drive);
            wetLpL_ += kWetSmoothAlpha * (wetL - wetLpL_);
            wetLpR_ += kWetSmoothAlpha * (wetR - wetLpR_);
            wetL = wetLpL_;
            wetR = wetLpR_;
        }

        float yL = dry * xL + mix * wetL;
        float yR = dry * xR + mix * wetR;
        if (state_.active && mix >= 0.4f) {
            yL = dcBlock_(yL, dcXL1_, dcYL1_);
            yR = dcBlock_(yR, dcXR1_, dcYR1_);
        }
        outL[i] = yL;
        outR[i] = yR;
    }

    if (!ctx.transportValid) {
        localSampleCounter_ += static_cast<uint64_t>(ctx.nframes);
    }
}

void BufferFxModule::reset() {
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writePos_ = 0U;
    localSampleCounter_ = 0U;
    state_ = RtState{};
    dcXL1_ = 0.0f;
    dcYL1_ = 0.0f;
    dcXR1_ = 0.0f;
    dcYR1_ = 0.0f;
    wetLpL_ = 0.0f;
    wetLpR_ = 0.0f;
    effectiveBpm_ = 120.0f;
}

std::size_t BufferFxModule::getParamCount() const {
    return NUM_PARAMS;
}

float BufferFxModule::getParam(std::size_t index) const {
    switch (index) {
        case P_MIX: return read_.mix;
        case P_SLICE_SIZE: return read_.sliceSize;
        case P_REPEAT: return read_.repeat;
        case P_SPEED: return read_.speed;
        case P_JITTER: return read_.jitter;
        case P_BUFFER_SIZE: return read_.bufferSize;
        case P_RETRIG: return read_.retrig;
        case P_REVERSE: return read_.reverse;
        default: return 0.0f;
    }
}

void BufferFxModule::setParam(std::size_t index, float value) {
    Params p = write_.load(std::memory_order_relaxed);
    const float v = clamp01_(value);
    switch (index) {
        case P_MIX:
            p.mix = v;
            break;
        case P_SLICE_SIZE:
            p.sliceSize = v;
            break;
        case P_REPEAT:
            p.repeat = v;
            break;
        case P_SPEED:
            p.speed = v;
            break;
        case P_JITTER:
            // JITTER POLICY v2: временно отключен, всегда 0.
            p.jitter = 0.0f;
            break;
        case P_BUFFER_SIZE:
            p.bufferSize = v;
            break;
        case P_RETRIG:
            p.retrig = v;
            break;
        case P_REVERSE:
            p.reverse = v;
            break;
        default:
            break;
    }
    write_.store(p, std::memory_order_relaxed);
}

const ParamMeta& BufferFxModule::getParamMeta(std::size_t index) const {
    return meta_[index];
}

float BufferFxModule::clamp01_(float v) noexcept {
    return clampToRangeF_(v, 0.0f, 1.0f);
}

uint32_t BufferFxModule::clampToRangeU32_(uint32_t v, uint32_t lo, uint32_t hi) noexcept {
    return std::min<uint32_t>(hi, std::max<uint32_t>(lo, v));
}

float BufferFxModule::clampToRangeF_(float v, float lo, float hi) noexcept {
    return std::min<float>(hi, std::max<float>(lo, v));
}

float BufferFxModule::mapSpeed_(float speed01) noexcept {
    const float t = clamp01_(speed01);
    if (t < 0.5f) {
        const float u = t / 0.5f;          // 0..1
        return kMinSpeed + (1.0f - kMinSpeed) * u; // 0.25..1.0
    }
    const float u = (t - 0.5f) / 0.5f; // 0..1
    return 1.0f + (kMaxSpeed - 1.0f) * u;   // 1.0..2.0
}

uint32_t BufferFxModule::mapRepeatCount_(float repeat01) noexcept {
    const float t = clamp01_(repeat01);
    const uint32_t n = static_cast<uint32_t>(
        std::lround(static_cast<float>(kMinRepeat) + t * static_cast<float>(kMaxRepeat - kMinRepeat)));
    return clampToRangeU32_(n, kMinRepeat, kMaxRepeat);
}

uint32_t BufferFxModule::effectiveRepeatCount_(uint32_t requestedRepeat,
                                               uint32_t retrigIntervalSamples,
                                               uint32_t sliceLengthSamples,
                                               float speed) noexcept {
    const uint32_t requested = clampToRangeU32_(requestedRepeat, kMinRepeat, kMaxRepeat);
    if (retrigIntervalSamples == 0U || sliceLengthSamples == 0U) {
        return requested;
    }
    const float safeSpeed = clampToRangeF_(speed, kMinSpeed, kMaxSpeed);
    // Output duration of one slice cycle in host samples.
    const float cycleOutSamplesF =
        static_cast<float>(sliceLengthSamples) / std::max(1e-6f, safeSpeed);
    const uint32_t cycleOutSamples = std::max<uint32_t>(1U, static_cast<uint32_t>(std::ceil(cycleOutSamplesF)));
    const uint32_t maxAudibleByRetrig = std::max<uint32_t>(1U, retrigIntervalSamples / cycleOutSamples);
    return std::max<uint32_t>(1U, std::min<uint32_t>(requested, maxAudibleByRetrig));
}

float BufferFxModule::beatsPerBar_(uint8_t tsNum, uint8_t tsDen) noexcept {
    const uint8_t num = (tsNum > 0U) ? tsNum : 4U;
    const uint8_t den = (tsDen > 0U) ? tsDen : 4U;
    return std::max(1.0f, static_cast<float>(num) * (4.0f / static_cast<float>(den)));
}

uint32_t BufferFxModule::mapRetrigIntervalSamples_(float retrig01,
                                                    double sampleRate,
                                                    float bpm,
                                                    uint8_t tsNum,
                                                    uint8_t tsDen) noexcept {
    const float t = clamp01_(retrig01);
    const std::size_t idx = std::min<std::size_t>(
        kRetrigBars.size() - 1U,
        static_cast<std::size_t>(std::lround(t * static_cast<float>(kRetrigBars.size() - 1U))));
    const float bars = kRetrigBars[idx];
    if (bars <= 0.0f) {
        return 0U;
    }
    const float bps = clampToRangeF_(bpm, kMinBpm, kMaxBpm) / 60.0f;
    const float samplesPerBeat = static_cast<float>(sampleRate) / std::max(1e-6f, bps);
    const float samplesPerBar = samplesPerBeat * beatsPerBar_(tsNum, tsDen);
    const uint32_t out = static_cast<uint32_t>(std::lround(samplesPerBar * bars));
    return std::max<uint32_t>(1U, out);
}

uint32_t BufferFxModule::mapBufferSizeSamples_(float bufferSize01,
                                                double sampleRate,
                                                float bpm,
                                                uint8_t tsNum,
                                                uint8_t tsDen) noexcept {
    const float t = clamp01_(bufferSize01);
    const std::size_t idx = std::min<std::size_t>(
        kBufferBars.size() - 1U,
        static_cast<std::size_t>(std::lround(t * static_cast<float>(kBufferBars.size() - 1U))));
    const float bars = kBufferBars[idx];
    const float bps = clampToRangeF_(bpm, kMinBpm, kMaxBpm) / 60.0f;
    const float samplesPerBeat = static_cast<float>(sampleRate) / std::max(1e-6f, bps);
    const float samplesPerBar = samplesPerBeat * beatsPerBar_(tsNum, tsDen);
    const uint32_t out = static_cast<uint32_t>(std::lround(samplesPerBar * bars));
    return std::max<uint32_t>(out, 128U);
}

uint32_t BufferFxModule::mapSliceSamples_(float slice01,
                                          double sampleRate,
                                          float bpm,
                                          uint8_t tsNum,
                                          uint8_t tsDen) noexcept {
    const float t = clamp01_(slice01);
    const std::size_t idx = std::min<std::size_t>(
        kSliceBars.size() - 1U,
        static_cast<std::size_t>(std::lround(t * static_cast<float>(kSliceBars.size() - 1U))));
    const float bars = kSliceBars[idx];
    const float bps = clampToRangeF_(bpm, kMinBpm, kMaxBpm) / 60.0f;
    const float samplesPerBeat = static_cast<float>(sampleRate) / std::max(1e-6f, bps);
    const float samplesPerBar = samplesPerBeat * beatsPerBar_(tsNum, tsDen);
    const uint32_t out = static_cast<uint32_t>(std::lround(samplesPerBar * bars));
    return std::max<uint32_t>(out, kMinSliceSamples);
}

float BufferFxModule::wrapPos_(float pos, uint32_t size) noexcept {
    if (size == 0U) {
        return 0.0f;
    }
    const float fsize = static_cast<float>(size);
    while (pos < 0.0f) {
        pos += fsize;
    }
    while (pos >= fsize) {
        pos -= fsize;
    }
    return pos;
}

uint32_t BufferFxModule::wrapIndex_(int64_t i, uint32_t size) noexcept {
    if (size == 0U) {
        return 0U;
    }
    const int64_t mod = i % static_cast<int64_t>(size);
    return static_cast<uint32_t>(mod < 0 ? mod + static_cast<int64_t>(size) : mod);
}

float BufferFxModule::readCubic_(const std::vector<float>& ring, float pos, uint32_t size) noexcept {
    if (ring.empty() || size == 0U) {
        return 0.0f;
    }
    const float p = wrapPos_(pos, size);
    const int64_t i1 = static_cast<int64_t>(p);
    const float t = p - static_cast<float>(i1);

    const uint32_t i0u = wrapIndex_(i1 - 1, size);
    const uint32_t i1u = wrapIndex_(i1, size);
    const uint32_t i2u = wrapIndex_(i1 + 1, size);
    const uint32_t i3u = wrapIndex_(i1 + 2, size);

    const float y0 = ring[i0u];
    const float y1 = ring[i1u];
    const float y2 = ring[i2u];
    const float y3 = ring[i3u];

    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;
    return ((a0 * t + a1) * t + a2) * t + a3;
}

float BufferFxModule::findTransientScore_(float prev, float cur) noexcept {
    const float d = std::fabs(cur - prev);
    const float a = std::fabs(cur);
    return d + 0.25f * a;
}

float BufferFxModule::renderVoiceSample_(const SliceVoice& voice,
                                         const std::vector<float>& ring,
                                         uint32_t ringSize) noexcept {
    if (!voice.active || ring.empty() || ringSize == 0U || voice.sliceLength == 0U) {
        return 0.0f;
    }
    const float sliceLen = static_cast<float>(voice.sliceLength);
    float local = voice.localPlayhead;
    while (local < 0.0f) {
        local += sliceLen;
    }
    while (local >= sliceLen) {
        local -= sliceLen;
    }

    float absPos = static_cast<float>(voice.sliceStart) + local;
    if (voice.reverse) {
        absPos = static_cast<float>(voice.sliceStart) + (sliceLen - 1.0f - local);
    }
    return readCubic_(ring, wrapPos_(absPos, ringSize), ringSize);
}

void BufferFxModule::advanceVoice_(SliceVoice& voice) noexcept {
    if (!voice.active || voice.sliceLength == 0U) {
        voice.active = false;
        return;
    }

    const float speed = clampToRangeF_(voice.speed, kMinSpeed, kMaxSpeed);
    const float sliceLen = static_cast<float>(voice.sliceLength);
    voice.localPlayhead += speed;

    while (voice.localPlayhead >= sliceLen && voice.active) {
        voice.localPlayhead -= sliceLen;
        if (voice.repeatsLeft > 1U) {
            --voice.repeatsLeft;
        } else {
            voice.active = false;
            voice.repeatsLeft = 0U;
        }
    }
}

void BufferFxModule::setActivePhraseParams_(uint32_t bufferSizeSamples,
                                            uint32_t sliceSamples,
                                            uint32_t repeatCount,
                                            uint32_t retrigIntervalSamples,
                                            bool reverse) noexcept {
    state_.bufferSizeSamples = std::max<uint32_t>(bufferSizeSamples, 128U);
    state_.sliceSamples = clampToRangeU32_(
        sliceSamples,
        kMinSliceSamples,
        std::max<uint32_t>(kMinSliceSamples, state_.bufferSizeSamples - 1U));
    state_.repeatCount = clampToRangeU32_(repeatCount, kMinRepeat, kMaxRepeat);
    state_.retrigIntervalSamples = retrigIntervalSamples;
    state_.reverseParam = reverse;
}

bool BufferFxModule::hasPendingPhraseDiff_(uint32_t bufferSizeSamples,
                                           uint32_t sliceSamples,
                                           uint32_t repeatCount,
                                           uint32_t retrigIntervalSamples,
                                           bool reverse) const noexcept {
    const uint32_t safeBuffer = std::max<uint32_t>(bufferSizeSamples, 128U);
    const uint32_t safeSlice = clampToRangeU32_(
        sliceSamples,
        kMinSliceSamples,
        std::max<uint32_t>(kMinSliceSamples, safeBuffer - 1U));
    const uint32_t safeRepeat = clampToRangeU32_(repeatCount, kMinRepeat, kMaxRepeat);

    return safeBuffer != state_.bufferSizeSamples ||
           safeSlice != state_.sliceSamples ||
           safeRepeat != state_.repeatCount ||
           retrigIntervalSamples != state_.retrigIntervalSamples ||
           reverse != state_.reverseParam;
}

bool BufferFxModule::applyPendingPhraseParams_() noexcept {
    if (!state_.pendingPhraseParams) {
        return false;
    }

    const uint32_t prevBuffer = state_.bufferSizeSamples;
    setActivePhraseParams_(
        state_.pendingBufferSizeSamples,
        state_.pendingSliceSamples,
        state_.pendingRepeatCount,
        state_.pendingRetrigIntervalSamples,
        state_.pendingReverseParam);
    state_.pendingPhraseParams = false;

    if (prevBuffer != state_.bufferSizeSamples) {
        // Смена размера активного окна буфера — это смена "координатной системы".
        // Сбрасываем активный хвост transition, чтобы не тянуть старую геометрию.
        state_.current = SliceVoice{};
        state_.next = SliceVoice{};
        state_.inTransition = false;
        state_.transitionSamples = 0U;
        state_.transitionPos = 0U;
        return true;
    }
    return false;
}

void BufferFxModule::startVoice_(const SliceVoice& voice, bool crossfade) noexcept {
    if (!voice.active) {
        return;
    }

    if (!crossfade || !state_.current.active) {
        state_.current = voice;
        state_.next = SliceVoice{};
        state_.inTransition = false;
        state_.transitionSamples = 0U;
        state_.transitionPos = 0U;
        return;
    }

    state_.next = voice;
    const uint32_t baseSlice = std::max<uint32_t>(voice.sliceLength, state_.current.sliceLength);
    const uint32_t edge = std::max<uint32_t>(8U, std::min<uint32_t>(kMaxCrossfadeSamples, baseSlice / 4U));
    state_.inTransition = true;
    state_.transitionSamples = edge;
    state_.transitionPos = 0U;
}

void BufferFxModule::triggerSliceAtSample_(uint64_t quantizedSample,
                                           uint64_t nowSample,
                                           uint32_t bufferSizeSamples,
                                           uint32_t sliceSamples,
                                           uint32_t repeatCount,
                                           bool reverse,
                                           float speed,
                                           float jitterAmount) noexcept {
    if (bufferSizeSamples == 0U) {
        state_.active = false;
        state_.current = SliceVoice{};
        state_.next = SliceVoice{};
        state_.inTransition = false;
        state_.transitionSamples = 0U;
        state_.transitionPos = 0U;
        return;
    }

    const uint32_t safeSlice = clampToRangeU32_(
        sliceSamples,
        kMinSliceSamples,
        std::max<uint32_t>(kMinSliceSamples, bufferSizeSamples - 1U));

    const uint64_t maxDelay = static_cast<uint64_t>(bufferSizeSamples - 1U);
    const uint64_t delay = std::min<uint64_t>(
        (nowSample >= quantizedSample) ? (nowSample - quantizedSample) : 0U,
        maxDelay);
    const uint32_t quantizedPos = wrapIndex_(static_cast<int64_t>(writePos_) - static_cast<int64_t>(delay), bufferSizeSamples);

    (void)jitterAmount;
    const float safeSpeed = clampToRangeF_(speed, kMinSpeed, kMaxSpeed);
    const uint32_t effRepeat = effectiveRepeatCount_(repeatCount, state_.retrigIntervalSamples, safeSlice, safeSpeed);

    SliceVoice voice{};
    voice.active = true;
    voice.sliceStart = quantizedPos;
    voice.sliceLength = safeSlice;
    voice.repeatsLeft = effRepeat;
    voice.localPlayhead = 0.0f;
    voice.reverse = reverse;
    voice.speed = safeSpeed;

    startVoice_(voice, state_.current.active || (state_.inTransition && state_.next.active));
    state_.active = true;
}

float BufferFxModule::dcBlock_(float x, float& x1, float& y1) noexcept {
    const float y = x - x1 + kDcBlockR * y1;
    x1 = x;
    y1 = y;
    return y;
}

} // namespace avantgarde
