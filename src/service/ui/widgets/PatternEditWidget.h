#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Отдельный экран редактирования параметров паттерна.
// Здесь сосредоточены только pattern-настройки:
// - длина (bars),
// - квантизация,
// - режим поведения на loop boundary (reset/continue).
class PatternEditWidget final : public IUiWidget {
public:
    struct Options {
        uint16_t frameWidth{60};
        std::optional<UiLayoutTemplate> layoutTemplate{};
    };

    PatternEditWidget() noexcept;
    explicit PatternEditWidget(const Options& options) noexcept;

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
        std::string title{"PATTERN EDIT"};
        std::string keysHint{" keys [F5/F6 focus] [F7/F8 adjust] [F1 apply] [esc back] "};
    };

    static const char* quantToStr_(SequencerQuantize q) noexcept;
    std::string buildActionStatusLine_(const UiState& rtState,
                                       const UiNavState& navState) const;
    void buildLayoutModel_(const UiLayoutTemplate& tpl);

    uint16_t frameWidth_{60};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};
};

} // namespace avantgarde

