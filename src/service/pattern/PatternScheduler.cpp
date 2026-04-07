#include "service/pattern/PatternScheduler.h"

#include <algorithm>
#include <cmath>

namespace avantgarde {
namespace {

constexpr uint64_t kPackTargetMask = 0xFFFFULL;
constexpr uint64_t kPackQuantMask = 0xFFULL;
constexpr uint64_t kPackTargetShift = 0;
constexpr uint64_t kPackQuantShift = 16;

} // namespace

PatternScheduler::PatternScheduler(double sampleRate) noexcept
    : sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0) {}

void PatternScheduler::requestSwitch(const PatternSwitchRequest& req) noexcept {
    // 1) Пишем payload запроса.
    requestData_.store(packRequest_(req), std::memory_order_release);
    // 2) Инкремент версии позволяет RT однозначно заметить "новый запрос".
    (void)requestSeq_.fetch_add(1, std::memory_order_acq_rel);
}

bool PatternScheduler::popReadySwitch(PatternId& outPatternId) noexcept {
    if (ready_ == kInvalidPatternId) {
        return false;
    }
    outPatternId = ready_;
    ready_ = kInvalidPatternId;
    return true;
}

void PatternScheduler::onTransport(const TransportRtSnapshot& transport) noexcept {
    // Подхватываем новые control-заявки только по изменению sequence.
    const uint32_t seq = requestSeq_.load(std::memory_order_acquire);
    // seq — текущая версия запроса (requestSeq_), которую пишет control-thread при requestSwitch().
    // seenSeq_ — версия, которую RT уже обработал.
    // Если не равны, значит пришел новый запрос на switch, RT его должен подхватить.
    if (seq != seenSeq_) {
        seenSeq_ = seq;
        const PatternSwitchRequest req = unpackRequest_(requestData_.load(std::memory_order_acquire));

        if (req.target == kInvalidPatternId) {
            // Явный cancel/reset заявки.
            armed_ = false;
            dueTarget_ = kInvalidPatternId;
            ready_ = kInvalidPatternId;
        } else if (req.quantize == QuantizeMode::None || !transport.playing) {
            // Если транспорт стоит, откладывать switch не нужно.
            armed_ = false;
            dueTarget_ = kInvalidPatternId;
            ready_ = req.target;
        } else {
            // Нормальный quantized-flow: считаем границу и "вооружаем" switch.
            dueSample_ = computeDueSample(transport.sampleTime, transport, req.quantize, sampleRate_);
            dueTarget_ = req.target;
            armed_ = true;
            if (dueSample_ <= transport.sampleTime) {
                // Защита от граничного случая "уже на границе".
                ready_ = dueTarget_;
                armed_ = false;
                dueTarget_ = kInvalidPatternId;
            }
        }
    }

    if (!armed_) {
        return;
    }
    // Как только RT-время дошло до dueSample, публикуем ready switch.
    if (transport.sampleTime >= dueSample_) {
        ready_ = dueTarget_;
        armed_ = false;
        dueTarget_ = kInvalidPatternId;
    }
}

uint64_t PatternScheduler::computeQuantumSamples(const TransportRtSnapshot& snap,
                                                 QuantizeMode quant,
                                                 double sampleRate) noexcept {
    if (quant == QuantizeMode::None) {
        return 1;
    }

    const double bpm = (std::isfinite(snap.bpm) && snap.bpm > 0.0f) ? static_cast<double>(snap.bpm) : 120.0;
    const uint8_t den = (snap.tsDen == 0) ? static_cast<uint8_t>(4) : snap.tsDen;
    const uint8_t num = (snap.tsNum == 0) ? static_cast<uint8_t>(4) : snap.tsNum;

    const uint64_t beatSamples = static_cast<uint64_t>(std::max(1.0, std::round(sampleRate * 60.0 / bpm)));
    if (quant == QuantizeMode::Beat) {
        return beatSamples;
    }

    const uint64_t scaledNum = static_cast<uint64_t>(num) * 4ULL;
    return std::max<uint64_t>(1, (beatSamples * scaledNum) / den);
}

uint64_t PatternScheduler::computeDueSample(uint64_t now,
                                            const TransportRtSnapshot& snap,
                                            QuantizeMode quant,
                                            double sampleRate) noexcept {
    if (quant == QuantizeMode::None) {
        return now;
    }

    const uint64_t quantum = computeQuantumSamples(snap, quant, sampleRate);
    if (quantum <= 1) {
        return now;
    }

    const uint64_t rem = now % quantum;
    if (rem == 0) {
        return now;
    }
    return now + (quantum - rem);
}

uint64_t PatternScheduler::packRequest_(const PatternSwitchRequest& req) noexcept {
    const uint64_t target = static_cast<uint64_t>(req.target) & kPackTargetMask;
    const uint64_t quant = static_cast<uint64_t>(static_cast<uint8_t>(req.quantize)) & kPackQuantMask;
    return (target << kPackTargetShift) | (quant << kPackQuantShift);
}

PatternSwitchRequest PatternScheduler::unpackRequest_(uint64_t packed) noexcept {
    PatternSwitchRequest out{};
    out.target = static_cast<PatternId>((packed >> kPackTargetShift) & kPackTargetMask);
    out.quantize = static_cast<QuantizeMode>((packed >> kPackQuantShift) & kPackQuantMask);
    return out;
}

} // namespace avantgarde
