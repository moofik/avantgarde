#pragma once

#include "contracts/UiLayout.h"
#include "contracts/IUi.h"
#include "contracts/UiNavState.h"
#include "contracts/UiPreparedLayout.h"
#include "contracts/UiScene.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

/**
 * @brief Композер компонентов из декларативного layout-дерева.
 *
 * Задача класса:
 * - пройти по UiLayoutTemplate;
 * - для каждой функциональной ноды собрать typed-компонент;
 * - подставить значения из UiPreparedParams (bind/id -> value).
 *
 * Виджет не знает ничего о конкретном рендерере и не строит "пиксели":
 * он только готовит значения параметров, а UiNodeComponentComposer
 * собирает единый UiPreparedLayout.
 */
class UiNodeComponentComposer final {
public:
    /**
     * @brief Сконвертировать layout-ноды в готовые UI-компоненты.
     * @param scene Текущая сцена (нужна для bind-резолва и контекста).
     * @param layoutTemplate Декларативное дерево layout.
     * @param state Runtime-состояние UI (для capability-условий visible_if/state.if).
     * @param nav Runtime-навигация UI (для capability-условий visible_if/state.if).
     * @param params Prepared-параметры из виджета.
     * @param builder Приемник компонентов итогового кадра.
     */
    static void compose(UiScene scene,
                        const UiLayoutTemplate& layoutTemplate,
                        const UiState& state,
                        const UiNavState& nav,
                        const UiPreparedParams& params,
                        UiPreparedLayoutBuilder& builder);
};

} // namespace avantgarde
