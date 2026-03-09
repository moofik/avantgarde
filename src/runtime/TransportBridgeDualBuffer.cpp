#include "runtime/TransportBridgeDualBuffer.h"

#include <algorithm>
#include <cmath>

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

} // namespace

TransportBridgeDualBuffer::TransportBridgeDualBuffer() noexcept {
    rt_.playing = playingWrite_.load(std::memory_order_relaxed);
    rt_.tsNum = tsNumWrite_.load(std::memory_order_relaxed);
    rt_.tsDen = tsDenWrite_.load(std::memory_order_relaxed);
    rt_.ppq = ppqWrite_.load(std::memory_order_relaxed);
    rt_.bpm = bpmWrite_.load(std::memory_order_relaxed);
    rt_.quant = static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_relaxed));
    rt_.swing = swingWrite_.load(std::memory_order_relaxed);
    rt_.sampleTime = 0;
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
    rt_.playing = playingWrite_.load(std::memory_order_acquire);
    rt_.tsNum = tsNumWrite_.load(std::memory_order_acquire);
    rt_.tsDen = tsDenWrite_.load(std::memory_order_acquire);
    rt_.ppq = ppqWrite_.load(std::memory_order_acquire);
    rt_.bpm = bpmWrite_.load(std::memory_order_acquire);
    rt_.quant = static_cast<QuantizeMode>(quantWrite_.load(std::memory_order_acquire));
    rt_.swing = swingWrite_.load(std::memory_order_acquire);
}

const TransportRtSnapshot& TransportBridgeDualBuffer::rt() const noexcept {
    return rt_;
}

void TransportBridgeDualBuffer::advanceSampleTime(uint64_t frames) noexcept {
    rt_.sampleTime += frames;
}

} // namespace avantgarde
