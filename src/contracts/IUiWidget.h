#pragma once

#include <string>
#include <vector>

#include "contracts/IUi.h"
#include "contracts/IUiInput.h"
#include "contracts/UiIntent.h"
#include "contracts/UiNavState.h"

namespace avantgarde {

// Минимальный text-buffer для scene/widget слоя.
// Сейчас этого достаточно для GB-style строкового UI, позже можно заменить
// на полноценный canvas без изменения интерфейса IUiWidget.
struct UiTextBuffer {
    std::vector<std::string> lines;

    // Полный сброс кадра перед новым render-pass.
    void clear() noexcept { lines.clear(); }
};

// Результат обработки input внутри конкретного виджета.
struct WidgetOutput {
    // true = событие обработано локально и не должно всплывать выше.
    bool handled{false};
    // Набор intents, который SceneHost/dispatcher применит после onInput.
    std::vector<UiIntent> intents;
};

// Базовый контракт любого экранного виджета.
// Виджет:
// - рисует только свою сцену
// - обрабатывает только scene-local input
// - возвращает intents вместо прямых вызовов runtime/control.
struct IUiWidget {
    virtual ~IUiWidget() = default;

    // Стабильный идентификатор для логирования/диагностики.
    virtual const char* id() const noexcept = 0;

    // Рендер виджета в буфер.
    // rtState = runtime snapshot, navState = UI-навигация/курсор/выбор.
    virtual void render(UiTextBuffer& out,
                        const UiState& rtState,
                        const UiNavState& navState) = 0;

    // Обработка одного input-action.
    // navState можно менять напрямую (курсор/selection/scene-local state).
    virtual WidgetOutput onInput(UiInputAction action,
                                 const UiState& rtState,
                                 UiNavState& navState) = 0;
};

} // namespace avantgarde
