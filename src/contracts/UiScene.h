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
    // Контекстное меню sample-операций (preview/load/detect bpm).
    SampleContextMenu,
    // Экран файлового менеджера для выбора и загрузки сэмплов.
    Manager,
    // Экран списка эффектов активного трека.
    FxList,
    // Экран параметров конкретного эффекта.
    FxEditor,
    // Экран секвенсора (lane list + lane focus editor).
    Sequencer,
    // Фокус-экран одного выбранного lane (кривая automation или event-таймлайн).
    SequencerLane,
    // Отдельный экран редактирования параметров паттерна (LEN / QUANT / LOOP MODE).
    PatternEdit,
    // Размер enum для массивов/таблиц scene-router.
    Count
};

} // namespace avantgarde
