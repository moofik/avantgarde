#pragma once

#include <cstdint>
#include <string>

#include "contracts/UiScene.h"
#include "contracts/UiAction.h"

namespace avantgarde {

// UI-only состояние навигации и выбора.
// Важно: эти данные не уходят в RT-движок напрямую.
struct UiNavState {
    // Текущий активный экран (scene router в UiSceneHost переключает это поле).
    UiScene scene{UiScene::Tracks};
    // Выбранный трек (0..N-1) для команд, которые адресуют track-local поведение.
    uint8_t selectedTrack{0};
    // Текущая страница списка треков (по 2 трека на страницу).
    uint16_t trackPage{0};

    // Универсальные индексы курсора/скролла для list-based экранов.
    // Конкретный виджет может использовать их по-своему.
    uint16_t cursor{0};
    uint16_t scroll{0};

    // Active Action Pointer (V2):
    // - actionScope определяет, сейчас выбран глобальный или scene-local список.
    // - globalActionIndex/sceneActionIndex хранят позицию в соответствующем списке.
    UiAction::Scope actionScope{UiAction::Scope::Scene};
    uint16_t globalActionIndex{0};
    uint16_t sceneActionIndex{0};

    // Выбранный FX-слот в FxList/FxEditor.
    uint16_t selectedFx{0};
    // Выбранный тип FX для действия "Add FX" в FxList.
    uint16_t selectedFxType{0};
    // Модальное окно выбора FX в сцене FxList (true = popup открыт).
    bool fxAddPopupOpen{false};
    // Текущая директория для File Manager.
    std::string managerCwd{"."};
    // Текст фильтра в File Manager (имя/маска/расширение).
    std::string managerFilter;
};

} // namespace avantgarde
