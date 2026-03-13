#pragma once

#include <cstdint>

namespace avantgarde {

// Логические экраны UI. Это не "режим рендера", а именно состояние навигации.
// Один и тот же Scene может отрисовываться в terminal/macOS window/SPI backend.
enum class UiScene : uint8_t {
    // Базовый экран плеера: транспорт + 2 трека.
    Tracks = 0,
    // Экран файлового менеджера для выбора и загрузки сэмплов.
    Manager,
    // Экран списка эффектов активного трека.
    FxList,
    // Экран параметров конкретного эффекта.
    FxEditor
};

} // namespace avantgarde
