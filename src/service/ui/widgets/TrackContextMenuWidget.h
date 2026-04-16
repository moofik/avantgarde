#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "service/ui/widgets/ContextMenuWidgetBase.h"

namespace avantgarde {

// Контекстное меню активного трека.
// В этом меню живут действия над треком/FX/навигацией,
// а sample-операции вынесены в отдельный SampleWidgetContextMenu.
class TrackContextMenuWidget final : public ContextMenuWidgetBase {
public:
    explicit TrackContextMenuWidget(uint16_t frameWidth = 60,
                                    std::optional<UiLayoutTemplate> layoutTemplate = std::nullopt) noexcept;

protected:
    std::vector<MenuItem> buildMenuItems_(const UiState& rtState,
                                          const UiNavState& navState) const override;
    WidgetOutput applyMenuItem_(UiAction::Id actionId,
                                const UiState& rtState,
                                UiNavState& navState) const override;
};

} // namespace avantgarde

