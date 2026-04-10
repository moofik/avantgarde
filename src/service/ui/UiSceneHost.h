#pragma once

#include <array>
#include <memory>

#include "contracts/IUiWidget.h"

namespace avantgarde {

/**
 * @brief Роутер сцен и координатор UI-виджетов.
 *
 * Класс решает две задачи:
 * 1) управляет переходами между сценами (Tracks, Manager, FxList, FxEditor);
 * 2) конвертирует входные жесты UI в высокоуровневые UiIntent.
 *
 * Важно: UiSceneHost не рендерит кадры сам.
 * Он только выбирает активный виджет и просит его собрать UiPreparedLayout.
 *
 * Поток данных:
 * UiGesture -> UiSceneHost -> (global action или active widget) -> WidgetOutput{intents}
 */
class UiSceneHost {
public:
    /**
     * @brief Регистрирует виджет для сцены.
     * @param scene Сцена, для которой регистрируем реализацию.
     * @param widget Экземпляр виджета (владение передается в host).
     * @return false, если передан пустой unique_ptr.
     *
     * При повторной регистрации той же сцены старый виджет будет заменен.
     */
    bool registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget);

    /**
     * @brief Принудительно переключить текущую сцену.
     * @param scene Целевая сцена.
     *
     * Метод меняет только UI-навигацию (nav_.scene). RT-движок не затрагивается.
     */
    void setScene(UiScene scene) noexcept;
    /**
     * @brief Получить текущую сцену.
     */
    UiScene scene() const noexcept;

    /**
     * @brief Доступ к UI-навигации (mutable версия).
     *
     * Нужен верхнему уровню приложения для синхронизации курсоров/сцены.
     * Не должен использоваться для обхода контрактов виджетов.
     */
    UiNavState& nav() noexcept;
    /**
     * @brief Read-only доступ к UI-навигации.
     */
    const UiNavState& nav() const noexcept;

    /**
     * @brief Построить prepared-layout активной сцены.
     * @param out Выходной объект кадра.
     * @param rtState Снимок состояния UI из движка.
     * @return true при успешной сборке.
     *
     * Метод выбрасывает исключение, если:
     * - для сцены не зарегистрирован виджет;
     * - виджет не смог собрать layout.
     */
    bool buildPreparedActive(UiPreparedLayout& out, const UiState& rtState) const;

    /**
     * @brief Главная точка обработки пользовательского жеста.
     * @param action Входной gesture.
     * @param rtState Текущий state из движка.
     * @return WidgetOutput с intents и флагом handled.
     *
     * Порядок обработки:
     * 1) нормализация аппаратных клавиш в универсальные действия;
     * 2) host-level маршруты (scene navigation, hotkeys);
     * 3) active-action-pointer (global/scene scope);
     * 4) делегирование жеста активному виджету.
     */
    WidgetOutput handleGesture(UiGesture action, const UiState& rtState);

private:
    // Размер массива widgets_. Должен покрывать все значения UiScene.
    static constexpr std::size_t kSceneCount_ = static_cast<std::size_t>(UiScene::Count);

    /**
     * @brief Построить каталог global actions для active-action-pointer.
     *
     * Эти действия доступны вне зависимости от активной сцены.
     * Пример: play/stop, возврат назад, перелистывание трек-страниц.
     */
    UiActionCatalog queryGlobalActions_(const UiState& rtState) const;
    /**
     * @brief Применить один глобальный action.
     * @param action Выбранный action с уже заполненным op/delta.
     * @param rtState Актуальный runtime-state.
     * @return WidgetOutput (обычно intents + изменения nav_).
     */
    WidgetOutput onGlobalAction_(UiAction& action, const UiState& rtState);

    // Преобразование enum-сцены в индекс массива widgets_.
    static constexpr std::size_t sceneIndex_(UiScene scene) noexcept {
        return static_cast<std::size_t>(scene);
    }

    // Реестр "сцена -> виджет". На сцену хранится ровно один виджет.
    std::array<std::unique_ptr<IUiWidget>, kSceneCount_> widgets_{};
    // Текущее UI-only состояние навигации (сцена, курсоры, popup-флаги).
    UiNavState nav_{};
};

} // namespace avantgarde
