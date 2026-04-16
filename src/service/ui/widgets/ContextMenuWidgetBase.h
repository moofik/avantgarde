#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Общая база для всех UI-виджетов класса "контекстное меню".
// База инкапсулирует:
// - навигацию по списку (up/down/enter),
// - сборку prepared-layout для status/list/footer,
// - конвертацию пунктов меню в UiActionCatalog.
//
// Дочерний класс задает только:
// - список пунктов (label/status/enabled/actionId),
// - что делать при apply выбранного пункта.
class ContextMenuWidgetBase : public IUiWidget {
public:
    struct MenuItem {
        UiAction::Id actionId{UiAction::Id::None};
        std::string label{};
        std::string status{};
        bool enabled{true};
    };

    ContextMenuWidgetBase(std::string widgetId,
                          UiScene scene,
                          uint16_t frameWidth,
                          std::string defaultTitle,
                          std::string defaultKeysHint,
                          std::optional<UiLayoutTemplate> layoutTemplate) noexcept;

    const char* id() const noexcept override;
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

protected:
    // Дочерний класс возвращает актуальный набор пунктов меню.
    virtual std::vector<MenuItem> buildMenuItems_(const UiState& rtState,
                                                  const UiNavState& navState) const = 0;
    // Дочерний класс выполняет действие выбранного пункта.
    virtual WidgetOutput applyMenuItem_(UiAction::Id actionId,
                                        const UiState& rtState,
                                        UiNavState& navState) const = 0;
    // При необходимости дочерний класс может поменять поведение для action-слоя.
    virtual UiAction::Execution actionExecution_() const noexcept;
    // Подсказка высоты кадра (может быть переопределена дочерним виджетом).
    virtual uint16_t frameHeightHint_(std::size_t itemCount) const noexcept;

    uint16_t frameWidth() const noexcept;

private:
    struct LayoutModel {
        bool enabled{false};
        std::string title{};
        std::string keysHint{};
    };

    static uint16_t clampIndex_(uint16_t index, std::size_t count) noexcept;
    static uint16_t wrapPrev_(uint16_t index, std::size_t count) noexcept;
    static uint16_t wrapNext_(uint16_t index, std::size_t count) noexcept;

    void buildLayoutModel_(const UiLayoutTemplate& tpl);

    std::string widgetId_{};
    UiScene scene_{UiScene::Tracks};
    uint16_t frameWidth_{60};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};
};

} // namespace avantgarde

