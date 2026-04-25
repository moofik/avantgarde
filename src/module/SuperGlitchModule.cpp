#include "module/SuperGlitchModule.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace avantgarde {
namespace {

constexpr float kMinBpm = 20.0f;
constexpr float kMaxBpm = 300.0f;
constexpr float kDcBlockR = 0.995f;
constexpr float kWetSmoothAlpha = 0.22f;
constexpr uint32_t kMinSliceSamples = 8U;
constexpr uint32_t kMaxCrossfadeSamples = 128U;
constexpr double kMaxBufferSeconds = 8.0;
constexpr float kFreePhraseSeconds = 4.0f;

// Retrig divisions in bars: off, 1/32, 1/16, 1/8, 1/6(T), 1/4, 1/3(T), 1/2
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

constexpr std::array<float, 4> kPhraseBars = {
    1.0f / 4.0f,
    1.0f / 2.0f,
    1.0f,
    2.0f,
};

constexpr std::array<uint32_t, 5> kSubsliceDiv = {1U, 2U, 4U, 8U, 16U};

} // namespace

SuperGlitchModule::SuperGlitchModule() noexcept {
    meta_[P_MIX] = {"Mix", 0.0f, 1.0f, false, "norm"};
    meta_[P_SUBSLICE] = {"Subslice", 0.0f, 1.0f, false, "sync"};
    meta_[P_HOLD] = {"Hold", 0.0f, 1.0f, false, "norm"};
    meta_[P_SPEED] = {"Speed", 0.0f, 1.0f, false, "rate"};
    meta_[P_MODE] = {"Mode", 0.0f, 1.0f, false, "bool"};
    meta_[P_PHRASE] = {"Phrase", 0.0f, 1.0f, false, "sync"};
    meta_[P_RETRIG] = {"Retrig", 0.0f, 1.0f, false, "sync"};
    meta_[P_REVERSE] = {"Reverse", 0.0f, 1.0f, false, "bool"};
}

void SuperGlitchModule::init(double sampleRate, std::size_t /*maxFrames*/) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    ringCapacity_ = std::max<uint32_t>(1024U, static_cast<uint32_t>(std::lround(sampleRate_ * kMaxBufferSeconds)));
    phraseCapture_.init(sampleRate_, ringCapacity_);
    localSampleCounter_ = 0U;
    effectiveBpm_ = 120.0f;
    read_ = write_.load(std::memory_order_relaxed);
    state_ = RtState{};
}

void SuperGlitchModule::beginBlock() noexcept {
    read_ = write_.load(std::memory_order_relaxed);
}

void SuperGlitchModule::process(const AudioProcessContext& ctx) noexcept {
    if (!ctx.in || !ctx.out || ringCapacity_ == 0U ||
        phraseCapture_.leftRing().empty() || phraseCapture_.rightRing().empty()) {
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

    const uint32_t mappedPhrase = mapPhraseSamples_(
        read_.phrase, sampleRate_, bpm, ctx.transportTsNum, ctx.transportTsDen, ringCapacity_);
    const uint32_t mappedSubslice = mapSubsliceSamples_(read_.subslice, mappedPhrase);
    const uint32_t mappedRetrig = mapRetrigIntervalSamples_(
        read_.retrig, sampleRate_, bpm, ctx.transportTsNum, ctx.transportTsDen);
    const bool mappedReverse = (read_.reverse >= 0.5f);
    const bool mappedChoppy = mapChoppy_(read_.mode);
    const float mappedSpeed = mapSpeed_(read_.speed);
    const float holdDuty = mapHoldDuty_(read_.hold);
    const float mix = clamp01_(read_.mix);
    const float dry = 1.0f - mix;
    const uint64_t blockStartSample = ctx.transportValid ? ctx.transportSampleTime : localSampleCounter_;

    // Если transport время прыгнуло назад (stop/start/rewind),
    // переинициализируем retrig-сетку относительно нового времени.
    // Иначе boundary может уехать далеко в будущее, и FX выглядит "мертвым".
    if (ctx.transportValid && state_.lastRetrigSample > (blockStartSample + static_cast<uint64_t>(ctx.nframes))) {
        state_.lastRetrigSample = blockStartSample;
        if (state_.retrigIntervalSamples > 0U) {
            state_.nextRetrigSample =
                ((blockStartSample / static_cast<uint64_t>(state_.retrigIntervalSamples)) + 1ULL) *
                static_cast<uint64_t>(state_.retrigIntervalSamples);
        } else {
            state_.nextRetrigSample = 0U;
        }
        // Перезапускаем голоса: после rewind старые phase/границы невалидны
        // и могли уводить эффект в "тишину".
        state_.active = false;
        state_.current = SliceVoice{};
        state_.next = SliceVoice{};
        state_.inTransition = false;
        state_.transitionSamples = 0U;
        state_.transitionPos = 0U;
        state_.isPhraseCaptured = false;
    }

    // Retrig — тайминг-параметр, должен реагировать сразу, а не ждать phrase-boundary.
    if (mappedRetrig != state_.retrigIntervalSamples) {
        state_.retrigIntervalSamples = mappedRetrig;
        state_.lastRetrigSample = blockStartSample;
        if (state_.retrigIntervalSamples > 0U) {
            state_.nextRetrigSample =
                ((blockStartSample / static_cast<uint64_t>(state_.retrigIntervalSamples)) + 1ULL) *
                static_cast<uint64_t>(state_.retrigIntervalSamples);
        } else {
            state_.nextRetrigSample = 0U;
        }
    }

    if (!state_.active) {
        setActivePhraseParams_(mappedPhrase, mappedSubslice, mappedReverse, mappedChoppy);
    } else if (hasPendingPhraseDiff_(mappedPhrase, mappedSubslice, mappedReverse, mappedChoppy)) {
        state_.pendingPhraseParams = true;
        state_.pendingPhraseLength = mappedPhrase;
        state_.pendingSubsliceLength = mappedSubslice;
        state_.pendingReverse = mappedReverse;
        state_.pendingChoppy = mappedChoppy;
    } else {
        state_.pendingPhraseParams = false;
    }
    state_.speed = mappedSpeed;
    state_.holdDuty = holdDuty;

    const auto& ringL = phraseCapture_.leftRing();
    const auto& ringR = phraseCapture_.rightRing();

    const auto anyVoiceActive = [this]() noexcept {
        return state_.current.active || (state_.inTransition && state_.next.active);
    };

    for (std::size_t i = 0; i < ctx.nframes; ++i) {
        const float xL = inL[i];
        const float xR = inR ? inR[i] : xL;

        phraseCapture_.pushSample(xL, xR);

        const uint64_t absSample = (ctx.transportValid ? ctx.transportSampleTime : localSampleCounter_) + i;

        if (!state_.active) {
            state_.active = true;
            capturePhraseAtSample_(
                absSample,
                absSample,
                state_.phraseLength,
                state_.subsliceLength,
                state_.reverse,
                state_.choppy,
                state_.speed);
            if (state_.retrigIntervalSamples > 0U) {
                state_.nextRetrigSample =
                    ((absSample / static_cast<uint64_t>(state_.retrigIntervalSamples)) + 1ULL) *
                    static_cast<uint64_t>(state_.retrigIntervalSamples);
                state_.lastRetrigSample = absSample;
            }
        } else {
            // При retrig=off изменения phrase/subslice/reverse/mode применяем сразу
            // (иначе при бесконечном loop они могли "залипнуть" надолго).
            if (state_.retrigIntervalSamples == 0U && state_.pendingPhraseParams) {
                applyPendingPhraseParams_();
                capturePhraseAtSample_(
                    absSample,
                    absSample,
                    state_.phraseLength,
                    state_.subsliceLength,
                    state_.reverse,
                    state_.choppy,
                    state_.speed);
            }

            if (state_.retrigIntervalSamples > 0U) {
                uint32_t guard = 0U;
                while (absSample >= state_.nextRetrigSample) {
                    const uint64_t boundarySample = state_.nextRetrigSample;
                    applyPendingPhraseParams_();

                    if (!state_.isPhraseCaptured || state_.pendingPhraseParams) {
                        capturePhraseAtSample_(
                            boundarySample,
                            absSample,
                            state_.phraseLength,
                            state_.subsliceLength,
                            state_.reverse,
                            state_.choppy,
                            state_.speed);
                    } else {
                        restartFromCurrentPhrase_(boundarySample);
                    }
                    state_.lastRetrigSample = boundarySample;
                    state_.nextRetrigSample = boundarySample + static_cast<uint64_t>(state_.retrigIntervalSamples);
                    // Защита от pathological-loop в случае некорректного тайм-скачка:
                    // не позволяем RT-потоку застрять в while.
                    if (++guard > 2048U) {
                        state_.nextRetrigSample = absSample + static_cast<uint64_t>(state_.retrigIntervalSamples);
                        break;
                    }
                }
            } else if (!anyVoiceActive()) {
                applyPendingPhraseParams_();
                if (!state_.isPhraseCaptured) {
                    capturePhraseAtSample_(
                        absSample,
                        absSample,
                        state_.phraseLength,
                        state_.subsliceLength,
                        state_.reverse,
                        state_.choppy,
                        state_.speed);
                } else {
                    restartFromCurrentPhrase_(absSample);
                }
            }
        }

        if (state_.current.active) {
            state_.current.speed = state_.speed;
        }
        if (state_.next.active) {
            state_.next.speed = state_.speed;
        }

        float wetL = wetHoldL_;
        float wetR = wetHoldR_;
        if (state_.active && anyVoiceActive()) {
            float curL = 0.0f;
            float curR = 0.0f;
            if (state_.current.active) {
                curL = GlitchVoice::renderSample(state_.current, ringL, ringCapacity_);
                curR = GlitchVoice::renderSample(state_.current, ringR, ringCapacity_);
                GlitchVoice::advance(state_.current, ringCapacity_);
            }

            if (state_.inTransition && state_.next.active) {
                const float nxtL = GlitchVoice::renderSample(state_.next, ringL, ringCapacity_);
                const float nxtR = GlitchVoice::renderSample(state_.next, ringR, ringCapacity_);
                GlitchVoice::advance(state_.next, ringCapacity_);

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
                    state_.transitionPos = 0U;
                    state_.transitionSamples = 0U;
                }
            } else {
                wetL = curL;
                wetR = curR;
            }

            // Gate/restart density. Не пускаем dry-signal в wet-path: gate -> только амплитуда wet.
            // Gate в v1 применяем только для CHOPPY-режима.
            if (state_.choppy &&
                state_.retrigIntervalSamples > 0U &&
                state_.lastRetrigSample <= absSample) {
                const uint64_t phase = absSample - state_.lastRetrigSample;
                const uint64_t duty = static_cast<uint64_t>(std::lround(state_.holdDuty *
                                                                        static_cast<float>(state_.retrigIntervalSamples)));
                const float g = (phase <= duty) ? 1.0f : 0.0f;
                wetL *= g;
                wetR *= g;
            }

            wetLpL_ += kWetSmoothAlpha * (wetL - wetLpL_);
            wetLpR_ += kWetSmoothAlpha * (wetR - wetLpR_);
            wetL = wetLpL_;
            wetR = wetLpR_;
            wetHoldL_ = wetL;
            wetHoldR_ = wetR;
        }

        float yL = dry * xL + mix * wetL;
        float yR = dry * xR + mix * wetR;
        if (state_.active && mix >= 0.35f) {
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

void SuperGlitchModule::reset() {
    phraseCapture_.reset();
    localSampleCounter_ = 0U;
    state_ = RtState{};
    wetLpL_ = 0.0f;
    wetLpR_ = 0.0f;
    wetHoldL_ = 0.0f;
    wetHoldR_ = 0.0f;
    dcXL1_ = 0.0f;
    dcYL1_ = 0.0f;
    dcXR1_ = 0.0f;
    dcYR1_ = 0.0f;
    effectiveBpm_ = 120.0f;
}

std::size_t SuperGlitchModule::getParamCount() const { return NUM_PARAMS; }

float SuperGlitchModule::getParam(std::size_t index) const {
    switch (index) {
        case P_MIX: return read_.mix;
        case P_SUBSLICE: return read_.subslice;
        case P_HOLD: return read_.hold;
        case P_SPEED: return read_.speed;
        case P_MODE: return read_.mode;
        case P_PHRASE: return read_.phrase;
        case P_RETRIG: return read_.retrig;
        case P_REVERSE: return read_.reverse;
        default: return 0.0f;
    }
}

void SuperGlitchModule::setParam(std::size_t index, float value) {
    Params p = write_.load(std::memory_order_relaxed);
    const float v = clamp01_(value);
    switch (index) {
        case P_MIX: p.mix = v; break;
        case P_SUBSLICE: p.subslice = v; break;
        case P_HOLD: p.hold = v; break;
        case P_SPEED: p.speed = v; break;
        case P_MODE: p.mode = v; break;
        case P_PHRASE: p.phrase = v; break;
        case P_RETRIG: p.retrig = v; break;
        case P_REVERSE: p.reverse = v; break;
        default: return;
    }
    write_.store(p, std::memory_order_relaxed);
}

const ParamMeta& SuperGlitchModule::getParamMeta(std::size_t index) const {
    static const ParamMeta kFallback{"Param", 0.0f, 1.0f, false, "norm"};
    return (index < meta_.size()) ? meta_[index] : kFallback;
}

float SuperGlitchModule::clamp01_(float v) noexcept {
    return clampToRangeF_(v, 0.0f, 1.0f);
}

uint32_t SuperGlitchModule::clampToRangeU32_(uint32_t v, uint32_t lo, uint32_t hi) noexcept {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

float SuperGlitchModule::clampToRangeF_(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) {
        return lo;
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float SuperGlitchModule::beatsPerBar_(uint8_t tsNum, uint8_t tsDen) noexcept {
    const float den = (tsDen == 0U) ? 4.0f : static_cast<float>(tsDen);
    return static_cast<float>(tsNum) * (4.0f / den);
}

float SuperGlitchModule::mapSpeed_(float speed01) noexcept {
    const float t = clamp01_(speed01);
    if (t < 0.5f) {
        const float u = t / 0.5f;
        return 0.25f + (1.0f - 0.25f) * u;
    }
    const float u = (t - 0.5f) / 0.5f;
    return 1.0f + (2.0f - 1.0f) * u;
}

float SuperGlitchModule::mapHoldDuty_(float hold01) noexcept {
    const float t = clamp01_(hold01);
    return 0.35f + 0.65f * t;
}

bool SuperGlitchModule::mapChoppy_(float mode01) noexcept {
    return clamp01_(mode01) >= 0.5f;
}

uint32_t SuperGlitchModule::mapPhraseSamples_(float phrase01,
                                              double sampleRate,
                                              float bpm,
                                              uint8_t tsNum,
                                              uint8_t tsDen,
                                              uint32_t ringCapacity) noexcept {
    if (ringCapacity == 0U) {
        return 1U;
    }
    const float t = clamp01_(phrase01);
    if (t > 0.92f) {
        const uint32_t freeLen = static_cast<uint32_t>(std::lround(kFreePhraseSeconds * sampleRate));
        return clampToRangeU32_(freeLen, kMinSliceSamples, ringCapacity);
    }
    const std::size_t idx = static_cast<std::size_t>(
        std::clamp<int>(static_cast<int>(std::lround(t * static_cast<float>(kPhraseBars.size() - 1U))),
                        0,
                        static_cast<int>(kPhraseBars.size() - 1U)));
    const float beatsBar = beatsPerBar_(tsNum, tsDen);
    const float samplesPerBeat = static_cast<float>(sampleRate) * (60.0f / clampToRangeF_(bpm, kMinBpm, kMaxBpm));
    const float bars = kPhraseBars[idx];
    const uint32_t s = static_cast<uint32_t>(std::lround(samplesPerBeat * beatsBar * bars));
    return clampToRangeU32_(s, kMinSliceSamples, ringCapacity);
}

uint32_t SuperGlitchModule::mapSubsliceSamples_(float subslice01, uint32_t phraseSamples) noexcept {
    const float t = clamp01_(subslice01);
    const std::size_t idx = static_cast<std::size_t>(
        std::clamp<int>(static_cast<int>(std::lround(t * static_cast<float>(kSubsliceDiv.size() - 1U))),
                        0,
                        static_cast<int>(kSubsliceDiv.size() - 1U)));
    const uint32_t div = kSubsliceDiv[idx];
    const uint32_t out = (div > 0U) ? (phraseSamples / div) : phraseSamples;
    return std::max<uint32_t>(kMinSliceSamples, out);
}

uint32_t SuperGlitchModule::mapRetrigIntervalSamples_(float retrig01,
                                                      double sampleRate,
                                                      float bpm,
                                                      uint8_t tsNum,
                                                      uint8_t tsDen) noexcept {
    const float t = clamp01_(retrig01);
    const std::size_t idx = static_cast<std::size_t>(
        std::clamp<int>(static_cast<int>(std::lround(t * static_cast<float>(kRetrigBars.size() - 1U))),
                        0,
                        static_cast<int>(kRetrigBars.size() - 1U)));
    const float bars = kRetrigBars[idx];
    if (bars <= 0.0f) {
        return 0U;
    }
    const float beatsBar = beatsPerBar_(tsNum, tsDen);
    const float samplesPerBeat = static_cast<float>(sampleRate) * (60.0f / clampToRangeF_(bpm, kMinBpm, kMaxBpm));
    const uint32_t s = static_cast<uint32_t>(std::lround(samplesPerBeat * beatsBar * bars));
    return std::max<uint32_t>(1U, s);
}

uint32_t SuperGlitchModule::wrapIndex_(int64_t i, uint32_t size) noexcept {
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

void SuperGlitchModule::startVoice_(const SliceVoice& voice, bool crossfade) noexcept {
    if (!crossfade || !state_.current.active) {
        state_.current = voice;
        state_.next = SliceVoice{};
        state_.inTransition = false;
        state_.transitionSamples = 0U;
        state_.transitionPos = 0U;
        return;
    }

    state_.next = voice;
    state_.inTransition = true;
    state_.transitionPos = 0U;
    const uint32_t edge = std::min<uint32_t>(voice.subsliceLength / 4U, kMaxCrossfadeSamples);
    state_.transitionSamples = std::max<uint32_t>(8U, edge);
}

void SuperGlitchModule::capturePhraseAtSample_(uint64_t quantizedSample,
                                               uint64_t nowSample,
                                               uint32_t phraseLength,
                                               uint32_t subsliceLength,
                                               bool reverse,
                                               bool choppy,
                                               float speed) noexcept {
    if (ringCapacity_ == 0U) {
        return;
    }
    const uint32_t safePhraseWanted = clampToRangeU32_(phraseLength, 1U, ringCapacity_);
    const CapturedPhrase captured =
        phraseCapture_.capturePhraseAtSample(quantizedSample, nowSample, safePhraseWanted);
    if (!captured.valid || captured.length == 0U) {
        return;
    }
    const uint32_t safePhrase = captured.length;
    const uint32_t safeSubsliceWanted = clampToRangeU32_(subsliceLength, 1U, safePhrase);
    const uint32_t safeSubslice = std::max<uint32_t>(1U, std::min<uint32_t>(safeSubsliceWanted, safePhrase));
    const uint32_t start = captured.start;

    state_.phraseStart = start;
    state_.phraseLength = safePhrase;
    state_.subsliceLength = safeSubslice;
    state_.subsliceStart = start;
    state_.playheadInPhrase = 0.0f;
    state_.isPhraseCaptured = true;

    SliceVoice v{};
    v.active = true;
    v.phraseStart = start;
    v.phraseLength = safePhrase;
    v.subsliceStart = state_.subsliceStart;
    v.subsliceLength = safeSubslice;
    v.localPlayhead = 0.0f;
    v.speed = speed;
    v.reverse = reverse;
    v.choppy = choppy;
    startVoice_(v, true);
}

void SuperGlitchModule::restartFromCurrentPhrase_(uint64_t boundarySample) noexcept {
    if (!state_.isPhraseCaptured) {
        return;
    }
    if (state_.choppy && state_.phraseLength > state_.subsliceLength) {
        const uint32_t rel = wrapIndex_(
            static_cast<int64_t>(state_.subsliceStart) - static_cast<int64_t>(state_.phraseStart),
            state_.phraseLength);
        const uint32_t nextRel = wrapIndex_(static_cast<int64_t>(rel) + static_cast<int64_t>(state_.subsliceLength),
                                            state_.phraseLength);
        state_.subsliceStart = wrapIndex_(
            static_cast<int64_t>(state_.phraseStart) + static_cast<int64_t>(nextRel),
            ringCapacity_);
    }

    SliceVoice v{};
    v.active = true;
    v.phraseStart = state_.phraseStart;
    v.phraseLength = state_.phraseLength;
    v.subsliceStart = state_.subsliceStart;
    v.subsliceLength = state_.subsliceLength;
    v.localPlayhead = 0.0f;
    v.speed = state_.speed;
    v.reverse = state_.reverse;
    v.choppy = state_.choppy;
    startVoice_(v, true);
    state_.lastRetrigSample = boundarySample;
}

void SuperGlitchModule::setActivePhraseParams_(uint32_t phraseLength,
                                               uint32_t subsliceLength,
                                               bool reverse,
                                               bool choppy) noexcept {
    state_.phraseLength = clampToRangeU32_(phraseLength, kMinSliceSamples, std::max<uint32_t>(kMinSliceSamples, ringCapacity_));
    state_.subsliceLength = clampToRangeU32_(subsliceLength, kMinSliceSamples, state_.phraseLength);
    state_.reverse = reverse;
    state_.choppy = choppy;
}

bool SuperGlitchModule::hasPendingPhraseDiff_(uint32_t phraseLength,
                                              uint32_t subsliceLength,
                                              bool reverse,
                                              bool choppy) const noexcept {
    const uint32_t safePhrase = clampToRangeU32_(phraseLength, kMinSliceSamples, std::max<uint32_t>(kMinSliceSamples, ringCapacity_));
    const uint32_t safeSubslice = clampToRangeU32_(subsliceLength, kMinSliceSamples, safePhrase);
    return safePhrase != state_.phraseLength ||
           safeSubslice != state_.subsliceLength ||
           reverse != state_.reverse ||
           choppy != state_.choppy;
}

void SuperGlitchModule::applyPendingPhraseParams_() noexcept {
    if (!state_.pendingPhraseParams) {
        return;
    }
    setActivePhraseParams_(state_.pendingPhraseLength,
                           state_.pendingSubsliceLength,
                           state_.pendingReverse,
                           state_.pendingChoppy);
    state_.isPhraseCaptured = false; // на следующей boundary будет новый capture phrase.
    state_.pendingPhraseParams = false;
}

float SuperGlitchModule::dcBlock_(float x, float& x1, float& y1) noexcept {
    const float y = x - x1 + kDcBlockR * y1;
    x1 = x;
    y1 = y;
    return y;
}

} // namespace avantgarde
