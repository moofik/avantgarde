#pragma once

#include <cstddef>
#include <cstdint>

#include "contracts/ITransport.h"
#include "service/pattern/PatternSnapshotManager.h"

namespace avantgarde {

/**
 * @brief Контракт цели применения pattern switch-плана.
 *
 * Назначение интерфейса:
 * - отделить слой построения плана (PatternSnapshotManager) от конкретного
 *   движка/приложения;
 * - позволить применять один и тот же план в разных backend-ах.
 */
struct IPatternApplyTarget {
    virtual ~IPatternApplyTarget() = default;

    virtual bool setTransportTempo(float bpm) noexcept = 0;
    virtual bool setTransportTimeSignature(uint8_t num, uint8_t den) noexcept = 0;
    virtual bool setTransportQuantize(QuantizeMode q) noexcept = 0;
    virtual bool setTransportSwing(float swing01) noexcept = 0;

    virtual bool setTrackMuted(uint8_t track, bool muted) noexcept = 0;
    virtual bool setTrackArmed(uint8_t track, bool armed) noexcept = 0;
    virtual bool setTrackBars(uint8_t track, uint32_t bars) noexcept = 0;
    virtual bool setTrackClipRef(uint8_t track, uint32_t clipRefId) noexcept = 0;
    virtual bool clearTrackSample(uint8_t track) noexcept = 0;
    virtual bool setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept = 0;
    virtual bool setFxParam(uint8_t track, uint8_t fxSlot, uint16_t paramIndex, float value) noexcept = 0;
};

/**
 * @brief Краткий отчет о применении switch-плана.
 */
struct PatternSwitchApplyReport {
    std::size_t totalOps{0};
    std::size_t appliedOps{0};
    std::size_t failedOps{0};

    bool ok() const noexcept {
        return failedOps == 0;
    }
};

/**
 * @brief Применяет CompiledSwitchPlan к абстрактной цели IPatternApplyTarget.
 *
 * Важно:
 * - Класс не содержит состояние и не делает IO.
 * - Для TrackSetClipRef поддержан reset-кейс:
 *   valueU32 == 0 -> clearTrackSample(track).
 */
class PatternSwitchPlanApplier final {
public:
    static PatternSwitchApplyReport apply(const CompiledSwitchPlan& plan,
                                          IPatternApplyTarget& target) noexcept;
};

} // namespace avantgarde

