#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

// Экран редактирования сэмпла выбранного трека:
// - playback profile (PATTERN/PATTERN ONCE/LOOP/ONESHOT),
// - speed/gain,
// - trim start/end,
// - пиксельная визуализация waveform-региона.
class SampleEditWidget final : public IUiWidget {
public:
    struct Options {
        uint16_t frameWidth{60};
        float speedStep{0.05f};
        float gainStep{0.05f};
        float trimStep{0.01f};
        std::optional<UiLayoutTemplate> layoutTemplate{};
    };

    SampleEditWidget() noexcept;
    explicit SampleEditWidget(const Options& options) noexcept;

    const char* id() const noexcept override;
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    struct LayoutModel {
        bool enabled{false};
        std::string title{"SAMPLE EDIT"};
        std::string keysHint{" keys [F5/F6 select] [F7/F8 adjust] [esc back] "};
        std::string targetTrackPlaybackProfile{"param.track.selected.playback_profile"};
        std::string targetTrackSpeed{"param.track.selected.speed"};
        std::string targetTrackGain{"param.track.selected.gain"};
        std::string targetTrackTrimStart{"param.track.selected.start"};
        std::string targetTrackTrimEnd{"param.track.selected.end"};
        std::string targetTrackTempoSync{"param.track.selected.tempo_sync"};
    };

    static uint8_t clampTrack_(uint8_t track, std::size_t totalTracks) noexcept;
    static uint8_t profileToIndex_(UiTrackPlaybackProfile profile) noexcept;
    static UiTrackPlaybackProfile indexToProfile_(uint8_t index) noexcept;
    static const char* profileName_(UiTrackPlaybackProfile profile) noexcept;
    static float speedTo01_(float speed) noexcept;
    static float trimTo01_(float value) noexcept;
    static std::vector<float> buildWavePeaks_(const std::vector<float>& mono,
                                              std::size_t pointCount);
    static bool decodeWavMono_(const std::string& path,
                               std::vector<float>& monoOut);
    std::vector<float> getWavePeaksForPath_(const std::string& path) const;

    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;
    UiPreparedParams buildPreparedLayoutParams_(const UiState& rtState, const UiNavState& navState) const;
    void buildLayoutModel_(const UiLayoutTemplate& tpl);

    uint16_t frameWidth_{60};
    float speedStep_{0.05f};
    float gainStep_{0.05f};
    float trimStep_{0.01f};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};
    // Кэш waveform-пиков по пути клипа, чтобы не декодировать WAV каждый кадр UI.
    mutable std::unordered_map<std::string, std::vector<float>> waveformCache_{};
};

} // namespace avantgarde
