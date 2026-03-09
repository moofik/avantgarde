#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "contracts/IRtExtension.h"
#include "contracts/IRtCommandQueue.h"
#include "contracts/ITransport.h"
#include "contracts/types.h"

namespace avantgarde {

class QuantizedSchedulerRtExtension final : public IRtExtension {
public:
    QuantizedSchedulerRtExtension(IRtCommandQueue* inQueue,
                                  IRtCommandQueue* outQueue,
                                  ITransportBridge* transport,
                                  double sampleRate,
                                  std::size_t pendingCapacity = 256) noexcept;

    bool overflowFlagAndReset() noexcept;

    void onBlockBegin(const AudioProcessContext& ctx) noexcept override;
    void onBlockEnd(const AudioProcessContext&) noexcept override {}

private:
    struct PendingCommand {
        RtCommand cmd{};
        uint64_t dueSample{0};
    };

    static uint64_t computeQuantumSamples(const TransportRtSnapshot& snap,
                                          QuantizeMode quant,
                                          double sampleRate) noexcept;
    uint64_t computeDueSample(uint64_t now,
                              const TransportRtSnapshot& snap,
                              QuantizeMode quant) const noexcept;
    static QuantizeMode decodeQuantizeMode(const RtCommand& cmd) noexcept;
    static bool isQuantizable(const RtCommand& cmd) noexcept;

    void drainIncoming(const TransportRtSnapshot& snap, uint64_t now) noexcept;
    void dispatchDue(uint64_t blockStart, uint64_t blockEnd) noexcept;

    IRtCommandQueue* inQueue_{nullptr};
    IRtCommandQueue* outQueue_{nullptr};
    ITransportBridge* transport_{nullptr};
    double sampleRate_{48000.0};

    std::size_t pendingCapacity_{0};
    std::unique_ptr<PendingCommand[]> pending_;
    std::size_t pendingCount_{0};

    QuantizeMode quantMode_{QuantizeMode::None};
    std::atomic<bool> overflow_{false};
};

} // namespace avantgarde
