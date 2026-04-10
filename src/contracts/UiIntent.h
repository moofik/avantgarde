#pragma once

#include <cstdint>
#include <string>

#include "contracts/UiScene.h"

namespace avantgarde {

// Команды высокого уровня от виджетов к application-layer dispatcher.
// Виджеты создают intent, но не знают про конкретные runtime/control API.
enum class UiIntentType : uint8_t {
    None = 0,
    // Переключение экрана.
    OpenScene,
    // Вернуться на предыдущий экран (стек/history решает хост).
    Back,
    // Загрузить файл-сэмпл в track/slot.
    LoadSampleToTrack,
    // Очистить загруженный сэмпл в выбранном треке (slot0).
    ClearTrackSample,
    // Добавить эффект в цепочку трека.
    AddFxToTrack,
    // Удалить эффект из цепочки трека.
    RemoveFxFromTrack,
    // Открыть редактор параметров выбранного эффекта.
    OpenFxEditor,
    // Установить конкретный параметр эффекта.
    SetFxParam,
    // Включить/выключить (bypass) FX-слот без удаления из цепочки.
    SetFxEnabled,
    // Запрос на предпрослушку выбранного файла (preview voice).
    PreviewRequest,
    // Остановка предпрослушки.
    PreviewStop,
    // Явная установка активного трека в UI/control модели.
    SetActiveTrack,
    // Явная установка mute для трека (value: 0.0=off, 1.0=on).
    SetTrackMuted,
    // Явная установка arm для трека (value: 0.0=off, 1.0=on).
    SetTrackArmed,
    // Переключение "из коробки" режима трека:
    // value: 1.0 = LOOPER, 0.0 = NOTE/SAMPLER.
    // Под капотом это пресет нескольких трековых policy-параметров.
    SetTrackLooperMode,
    // Явная установка одного из 4 профилей playback-режима трека:
    // value: 0=PATTERN, 1=PATTERN_ONCE, 2=LOOP, 3=ONESHOT.
    SetTrackPlaybackProfile,
    // Явная установка speed/stretch для трека.
    SetTrackSpeed,
    // Явная установка gain [0..1] для трека.
    SetTrackGain,
    // Явная установка стартовой границы playback-региона [0..1].
    SetTrackTrimStart,
    // Явная установка конечной границы playback-региона [0..1].
    SetTrackTrimEnd,
    // Явная установка режима квантизации транспорта
    // (value: 0=None, 1=Beat, 2=Bar).
    SetTransportQuant,
    // Явная установка BPM транспорта.
    SetTransportBpm,
    // Включить/выключить метроном проекта (value: 1.0=on, 0.0=off).
    SetMetronomeEnabled,
    // Детект BPM из сэмпла активного трека с учетом его speed и установка project BPM.
    DetectProjectBpmFromTrack,
    // Явная установка play/stop транспорта (value: 1.0=play, 0.0=stop).
    SetTransportPlaying,
    // Запрос на switch к предыдущему паттерну (квантизация берется из транспорта).
    SwitchPatternPrev,
    // Запрос на switch к следующему паттерну (квантизация берется из транспорта).
    SwitchPatternNext,
    // Запрос на switch к конкретному паттерну (value = pattern id, 1..N).
    SwitchPatternSet,
    // Intent-обертки над текущими transport/track действиями движка.
    EnginePlayTrack,
    EngineStopTrack,
    EngineSetQuant,
    EngineSetBpm,
    EngineSetTrackSpeed
};

// Унифицированный payload для любого UiIntentType.
// Поля используются частично в зависимости от типа.
struct UiIntent {
    UiIntentType type{UiIntentType::None};
    // Целевая сцена для навигационных intent-ов (OpenScene/Back).
    UiScene scene{UiScene::Tracks};
    // Флаги сброса UI-навигации. Применяются внешним роутером.
    bool resetCursor{false};
    bool resetScroll{false};
    bool resetSceneActionIndex{false};
    bool resetSelectedFx{false};
    bool closeFxAddPopup{false};
    // Целевой трек (если intent track-scoped).
    uint8_t track{0};
    // FX slot в цепочке трека (для FxList/FxEditor действий).
    uint8_t fxSlot{0};
    // Индекс параметра эффекта.
    uint16_t paramIndex{0};
    // Значение параметра / BPM / speed и т.д.
    float value{0.0f};
    // Доп. значение для расширений (например relative delta/secondary payload).
    float value2{0.0f};
    // Путь к файлу (для load intents) или иной string payload.
    std::string path;
};

} // namespace avantgarde
