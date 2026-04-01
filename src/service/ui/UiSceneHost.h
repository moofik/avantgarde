#pragma once

#include <array>
#include <memory>

#include "contracts/IUiWidget.h"

namespace avantgarde {

// Роутер сцен и координатор виджетов.
// Хранит навигационное состояние и решает:
// - какой виджет активен
// - какие input-события глобальные, а какие делегируются виджету.
class UiSceneHost {
public:
    // Регистрирует реализацию виджета для конкретной сцены.
    // Повторная регистрация перезаписывает предыдущую.
    bool registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget);

    // Принудительное переключение активной сцены.
    void setScene(UiScene scene) noexcept;
    UiScene scene() const noexcept;

    // Прямой доступ к UI-only navigation state.
    UiNavState& nav() noexcept;
    const UiNavState& nav() const noexcept;

    // Рендер активной сцены в буфер.
    // Возвращает false, если для сцены не зарегистрирован виджет.
    bool renderActive(UiTextBuffer& out, const UiState& rtState) const;

    // Обрабатывает input:
    // 1) глобальные shortcuts (host-level)
    // 2) делегирование в активный виджет.
    WidgetOutput handleGesture(UiGesture action, const UiState& rtState);

private:
    // Формирует каталог глобальных pointer-экшенов (scope=Global).
    UiActionCatalog queryGlobalActions_(const UiState& rtState) const;
    // Применяет глобальный экшен и возвращает intents/UI-nav изменения.
    WidgetOutput onGlobalAction_(UiAction& action, const UiState& rtState);

    // Индексирование std::array по enum UiScene.
    static constexpr std::size_t sceneIndex_(UiScene scene) noexcept {
        return static_cast<std::size_t>(scene);
    }

    // Registry: one widget per scene.
    std::array<std::unique_ptr<IUiWidget>, 4> widgets_{};
    // Текущее UI-only состояние навигации.
    UiNavState nav_{};
};

} // namespace avantgarde
