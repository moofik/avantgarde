#pragma once

#include "app/SamplerEngineLayer.h"
#include "service/pattern/PatternSwitchPlanApplier.h"

namespace avantgarde {

/**
 * @brief Адаптер SamplerEngineLayer к контракту IPatternApplyTarget.
 *
 * Нужен, чтобы PatternSwitchPlanApplier не зависел от конкретного engine-класса,
 * а SamplerApplication мог применять compiled switch-планы напрямую в движок.
 */
class SamplerEnginePatternApplyTarget final : public IPatternApplyTarget {
public:
    explicit SamplerEnginePatternApplyTarget(SamplerEngineLayer& engine) noexcept
        : engine_(engine) {}

    bool setTransportTempo(float bpm) noexcept override;
    bool setTransportTimeSignature(uint8_t num, uint8_t den) noexcept override;
    bool setTransportQuantize(QuantizeMode q) noexcept override;
    bool setTransportSwing(float swing01) noexcept override;

    bool setTrackMuted(uint8_t track, bool muted) noexcept override;
    bool setTrackArmed(uint8_t track, bool armed) noexcept override;
    bool setTrackBars(uint8_t track, uint32_t bars) noexcept override;
    bool setTrackClipRef(uint8_t track, uint32_t clipRefId) noexcept override;
    bool clearTrackSample(uint8_t track) noexcept override;
    bool setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept override;
    bool setFxParam(uint8_t track, uint8_t fxSlot, uint16_t paramIndex, float value) noexcept override;

private:
    // Ссылка на live engine-layer, куда пушим операции switch-плана.
    SamplerEngineLayer& engine_;
};

} // namespace avantgarde

