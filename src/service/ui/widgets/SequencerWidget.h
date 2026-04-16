#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

// Экран секвенсора (MVP):
// - список lane-ов,
// - lane-focus режим с выбором/редактированием объектов,
// - action-pointer совместимая модель под будущие hardware knobs.
class SequencerWidget final : public IUiWidget {
public:
    enum class Mode : uint8_t {
        List = 0,
        Lane = 1
    };

    struct Options {
        uint16_t frameWidth{60};
        Mode mode{Mode::List};
        std::optional<UiLayoutTemplate> layoutTemplate{};
    };

    SequencerWidget() noexcept;
    explicit SequencerWidget(const Options& options) noexcept;

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
        std::string title{"SEQUENCER"};
        std::string keysHint{
            " keys [F5/F6 focus] [F7/F8 adjust] [F1 apply] [v rec] [p/s play/stop] [n back] "};
    };

    static uint16_t clampLane_(uint16_t lane, const UiSequencerState& seq) noexcept;
    static uint16_t clampPoint_(uint16_t point, const UiSequencerState& seq) noexcept;
    static const char* quantToStr_(SequencerQuantize q) noexcept;
    static const char* laneKindToStr_(UiSequencerLaneKind kind) noexcept;
    static std::vector<float> buildLaneWave_(const UiSequencerState& seq);
    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;
    UiPreparedParams buildPreparedLayoutParams_(const UiState& rtState, const UiNavState& navState) const;
    void buildLayoutModel_(const UiLayoutTemplate& tpl);

    uint16_t frameWidth_{60};
    Mode mode_{Mode::List};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};
};

} // namespace avantgarde
