#pragma once

#include <cstdint>
#include <string>

#include "contracts/UiScene.h"

namespace avantgarde {

// UI-only состояние навигации и выбора.
// Важно: эти данные не уходят в RT-движок напрямую.
struct UiNavState {
    // Текущий активный экран (scene router в UiSceneHost переключает это поле).
    UiScene scene{UiScene::Tracks};
    // Выбранный трек (0..1) для команд, которые адресуют track-local поведение.
    uint8_t selectedTrack{0};

    // Универсальные индексы курсора/скролла для list-based экранов.
    // Конкретный виджет может использовать их по-своему.
    uint16_t cursor{0};
    uint16_t scroll{0};

    // Выбранный FX-слот в FxList/FxEditor.
    uint16_t selectedFx{0};
    // Текущая директория для File Manager.
    std::string managerCwd{"."};
    // Текст фильтра в File Manager (имя/маска/расширение).
    std::string managerFilter;
};

} // namespace avantgarde
