#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
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
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    struct FxTypeOption {
        std::string_view id{};
        std::string_view label{};
    };

    struct LayoutModel {
        bool enabled{false};
        std::string title{"FX LIST"};
        std::string keysHint{" keys [F5/F6 slot] [F1 apply] [F7 bypass] [F8 remove] [esc] "};
    };

    // Защита от выхода индекса за пределы списка треков.
    static uint8_t clampTrack_(uint8_t track, std::size_t totalTracks) noexcept;
    // Защита от выхода индекса за пределы FX-слотов.
    static uint16_t clampFx_(uint16_t fx, std::size_t fxCount) noexcept;
    // Курсор FX-слотов с поддержкой "виртуального пустого слота" (индекс == fxCount).
    static uint16_t clampFxCursor_(uint16_t fxCursor, std::size_t fxCount) noexcept;
    // Имя FX в списке по данным трека и реестра профилей.
    static std::string fxName_(const UiTrackStateView& track, uint16_t slot);
    // Состояние bypass слота FX (true = enabled, false = bypass).
    static bool fxEnabled_(const UiTrackStateView& track, uint16_t slot) noexcept;
    // Каталог доступных типов FX для действия "Add FX".
    static const std::array<FxTypeOption, 4>& fxTypeOptions_() noexcept;
    // Безопасное ограничение индекса типа FX.
    static uint16_t clampFxType_(uint16_t typeIndex) noexcept;
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
