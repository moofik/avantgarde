#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Контекстное меню активного трека.
// Принцип: "трек = главный объект", операции выполняются через это меню.
class TrackContextMenuWidget final : public IUiWidget {
public:
    explicit TrackContextMenuWidget(uint16_t frameWidth = 60,
                                    std::optional<UiLayoutTemplate> layoutTemplate = std::nullopt) noexcept;

    const char* id() const noexcept override;
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    struct LayoutModel {
        bool enabled{false};
        std::string title{"TRACK MENU"};
        std::string keysHint{" keys [F5/F6 select] [F1 apply] [esc back] "};
    };

    enum class Item : uint8_t {
        LoadSample = 0,
        Clear = 1,
        LoadFx = 2
    };

    static constexpr uint16_t kItemCount = 3U;

    uint16_t frameWidth_{60};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};

    static uint16_t clampIndex_(uint16_t index) noexcept;
    static Item itemFromIndex_(uint16_t index) noexcept;
    static uint16_t wrapPrev_(uint16_t index) noexcept;
    static uint16_t wrapNext_(uint16_t index) noexcept;
    static const char* itemLabel_(Item item) noexcept;
    static const char* itemStatus_(Item item) noexcept;
    static std::string padRight_(const std::string& s, std::size_t width);

    WidgetOutput applyItem_(Item item, const UiState& rtState, UiNavState& navState) const;
    void buildLayoutModel_(const UiLayoutTemplate& tpl);
};

} // namespace avantgarde
