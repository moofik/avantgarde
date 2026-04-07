#include "service/pattern/PatternSwitchPlanApplier.h"

#include <algorithm>
#include <cmath>

#include "contracts/ids.h"

namespace avantgarde {
namespace {

uint8_t clampTimeSigNum(float raw) noexcept {
    const int num = std::clamp(static_cast<int>(std::lround(raw)), 1, 32);
    return static_cast<uint8_t>(num);
}

uint8_t sanitizeTimeSigDen(uint16_t raw) noexcept {
    // Поддерживаем только музыкально ожидаемые степени двойки.
    switch (raw) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
            return static_cast<uint8_t>(raw);
        default:
            return 4;
    }
}

QuantizeMode decodeQuant(float raw) noexcept {
    const int q = std::clamp(static_cast<int>(std::lround(raw)), 0, 2);
    switch (q) {
        case 0: return QuantizeMode::None;
        case 1: return QuantizeMode::Beat;
        default:
            return QuantizeMode::Bar;
    }
}

bool applyOne(const PatternApplyOp& op, IPatternApplyTarget& target) noexcept {
    switch (op.kind) {
        case PatternApplyOpKind::TransportSetTempo:
            return target.setTransportTempo(op.value);
        case PatternApplyOpKind::TransportSetTimeSig:
            return target.setTransportTimeSignature(clampTimeSigNum(op.value), sanitizeTimeSigDen(op.index));
        case PatternApplyOpKind::TransportSetQuant:
            return target.setTransportQuantize(decodeQuant(op.value));
        case PatternApplyOpKind::TransportSetSwing:
            return target.setTransportSwing(std::clamp(op.value, 0.0f, 1.0f));
        case PatternApplyOpKind::TrackSetMuted:
            return target.setTrackMuted(op.trackId, op.value >= 0.5f);
        case PatternApplyOpKind::TrackSetArmed:
            return target.setTrackArmed(op.trackId, op.value >= 0.5f);
        case PatternApplyOpKind::TrackSetGain:
            return target.setTrackParam(op.trackId, toParamIndex(TrackParamId::Gain01), op.value);
        case PatternApplyOpKind::TrackSetPlaybackInc:
            return target.setTrackParam(op.trackId, toParamIndex(TrackParamId::PlaybackInc), op.value);
        case PatternApplyOpKind::TrackSetBars:
            return target.setTrackBars(op.trackId, std::max<uint32_t>(1u, op.valueU32));
        case PatternApplyOpKind::TrackSetClipRef:
            if (op.valueU32 == 0u) {
                return target.clearTrackSample(op.trackId);
            }
            return target.setTrackClipRef(op.trackId, op.valueU32);
        case PatternApplyOpKind::TrackParamSet:
            return target.setTrackParam(op.trackId, op.index, op.value);
        case PatternApplyOpKind::FxParamSet:
            if (op.slot < 0 || op.slot > 255) {
                return false;
            }
            return target.setFxParam(op.trackId, static_cast<uint8_t>(op.slot), op.index, op.value);
    }
    return false;
}

} // namespace

PatternSwitchApplyReport PatternSwitchPlanApplier::apply(const CompiledSwitchPlan& plan,
                                                         IPatternApplyTarget& target) noexcept {
    PatternSwitchApplyReport report{};
    report.totalOps = plan.ops.size();
    for (const PatternApplyOp& op : plan.ops) {
        if (applyOne(op, target)) {
            ++report.appliedOps;
        } else {
            ++report.failedOps;
        }
    }
    return report;
}

} // namespace avantgarde

