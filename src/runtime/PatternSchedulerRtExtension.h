#pragma once

#include <atomic>
#include <cstdint>

#include "contracts/IPattern.h"
#include "contracts/IRtExtension.h"
#include "contracts/ITransport.h"

namespace avantgarde {

/**
 * @brief RT-extension, который кормит PatternScheduler транспортным временем
 *        и публикует готовые pattern-switch события в control-поток.
 *
 * Потоки:
 * - RT-thread: onBlockBegin()
 * - control-thread: consumeReadySwitch()
 *
 * Модель доставки:
 * - single-slot mailbox (last-write-wins).
 * - этого достаточно, т.к. переключение паттернов не является high-rate событием.
 */
class PatternSchedulerRtExtension final : public IRtExtension {
public:
    PatternSchedulerRtExtension(IPatternScheduler* scheduler,
                                ITransportBridge* transport) noexcept;

    void onBlockBegin(const AudioProcessContext& ctx) noexcept override;
    void onBlockEnd(const AudioProcessContext&) noexcept override {}

    // Забрать последний ready-pattern из RT mailbox.
    bool consumeReadySwitch(PatternId& outPatternId) noexcept;

private:
    void publishReady_(PatternId id) noexcept;

private:
    IPatternScheduler* scheduler_{nullptr};
    ITransportBridge* transport_{nullptr};

    std::atomic<PatternId> readyId_{kInvalidPatternId};
    std::atomic<uint32_t> readySeq_{0};
    uint32_t seenReadySeq_{0};
};

} // namespace avantgarde

