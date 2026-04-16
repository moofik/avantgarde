#pragma once

#include <cstdint>
#include <string>

#include "contracts/UiScene.h"

namespace avantgarde {

// Канонические HUD-события, которые может отправлять любой слой UI через UiIntent.
enum class UiHudIntentEvent : uint8_t {
    None = 0,
    SnapshotCaptured,
    SnapshotApplied
};

// Уровень важности ad-hoc HUD-текста.
enum class UiHudIntentLevel : uint8_t {
    Info = 0,
    Action,
    Critical
};

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
    // Включить/выключить tempo sync выбранного трека (value: 1.0=on, 0.0=off).
    SetTrackTempoSync,
    // Явная установка режима квантизации транспорта
    // (value: 0=None, 1=Beat, 2=Bar).
    SetTransportQuant,
    // Явная установка BPM транспорта.
    SetTransportBpm,
    // Включить/выключить метроном проекта (value: 1.0=on, 0.0=off).
    SetMetronomeEnabled,
    // Детект BPM из сэмпла активного трека с учетом его speed и HUD-уведомление.
    // Значение проекта не меняется.
    DetectProjectBpmFromTrack,
    // Явная установка play/stop транспорта (value: 1.0=play, 0.0=stop).
    SetTransportPlaying,
    // Запрос на switch к предыдущему паттерну (квантизация берется из транспорта).
    SwitchPatternPrev,
    // Запрос на switch к следующему паттерну (квантизация берется из транспорта).
    SwitchPatternNext,
    // Запрос на switch к конкретному паттерну (value = pattern id, 1..N).
    SwitchPatternSet,
    // Переключить lane-focus режим секвенсора (value: 0/1).
    SequencerSetLaneFocus,
    // Выбрать активный lane секвенсора (value: lane index).
    SequencerSetActiveLane,
    // Выбрать активный объект внутри lane (value: object index).
    SequencerSetActiveObject,
    // Установить scrub tick в секвенсоре (value: absolute tick).
    SequencerSetScrubTick,
    // Сместить выбранный объект по времени (value: delta ticks).
    SequencerNudgeObjectTime,
    // Изменить значение выбранного объекта (automation/event payload).
    SequencerAdjustObjectValue,
    // Установить zoom tier секвенсора [1..8].
    SequencerSetZoom,
    // Установить tool/mode индекс секвенсора.
    SequencerSetTool,
    // Установить режим поведения на границе цикла паттерна:
    // value: 1.0 = ResetOnLoop, 0.0 = Continue.
    SequencerSetLoopMode,
    // Установить длину паттерна в барах (четное число, кратное 2).
    SequencerSetPatternLengthBars,
    // Установить квантизацию секвенсора:
    // value: 0=None, 1=1/16, 2=1/8, 3=1/4, 4=Bar.
    SequencerSetQuant,
    // Добавить точку/событие в текущей scrub позиции.
    SequencerAddObjectAtCursor,
    // Удалить выбранную точку/событие.
    SequencerDeleteSelectedObject,
    // Удалить выбранный lane целиком вместе со всеми связанными точками/событиями.
    SequencerDeleteSelectedLane,
    // Snapshot intent-ы (обрабатываются SnapshotManager-ом на уровне orchestration).
    // Trigger: policy-based (capture/recall) для текущего режима и контекста.
    SnapshotTriggerSlot,
    // Явный capture в слот snapshot (slotIndex в snapshotSlot, track/fxSlot задают контекст).
    SnapshotCaptureSlot,
    // Явный recall из snapshot-слота (slotIndex в snapshotSlot).
    SnapshotRecallSlot,
    // Нотификация о том, что snapshot успешно captured.
    SnapshotCaptured,
    // Нотификация о том, что snapshot успешно applied.
    SnapshotApplied,
    // Показать HUD-уведомление через централизованный HUD manager.
    // Если задан hudEvent != None, используется event-реестр HUD.
    // Если hudEvent == None и hudText не пустой, показывается ad-hoc текст с hudLevel.
    HudNotify,
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
    // Параметры preview для PreviewRequest:
    // - speed: playback-inc preview-голоса,
    // - start/end: нормализованный playback-регион [0..1],
    // - gain: уровень preview-голоса [0..1] (обычно равен gain выбранного трека).
    float previewSpeed{1.0f};
    float previewStart01{0.0f};
    float previewEnd01{1.0f};
    float previewGain{1.0f};
    // Snapshot slot [0..3] для snapshot-intent-ов.
    uint8_t snapshotSlot{0};
    // Поля HUD-intent (UiIntentType::HudNotify).
    UiHudIntentEvent hudEvent{UiHudIntentEvent::None};
    UiHudIntentLevel hudLevel{UiHudIntentLevel::Info};
    // Доп. payload для event-нотификаций (например slot index для snapshot).
    uint8_t hudSlot{0};
    // Текст ad-hoc уведомления (если hudEvent == None).
    std::string hudText{};
};

} // namespace avantgarde
