#pragma once

#include <cstdint>
#include <span>

#include "contracts/IPattern.h"
#include "contracts/ISnapshotable.h"

namespace avantgarde {

/**
 * @brief Сборщик PatternState из абстрактных snapshot-источников.
 *
 * Роль в архитектуре:
 * - принимает единый список `ISnapshotable`-источников (transport/track/и т.д.);
 * - строит канонический `PatternState` без знания о `SamplerEngineLayer::Impl`;
 * - инкапсулирует маппинг `TrackSnapshot -> PatternTrackSnapshot`.
 */
class PatternSnapshotBuilder final {
public:
    /**
     * @brief Дефолты layout-полей паттерна.
     * Эти значения задают "каркас" паттерна и не зависят от live DSP.
     */
    struct LayoutDefaults {
        uint32_t lengthBars{64};
        uint32_t lengthInSteps{64};
        uint16_t stepsPerBeat{4};
    };

    /**
     * @brief Построить дефолтный паттерн без live трековых снимков.
     * @param id Идентификатор паттерна.
     * @param trackCount Число треков, для которых нужно создать snapshot-слоты.
     * @param transport Базовый transport snapshot для паттерна.
     * @param layout Дефолты temporal layout (bars/steps).
     */
    PatternState makeDefaultPattern(PatternId id,
                                    uint8_t trackCount,
                                    const PatternTransportSnapshot& transport) const;
    PatternState makeDefaultPattern(PatternId id,
                                    uint8_t trackCount,
                                    const PatternTransportSnapshot& transport,
                                    const LayoutDefaults& layout) const;

    /**
     * @brief Построить паттерн из live snapshot-источников треков.
     * @param id Идентификатор паттерна.
     * @param sources Единый список snapshot-источников.
     * @param out Выходной собранный PatternState.
     * @param layout Дефолты temporal layout (bars/steps).
     * @return true если сборка завершена.
     */
    bool buildFromSnapshotables(PatternId id,
                                std::span<ISnapshotable* const> sources,
                                PatternState& out) const noexcept;
    bool buildFromSnapshotables(PatternId id,
                                std::span<ISnapshotable* const> sources,
                                PatternState& out,
                                const LayoutDefaults& layout) const noexcept;

private:
    static PatternTrackSnapshot makeDefaultTrackSnapshot_(uint8_t trackId);
    static bool readSnapshotRecord_(const ISnapshotable& source, SnapshotRecord& out) noexcept;
    static void applySnapshotRecord_(const SnapshotRecord& record,
                                     PatternState& state,
                                     uint8_t trackId) noexcept;
    static void copyTrackSnapshotToPattern_(const TrackSnapshot& src, PatternTrackSnapshot& dst);
    static void applyLayoutDefaults_(PatternState& state, const LayoutDefaults& layout);
};

} // namespace avantgarde
