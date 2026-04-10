#pragma once

#include <cstdint>

namespace avantgarde {

// Логические экраны UI. Это не "режим рендера", а именно состояние навигации.
// Один и тот же Scene может отрисовываться любым backend-рендерером.
enum class UiScene : uint8_t {
    // Базовый экран плеера: транспорт + 2 трека.
    Tracks = 0,
    // Контекстное меню активного трека (операции над клипом/треком).
    TrackContext,
    // Редактор сэмпла активного трека (speed/gain/start/end + preview).
    SampleEdit,
    // Экран файлового менеджера для выбора и загрузки сэмплов.
    Manager,
    // Экран списка эффектов активного трека.
    FxList,
    // Экран параметров конкретного эффекта.
    FxEditor,
    // Размер enum для массивов/таблиц scene-router.
    Count
};

} // namespace avantgarde
