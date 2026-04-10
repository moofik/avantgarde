#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "contracts/UiLayout.h"
#include "contracts/UiScene.h"

namespace avantgarde {

/**
 * @brief Результат резолва write-target для UI-ноды.
 *
 * `target` описывает путь записи (что именно менять в домене),
 * в то время как `bind` описывает путь чтения (что показывать в UI).
 */
struct UiTargetResolution {
    enum class Kind : uint8_t {
        Unknown = 0,
        TrackSpeed,
        TrackGain,
        TrackPlaybackProfile,
        TrackTrimStart,
        TrackTrimEnd,
        TrackLooperMode,
        TrackMute,
        TrackArm,
        TransportBpm,
        TransportQuant,
        TransportPlaying,
        TransportMetronome,
        FxParam
    };

    bool ok{false};
    std::string canonical{};
    std::string error{};
    Kind kind{Kind::Unknown};
    // Для FxParam: индекс параметра в descriptor.params.
    // -1 означает "текущий выбранный параметр".
    int32_t paramIndex{-1};
};

/**
 * @brief Резолвер write-path для layout нод.
 *
 * Источник:
 * - если `rawTarget` не пустой: используем его;
 * - иначе fallback на `fallbackBindCanonical`.
 *
 * Поддерживает namespace-first формат:
 * - param.track.selected.speed
 * - param.track.selected.gain
 * - param.track.selected.playback_profile
 * - param.track.selected.start
 * - param.track.selected.end
 * - param.track.selected.looper_mode
 * - param.track.selected.mute
 * - param.track.selected.arm
 * - param.transport.bpm
 * - param.transport.quant
 * - param.transport.playing
 * - param.transport.metronome_enabled
 * - param.fx.selected.<index|selected>
 * - fx.selected.param.<index|selected> (как fallback bind-путь в FxEditor)
 */
class UiTargetResolver final {
public:
    static UiTargetResolution resolve(UiScene scene,
                                      UiLayoutNodeType nodeType,
                                      std::string_view rawTarget,
                                      std::string_view fallbackBindCanonical);
};

} // namespace avantgarde
