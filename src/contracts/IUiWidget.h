#pragma once

#include <string>
#include <vector>

#include "contracts/IUi.h"
#include "contracts/UiAction.h"
#include "contracts/IUiGestureInput.h"
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

// Результат обработки жеста внутри конкретного виджета.
struct WidgetOutput {
    // true = событие обработано локально и не должно всплывать выше.
    bool handled{false};
    // Набор intents, который SceneHost/dispatcher применит после onGesture.
    std::vector<UiIntent> intents;
};

// Базовый контракт любого экранного виджета.
// Виджет:
// - рисует только свою сцену
// - обрабатывает только scene-local жесты
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

    // Обработка одного gesture-события.
    // navState можно менять напрямую (курсор/selection/scene-local state).
    virtual WidgetOutput onGesture(UiGesture action,
                                 const UiState& rtState,
                                 UiNavState& navState) = 0;

    // V2 контракт для Active Action Pointer:
    // read-only query, который возвращает scene-local набор экшенов
    // и текущий индекс активного pointer.
    // Метод ничего не мутирует в виджете и в переданных состояниях.
    virtual UiActionCatalog queryAvailableActions(const UiState&,
                                                  const UiNavState&) const {
        return {};
    }

    // V2 контракт для применения pointer-операции.
    // Вызов всегда получает единую сущность UiAction (включая id/op/state).
    // Ключевая идея пайплайна:
    //   UiAction + UiNavState (+ текущий UiState) => UiIntent
    // То есть виджет сам маппит пользовательский action в intent,
    // а верхние слои не знают деталей конкретного экшена.
    // По умолчанию обработка отсутствует.
    virtual WidgetOutput onAction(UiAction&,
                                  const UiState&,
                                  UiNavState&) {
        return {};
    }
};

} // namespace avantgarde
