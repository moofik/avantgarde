#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Экран списка FX активного трека.
// Ответственность:
// - показать список слотов эффектов выбранного трека;
// - выдать intent на добавление нового FX;
// - открыть редактор параметров выбранного FX.
class FxListWidget final : public IUiWidget {
public:
    explicit FxListWidget(uint16_t frameWidth = 60,
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
        std::string title{"FX LIST"};
        std::string keysHint{" keys [j/k slot] [;/' focus] [/? adj] [o apply] [esc] "};
    };

    // Защита от выхода индекса за пределы списка треков.
    static uint8_t clampTrack_(uint8_t track, std::size_t totalTracks) noexcept;
    // Защита от выхода индекса за пределы FX-слотов.
    static uint16_t clampFx_(uint16_t fx, std::size_t fxCount) noexcept;
    // Имя FX в списке по данным трека и реестра профилей.
    static std::string fxName_(const UiTrackStateView& track, uint16_t slot);
    // Строка статуса active action pointer внизу кадра.
    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;

    // Ширина текстовой рамки в символах.
    uint16_t frameWidth_{60};
    // Количество видимых строк списка FX.
    uint16_t listRows_{8};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};

    void buildLayoutModel_(const UiLayoutTemplate& tpl);
    static void collectNodes_(const UiLayoutNode& root, std::vector<const UiLayoutNode*>& out) noexcept;
};

} // namespace avantgarde
