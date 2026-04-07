#include "app/SamplerEnginePatternApplyTarget.h"

namespace avantgarde {

bool SamplerEnginePatternApplyTarget::setTransportTempo(float bpm) noexcept {
    engine_.setTempo(bpm);
    return true;
}

bool SamplerEnginePatternApplyTarget::setTransportTimeSignature(uint8_t num, uint8_t den) noexcept {
    engine_.setTimeSignature(num, den);
    return true;
}

bool SamplerEnginePatternApplyTarget::setTransportQuantize(QuantizeMode q) noexcept {
    engine_.setQuantize(q);
    return true;
}

bool SamplerEnginePatternApplyTarget::setTransportSwing(float swing01) noexcept {
    engine_.setSwing(swing01);
    return true;
}

bool SamplerEnginePatternApplyTarget::setTrackMuted(uint8_t track, bool muted) noexcept {
    return engine_.setTrackMuted(track, muted);
}

bool SamplerEnginePatternApplyTarget::setTrackArmed(uint8_t track, bool armed) noexcept {
    return engine_.setTrackArmed(track, armed);
}

bool SamplerEnginePatternApplyTarget::setTrackBars(uint8_t track, uint32_t bars) noexcept {
    return engine_.setTrackBars(track, bars);
}

bool SamplerEnginePatternApplyTarget::setTrackClipRef(uint8_t track, uint32_t clipRefId) noexcept {
    return engine_.setTrackClipRef(track, clipRefId);
}

bool SamplerEnginePatternApplyTarget::clearTrackSample(uint8_t track) noexcept {
    return engine_.clearTrackSample(track);
}

bool SamplerEnginePatternApplyTarget::setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept {
    return engine_.setTrackParam(track, paramIndex, value);
}

bool SamplerEnginePatternApplyTarget::setFxParam(uint8_t track,
                                                 uint8_t fxSlot,
                                                 uint16_t paramIndex,
                                                 float value) noexcept {
    return engine_.setFxParam(track, fxSlot, paramIndex, value);
}

} // namespace avantgarde

