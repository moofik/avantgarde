#include "runtime/TransportBridgeDualBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "contracts/ids.h"

namespace avantgarde {
namespace {

uint8_t normalizeDenominator(uint8_t den) noexcept {
    switch (den) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
            return den;
        default:
            return 4;
    }
}

float normalizeTempo(float bpm) noexcept {
    if (!std::isfinite(bpm)) return 120.0f;
    return std::clamp(bpm, 20.0f, 300.0f);
}

float normalizeSwing(float swing) noexcept {
    if (!std::isfinite(swing)) return 0.0f;
    return std::clamp(swing, 0.0f, 1.0f);
}

uint8_t normalizeTsNum(uint8_t num) noexcept {
    return num == 0 ? static_cast<uint8_t>(4) : num;
}

float clamp01(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

float tempoFromNorm(float norm) noexcept {
    constexpr float kMin = 20.0f;
    constexpr float kMax = 300.0f;
    return kMin + clamp01(norm) * (kMax - kMin);
}

float tempoToNorm(float bpm) noexcept {
    constexpr float kMin = 20.0f;
    constexpr float kMax = 300.0f;
    const float clamped = normalizeTempo(bpm);
    return (clamped - kMin) / (kMax - kMin);
}

QuantizeMode quantFromNorm(float norm) noexcept {
    const float v = clamp01(norm);
    if (v < (1.0f / 3.0f)) return QuantizeMode::None;
    if (v < (2.0f / 3.0f)) return QuantizeMode::Beat;
    return QuantizeMode::Bar;
}

float quantToNorm(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return 0.0f;
        case QuantizeMode::Beat: return 0.5f;
        case QuantizeMode::Bar:
        default:
            return 1.0f;
    }
}

uint8_t tsNumFromNorm(float norm) noexcept {
    const float v = clamp01(norm);
    const int num = static_cast<int>(std::lround(1.0f + v * 31.0f));
    return normalizeTsNum(static_cast<uint8_t>(std::clamp(num, 1, 32)));
}

float tsNumToNorm(uint8_t num) noexcept {
    const int n = std::clamp<int>(normalizeTsNum(num), 1, 32);
    return static_cast<float>(n - 1) / 31.0f;
}

uint8_t tsDenFromNorm(float norm) noexcept {
    constexpr std::array<uint8_t, 6> kDenValues{{1, 2, 4, 8, 16, 32}};
    const float v = clamp01(norm);
    const std::size_t idx = static_cast<std::size_t>(
        std::clamp<int>(static_cast<int>(std::lround(v * static_cast<float>(kDenValues.size() - 1))), 0,
                        static_cast<int>(kDenValues.size() - 1)));
    return normalizeDenominator(kDenValues[idx]);
}

float tsDenToNorm(uint8_t den) noexcept {
    constexpr std::array<uint8_t, 6> kDenValues{{1, 2, 4, 8, 16, 32}};
    for (std::size_t i = 0; i < kDenValues.size(); ++i) {
        if (kDenValues[i] == den) {
            return static_cast<float>(i) / static_cast<float>(kDenValues.size() - 1);
        }
    }
    return 0.4f; // значение для "4"
}

const ParamMeta& noMeta() {
    static const ParamMeta k{
        .name = "transport.unknown",
        .minValue = 0.0f,
        .maxValue = 1.0f,
        .logarithmic = false,
        .unit = ""
    };
    return k;
}

const std::array<ParamMeta, 6>& transportMeta() {
    static const std::array<ParamMeta, 6> k{{
        ParamMeta{.name = "transport.playing", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "bool"},
        ParamMeta{.name = "transport.tempo_norm", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "norm"},
        ParamMeta{.name = "transport.quantize_norm", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "norm"},
        ParamMeta{.name = "transport.ts_num_norm", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "norm"},
        ParamMeta{.name = "transport.ts_den_norm", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "norm"},
        ParamMeta{.name = "transport.swing", .minValue = 0.0f, .maxValue = 1.0f, .logarithmic = false, .unit = "norm"},
    }};
    return k;
}

} // namespace

TransportBridgeDualBuffer::TransportBridgeDualBuffer() noexcept {
    transportRtSnapshot_.playing = playingWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.tsNum = tsNumWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.tsDen = tsDenWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.ppq = ppqWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.bpm = bpmWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.quant = static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_relaxed));
    transportRtSnapshot_.swing = swingWrite_.load(std::memory_order_relaxed);
    transportRtSnapshot_.sampleTime = 0;
}

void TransportBridgeDualBuffer::setPlaying(bool on) {
    playingWrite_.store(on, std::memory_order_relaxed);
}

void TransportBridgeDualBuffer::setTempo(float bpm) {
    bpmWrite_.store(normalizeTempo(bpm), std::memory_order_relaxed);
}

void TransportBridgeDualBuffer::setTimeSignature(uint8_t num, uint8_t den) {
    tsNumWrite_.store(normalizeTsNum(num), std::memory_order_relaxed);
    tsDenWrite_.store(normalizeDenominator(den), std::memory_order_relaxed);
}

void TransportBridgeDualBuffer::setQuantize(QuantizeMode q) {
    quantWrite_.store(static_cast<uint8_t>(q), std::memory_order_relaxed);
}

void TransportBridgeDualBuffer::setSwing(float s01) {
    swingWrite_.store(normalizeSwing(s01), std::memory_order_relaxed);
}

void TransportBridgeDualBuffer::swapBuffers() noexcept {
    transportRtSnapshot_.playing = playingWrite_.load(std::memory_order_acquire);
    transportRtSnapshot_.tsNum = tsNumWrite_.load(std::memory_order_acquire);
    transportRtSnapshot_.tsDen = tsDenWrite_.load(std::memory_order_acquire);
    transportRtSnapshot_.ppq = ppqWrite_.load(std::memory_order_acquire);
    transportRtSnapshot_.bpm = bpmWrite_.load(std::memory_order_acquire);
    transportRtSnapshot_.quant = static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_acquire));
    transportRtSnapshot_.swing = swingWrite_.load(std::memory_order_acquire);
}

const TransportRtSnapshot& TransportBridgeDualBuffer::rt() const noexcept {
    return transportRtSnapshot_;
}

void TransportBridgeDualBuffer::advanceSampleTime(uint64_t frames) noexcept {
    transportRtSnapshot_.sampleTime += frames;
}

bool TransportBridgeDualBuffer::getSnapshot(SnapshotRecord& out) const noexcept {
    out = SnapshotRecord{};
    out.domain = SnapshotDomain::Transport;
    out.entityId = kSnapshotEntityTransport;
    out.transport.playing = playingWrite_.load(std::memory_order_relaxed);
    out.transport.bpm = normalizeTempo(bpmWrite_.load(std::memory_order_relaxed));
    out.transport.tsNum = normalizeTsNum(tsNumWrite_.load(std::memory_order_relaxed));
    out.transport.tsDen = normalizeDenominator(tsDenWrite_.load(std::memory_order_relaxed));
    out.transport.quant = static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_relaxed));
    out.transport.swing01 = normalizeSwing(swingWrite_.load(std::memory_order_relaxed));
    out.transport.sampleTime = transportRtSnapshot_.sampleTime;
    return true;
}

std::size_t TransportBridgeDualBuffer::getParamCount() const {
    return transportMeta().size();
}

float TransportBridgeDualBuffer::getParam(std::size_t index) const {
    switch (static_cast<TransportParamId>(index)) {
        case TransportParamId::Playing:
            return playingWrite_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        case TransportParamId::TempoNorm:
            return tempoToNorm(bpmWrite_.load(std::memory_order_relaxed));
        case TransportParamId::QuantizeNorm:
            return quantToNorm(static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_relaxed)));
        case TransportParamId::TimeSigNumNorm:
            return tsNumToNorm(tsNumWrite_.load(std::memory_order_relaxed));
        case TransportParamId::TimeSigDenNorm:
            return tsDenToNorm(tsDenWrite_.load(std::memory_order_relaxed));
        case TransportParamId::Swing01:
            return normalizeSwing(swingWrite_.load(std::memory_order_relaxed));
        default:
            return 0.0f;
    }
}

void TransportBridgeDualBuffer::setParam(std::size_t index, float value) {
    switch (static_cast<TransportParamId>(index)) {
        case TransportParamId::Playing:
            setPlaying(value >= 0.5f);
            break;
        case TransportParamId::TempoNorm:
            setTempo(tempoFromNorm(value));
            break;
        case TransportParamId::QuantizeNorm:
            setQuantize(quantFromNorm(value));
            break;
        case TransportParamId::TimeSigNumNorm: {
            const uint8_t den = tsDenWrite_.load(std::memory_order_relaxed);
            setTimeSignature(tsNumFromNorm(value), den);
        } break;
        case TransportParamId::TimeSigDenNorm: {
            const uint8_t num = tsNumWrite_.load(std::memory_order_relaxed);
            setTimeSignature(num, tsDenFromNorm(value));
        } break;
        case TransportParamId::Swing01:
            setSwing(value);
            break;
        default:
            break;
    }
}

const ParamMeta& TransportBridgeDualBuffer::getParamMeta(std::size_t index) const {
    const auto& meta = transportMeta();
    if (index >= meta.size()) {
        return noMeta();
    }
    return meta[index];
}

} // namespace avantgarde
