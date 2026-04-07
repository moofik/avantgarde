#include "runtime/PatternSchedulerRtExtension.h"

namespace avantgarde {

PatternSchedulerRtExtension::PatternSchedulerRtExtension(IPatternScheduler* scheduler,
                                                         ITransportBridge* transport) noexcept
    : scheduler_(scheduler)
    , transport_(transport) {}

void PatternSchedulerRtExtension::onBlockBegin(const AudioProcessContext&) noexcept {
    if (!scheduler_ || !transport_) {
        return;
    }

    const TransportRtSnapshot& snap = transport_->rt();
    scheduler_->onTransport(snap);

    PatternId ready = kInvalidPatternId;
    while (scheduler_->popReadySwitch(ready)) {
        if (ready != kInvalidPatternId) {
            publishReady_(ready);
        }
    }
}

bool PatternSchedulerRtExtension::consumeReadySwitch(PatternId& outPatternId) noexcept {
    const uint32_t seq = readySeq_.load(std::memory_order_acquire);
    if (seq == seenReadySeq_) {
        return false;
    }
    seenReadySeq_ = seq;
    const PatternId id = readyId_.load(std::memory_order_relaxed);
    if (id == kInvalidPatternId) {
        return false;
    }
    outPatternId = id;
    return true;
}

void PatternSchedulerRtExtension::publishReady_(PatternId id) noexcept {
    readyId_.store(id, std::memory_order_relaxed);
    (void)readySeq_.fetch_add(1u, std::memory_order_release);
}

} // namespace avantgarde

