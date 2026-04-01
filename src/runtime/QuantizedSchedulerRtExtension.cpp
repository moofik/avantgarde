#include "runtime/QuantizedSchedulerRtExtension.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

#include "contracts/ids.h"

namespace avantgarde {
namespace {

uint64_t safeAddU64(uint64_t a, uint64_t b) noexcept {
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    if (a > max - b) return max;
    return a + b;
}

} // namespace

QuantizedSchedulerRtExtension::QuantizedSchedulerRtExtension(IRtCommandQueue* inQueue,
                                                             IRtCommandQueue* outQueue,
                                                             ITransportBridge* transport,
                                                             double sampleRate,
                                                             std::size_t pendingCapacity) noexcept
        : inQueue_(inQueue)
        , outQueue_(outQueue)
        , transport_(transport)
        , sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0)
        , pendingCapacity_(pendingCapacity)
        , pending_(pendingCapacity_ ? new (std::nothrow) PendingCommand[pendingCapacity_] : nullptr) {
    if (!pending_) {
        pendingCapacity_ = 0;
    }
}

bool QuantizedSchedulerRtExtension::overflowFlagAndReset() noexcept {
    return overflow_.exchange(false, std::memory_order_relaxed);
}

void QuantizedSchedulerRtExtension::onBlockBegin(const AudioProcessContext& ctx) noexcept {
    if (!inQueue_ || !outQueue_ || !transport_) {
        return;
    }

    const TransportRtSnapshot& snap = transport_->rt();
    const uint64_t blockStart = snap.sampleTime;
    const uint64_t blockEnd = safeAddU64(blockStart, static_cast<uint64_t>(ctx.nframes));

    drainIncoming(snap, blockStart);
    dispatchDue(blockStart, blockEnd);
}

uint64_t QuantizedSchedulerRtExtension::computeQuantumSamples(const TransportRtSnapshot& snap,
                                                              QuantizeMode quant,
                                                              double sampleRate) noexcept {
    if (quant == QuantizeMode::None) {
        return 1;
    }

    const double bpm = (std::isfinite(snap.bpm) && snap.bpm > 0.0f) ? static_cast<double>(snap.bpm) : 120.0;
    const uint8_t den = (snap.tsDen == 0) ? static_cast<uint8_t>(4) : snap.tsDen;
    const uint8_t num = (snap.tsNum == 0) ? static_cast<uint8_t>(4) : snap.tsNum;

    const double beatSamplesD = std::max(1.0, std::round(sampleRate * 60.0 / bpm));
    const uint64_t beatSamples = static_cast<uint64_t>(beatSamplesD);

    if (quant == QuantizeMode::Beat) {
        return std::max<uint64_t>(1, beatSamples);
    }

    const uint64_t scaledNum = static_cast<uint64_t>(num) * 4ULL;
    const uint64_t barSamples = std::max<uint64_t>(1, (beatSamples * scaledNum) / den);
    return barSamples;
}

uint64_t QuantizedSchedulerRtExtension::computeDueSample(uint64_t now,
                                                         const TransportRtSnapshot& snap,
                                                         QuantizeMode quant) const noexcept {
    if (quant == QuantizeMode::None) {
        return now;
    }

    const uint64_t quantum = computeQuantumSamples(snap, quant, sampleRate_);
    if (quantum <= 1) {
        return now;
    }

    const uint64_t rem = now % quantum;
    if (rem == 0) {
        return now;
    }
    return safeAddU64(now, quantum - rem);
}

QuantizeMode QuantizedSchedulerRtExtension::decodeQuantizeMode(const RtCommand& cmd) noexcept {
    const int v = static_cast<int>(std::lround(cmd.value));
    switch (v) {
        case static_cast<int>(QuantizeCmdValue::Beat): return QuantizeMode::Beat;
        case static_cast<int>(QuantizeCmdValue::Bar): return QuantizeMode::Bar;
        case static_cast<int>(QuantizeCmdValue::None):
        default:
            return QuantizeMode::None;
    }
}

bool QuantizedSchedulerRtExtension::isQuantizable(const RtCommand& cmd) noexcept {
    const CmdId id = fromWireCmdId(cmd.id);
    return id == CmdId::Play || id == CmdId::StopQuantized;
}

void QuantizedSchedulerRtExtension::drainIncoming(const TransportRtSnapshot& snap, uint64_t now) noexcept {
    RtCommand cmd{};
    while (inQueue_->pop(cmd)) {
        const CmdId id = fromWireCmdId(cmd.id);

        if (id == CmdId::QuantizeMode &&
            cmd.track == kRtTrackGlobal &&
            cmd.slot == kRtSlotTrackParams &&
            cmd.index == kRtQuantizeModeIndex) {
            quantMode_ = decodeQuantizeMode(cmd);
            continue;
        }

        // Квантизацию применяем только когда транспорт уже в PLAY.
        // В STOP команда Play должна проходить мгновенно, иначе пользователь
        // получает "скрытую" задержку в несколько секунд.
        if (isQuantizable(cmd) && quantMode_ != QuantizeMode::None && snap.playing) {
            if (!pending_ || pendingCount_ >= pendingCapacity_) {
                overflow_.store(true, std::memory_order_relaxed);
                continue;
            }

            PendingCommand& p = pending_[pendingCount_++];
            p.cmd = cmd;
            p.dueSample = computeDueSample(now, snap, quantMode_);
            continue;
        }

        if (!outQueue_->push(cmd)) {
            overflow_.store(true, std::memory_order_relaxed);
        }
    }
}

void QuantizedSchedulerRtExtension::dispatchDue(uint64_t blockStart, uint64_t blockEnd) noexcept {
    std::size_t i = 0;
    while (i < pendingCount_) {
        PendingCommand& p = pending_[i];
        const bool dueNow = (p.dueSample >= blockStart && p.dueSample < blockEnd) || (p.dueSample < blockStart);
        if (!dueNow) {
            ++i;
            continue;
        }

        if (!outQueue_->push(p.cmd)) {
            overflow_.store(true, std::memory_order_relaxed);
            ++i;
            continue;
        }

        pending_[i] = pending_[pendingCount_ - 1];
        --pendingCount_;
    }
}

} // namespace avantgarde
