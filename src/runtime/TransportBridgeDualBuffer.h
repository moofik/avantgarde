#pragma once

#include <atomic>
#include <cstdint>

#include "contracts/ITransport.h"

namespace avantgarde {

class TransportBridgeDualBuffer final : public ITransportBridge {
public:
    TransportBridgeDualBuffer() noexcept;

    void setPlaying(bool on) override;
    void setTempo(float bpm) override;
    void setTimeSignature(uint8_t num, uint8_t den) override;
    void setQuantize(QuantizeMode q) override;
    void setSwing(float s01) override;

    void swapBuffers() noexcept override;
    const TransportRtSnapshot& rt() const noexcept override;
    void advanceSampleTime(uint64_t frames) noexcept override;
    bool getSnapshot(SnapshotRecord& out) const noexcept override;

    // IParameterized (global transport surface).
    std::size_t getParamCount() const override;
    float getParam(std::size_t index) const override;
    void setParam(std::size_t index, float value) override;
    const ParamMeta& getParamMeta(std::size_t index) const override;

private:
    std::atomic<bool> playingWrite_{false};
    std::atomic<uint8_t> tsNumWrite_{4};
    std::atomic<uint8_t> tsDenWrite_{4};
    std::atomic<uint16_t> ppqWrite_{96};
    std::atomic<float> bpmWrite_{120.0f};
    std::atomic<uint8_t> quantWrite_{static_cast<uint8_t>(QuantizeMode::Bar)};
    std::atomic<float> swingWrite_{0.0f};

    // RT-снапшот, который читается движком/RT-extension в пределах блока.
    TransportRtSnapshot transportRtSnapshot_{};
};

} // namespace avantgarde
