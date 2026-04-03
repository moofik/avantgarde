#include "module/StutterModule.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace avantgarde {
namespace {

constexpr float kMinBpm = 20.0f;
constexpr float kMaxBpm = 300.0f;
constexpr std::size_t kMaxCrossfadeSamples = 24;

} // namespace

StutterModule::StutterModule() noexcept {
    meta_[P_WET] = {"Wet", 0.0f, 1.0f, false, "norm"};
    meta_[P_RATE] = {"Rate", 0.0f, 1.0f, false, "sync"};
    meta_[P_GATE] = {"Gate", 0.0f, 1.0f, false, "norm"};
    meta_[P_RETRIGGER] = {"Retrigger", 0.0f, 1.0f, false, "enum"};
}

void StutterModule::init(double sampleRate, std::size_t /*maxFrames*/) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    // Буфер с запасом под медленный темп и длинный sync-step.
    // Нужен минимум > 12 cек при 20 BPM и rate=1 bar (4 beats), поэтому берем 16 сек.
    ringSize_ = static_cast<std::size_t>(std::max<double>(sampleRate_ * 16.0, 4096.0));
    ringL_.assign(ringSize_, 0.0f);
    ringR_.assign(ringSize_, 0.0f);
    writePos_ = 0;
    localSampleCounter_ = 0;
    read_ = write_.load(std::memory_order_relaxed);
}

void StutterModule::beginBlock() noexcept {
    read_ = write_.load(std::memory_order_relaxed);
}

void StutterModule::process(const AudioProcessContext& ctx) noexcept {
    if (!ctx.in || !ctx.out || ringSize_ == 0U || ringL_.empty() || ringR_.empty()) {
        return;
    }
    const float* inL = ctx.in[0];
    const float* inR = ctx.in[1] ? ctx.in[1] : ctx.in[0];
    float* outL = ctx.out[0];
    float* outR = ctx.out[1] ? ctx.out[1] : ctx.out[0];
    if (!inL || !outL) {
        return;
    }

    const float bpm = std::clamp(ctx.transportBpm, kMinBpm, kMaxBpm);
    const float beatsPerStep = mapRateToBeats_(read_.rate);
    const double stepSamplesF = sampleRate_ * (60.0 / static_cast<double>(bpm)) * static_cast<double>(beatsPerStep);
    const std::size_t stepSamples = std::max<std::size_t>(1U, static_cast<std::size_t>(std::llround(stepSamplesF)));

    const uint8_t retrigCount = mapRetriggerCount_(read_.retrigger);
    const bool retriggerEnabled = retrigCount > 0U;

    // Gate живет в rate-домене (как и раньше): rate задает период чопа.
    const std::size_t gateStepSamples = stepSamples;
    const std::size_t gateSamples = static_cast<std::size_t>(
        std::llround(clamp01_(read_.gate) * static_cast<float>(gateStepSamples)));

    // Retrigger живет в bar-домене: это дает музыкально длинные повторяемые куски
    // и режим "раз в bar", который ожидается от лупер/stutter UX.
    float beatsPerBar = 4.0f;
    if (ctx.transportTsNum > 0U && ctx.transportTsDen > 0U) {
        beatsPerBar = std::max(1.0f, static_cast<float>(ctx.transportTsNum) * (4.0f / static_cast<float>(ctx.transportTsDen)));
    }
    const double barSamplesF = sampleRate_ * (60.0 / static_cast<double>(bpm)) * static_cast<double>(beatsPerBar);
    const std::size_t barSamples = std::max<std::size_t>(1U, static_cast<std::size_t>(std::llround(barSamplesF)));
    // retrigPeriodSamples = длина одного "окна перезахвата".
    // Примеры при 4/4:
    // - retrigCount=1  -> 1 раз за бар (1 окно = целый bar),
    // - retrigCount=2  -> каждые 1/2 bar,
    // - retrigCount=4  -> каждые 1/4 bar,
    // - retrigCount=8  -> каждые 1/8 bar,
    // - retrigCount=16 -> каждые 1/16 bar.
    const std::size_t retrigPeriodSamples = retriggerEnabled
                                                ? std::max<std::size_t>(1U, barSamples / static_cast<std::size_t>(retrigCount))
                                                : gateStepSamples;

    const float wet = clamp01_(read_.wet);
    const float dry = 1.0f - wet;

    // Если transport подключен и остановлен, модуль не модулирует сигнал.
    const bool modulationActive = (!ctx.transportValid) || ctx.transportPlaying;

    for (std::size_t i = 0; i < ctx.nframes; ++i) {
        const float xL = inL[i];
        const float xR = inR ? inR[i] : xL;

        const std::size_t curWritePos = writePos_;
        ringL_[curWritePos] = xL;
        ringR_[curWritePos] = xR;
        writePos_ = (writePos_ + 1U >= ringSize_) ? 0U : writePos_ + 1U;

        if (!modulationActive) {
            outL[i] = xL;
            outR[i] = xR;
            continue;
        }

        const uint64_t absSample = (ctx.transportValid ? ctx.transportSampleTime : localSampleCounter_) + i;
        const std::size_t indexInGateStep = static_cast<std::size_t>(absSample % static_cast<uint64_t>(gateStepSamples));
        const float gateEnv = gateEnvelope_(indexInGateStep, gateSamples, gateStepSamples);

        float stL = xL;
        float stR = xR;

        if (retriggerEnabled && absSample >= static_cast<uint64_t>(gateStepSamples)) {
            // Настоящий stutter/freeze:
            // - rate определяет длину зацикливаемого куска (gateStepSamples),
            // - retrigger определяет, как часто мы берём новый кусок по bar-grid.
            //
            // Между retrigger-событиями крутится один и тот же кусок,
            // поэтому эффект слышен стабильно, а не только в момент изменения параметра.
            const std::size_t indexInRetrigPeriod =
                static_cast<std::size_t>(absSample % static_cast<uint64_t>(retrigPeriodSamples));
            const uint64_t retrigPeriodStart = absSample - static_cast<uint64_t>(indexInRetrigPeriod);
            const uint64_t loopChunkStart = retrigPeriodStart - static_cast<uint64_t>(gateStepSamples);
            const std::size_t loopPhase = indexInRetrigPeriod % gateStepSamples;
            const uint64_t readAbs = loopChunkStart + static_cast<uint64_t>(loopPhase);
            const std::size_t delay = static_cast<std::size_t>(absSample - readAbs);

            stL = sampleAtDelay_(ringL_, curWritePos, delay);
            stR = sampleAtDelay_(ringR_, curWritePos, delay);
        }

        stL *= gateEnv;
        stR *= gateEnv;

        outL[i] = dry * xL + wet * stL;
        outR[i] = dry * xR + wet * stR;
    }

    if (!ctx.transportValid) {
        localSampleCounter_ += static_cast<uint64_t>(ctx.nframes);
    }
}

void StutterModule::reset() {
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writePos_ = 0U;
    localSampleCounter_ = 0U;
}

std::size_t StutterModule::getParamCount() const {
    return NUM_PARAMS;
}

float StutterModule::getParam(std::size_t index) const {
    switch (index) {
        case P_WET: return read_.wet;
        case P_RATE: return read_.rate;
        case P_GATE: return read_.gate;
        case P_RETRIGGER: return read_.retrigger;
        default: return 0.0f;
    }
}

void StutterModule::setParam(std::size_t index, float value) {
    Params p = write_.load(std::memory_order_relaxed);
    switch (index) {
        case P_WET:
            p.wet = clamp01_(value);
            break;
        case P_RATE:
            p.rate = clamp01_(value);
            break;
        case P_GATE:
            p.gate = clamp01_(value);
            break;
        case P_RETRIGGER:
            p.retrigger = clamp01_(value);
            break;
        default:
            break;
    }
    write_.store(p, std::memory_order_relaxed);
}

const ParamMeta& StutterModule::getParamMeta(std::size_t index) const {
    return meta_[index];
}

float StutterModule::clamp01_(float v) noexcept {
    return std::clamp(v, 0.0f, 1.0f);
}

float StutterModule::mapRateToBeats_(float rate01) noexcept {
    // Монотонная карта длительностей от медленно -> быстро.
    // Явные пороги дают стабильное UX-поведение без реверсов в середине ручки.
    const float r = clamp01_(rate01);
    if (r < 0.10f) return 4.0f;         // 1 bar
    if (r < 0.20f) return 2.0f;         // 1/2
    if (r < 0.32f) return 1.0f;         // 1/4
    if (r < 0.44f) return 0.5f;         // 1/8
    if (r < 0.56f) return 1.0f / 3.0f;  // 1/8T
    if (r < 0.68f) return 0.25f;        // 1/16
    if (r < 0.78f) return 1.0f / 6.0f;  // 1/16T
    if (r < 0.88f) return 0.125f;       // 1/32
    if (r < 0.95f) return 1.0f / 12.0f; // 1/32T
    return 1.0f / 16.0f;                // 1/64
}

uint8_t StutterModule::mapRetriggerCount_(float retrig01) noexcept {
    // Дискретные ступени retrig-контрола:
    // OFF -> 0 (ретриггер выключен, работает только gate),
    // 1   -> 1 перезахват за bar,
    // 2   -> 2 перезахвата за bar,
    // 4   -> 4 перезахвата за bar,
    // 8   -> 8 перезахватов за bar,
    // 16  -> 16 перезахватов за bar.
    //
    // Это именно частота "перезахвата нового куска".
    // Длина самого повторяемого куска задается ручкой RATE.
    const float r = clamp01_(retrig01);
    if (r < 0.05f) return 0;
    if (r < 0.25f) return 1;
    if (r < 0.45f) return 2;
    if (r < 0.65f) return 4;
    if (r < 0.85f) return 8;
    return 16;
}

float StutterModule::sampleAtDelay_(const std::vector<float>& ring,
                                    std::size_t writePos,
                                    std::size_t delay) const noexcept {
    if (ring.empty() || ringSize_ == 0U) {
        return 0.0f;
    }
    const std::size_t d = (delay >= ringSize_) ? (ringSize_ - 1U) : delay;
    const std::size_t idx = (writePos + ringSize_ - d) % ringSize_;
    return ring[idx];
}

float StutterModule::gateEnvelope_(std::size_t indexInSubStep,
                                   std::size_t gateSamples,
                                   std::size_t subStepSamples) const noexcept {
    if (gateSamples == 0U) {
        return 0.0f;
    }
    // Полностью открытый gate не должен добавлять внутренние рампы
    // и модулировать сигнал.
    if (gateSamples >= subStepSamples) {
        return 1.0f;
    }
    if (indexInSubStep >= gateSamples) {
        return 0.0f;
    }

    float env = 1.0f;
    const std::size_t edge = std::max<std::size_t>(
        1U, std::min<std::size_t>(kMaxCrossfadeSamples, gateSamples / 8U));

    if (indexInSubStep < edge) {
        env *= static_cast<float>(indexInSubStep) / static_cast<float>(edge);
    }
    const std::size_t tail = gateSamples - indexInSubStep;
    if (tail < edge) {
        env *= static_cast<float>(tail) / static_cast<float>(edge);
    }
    return std::clamp(env, 0.0f, 1.0f);
}

} // namespace avantgarde
