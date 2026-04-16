#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ITransport.h"
#include "IPattern.h"
#include "ISequencer.h"

namespace avantgarde {

enum class UiTrackState : uint8_t {
    Empty = 0,
    Stopped,
    Playing,
    Recording
};

// Упрощенный playback-режим трека для UI.
// Детальные launch/stop policy остаются в engine-параметрах,
// а UI в "из коробки" сценарии показывает только понятный toggle:
// Looper <-> Note.
enum class UiTrackPlaybackMode : uint8_t {
    Looper = 0,
    Note = 1
};

// Профиль пользовательского режима трека (4 понятных пресета):
// - Pattern       = note + loop on
// - PatternOnce   = note + loop off
// - Loop          = looper + loop on
// - OneShot       = looper + loop off
enum class UiTrackPlaybackProfile : uint8_t {
    Pattern = 0,
    PatternOnce = 1,
    Loop = 2,
    OneShot = 3
};

struct UiTransportState {
    bool playing{false};
    // Глобальный REC-флаг.
    // true  -> пользовательские изменения пишутся в секвенсор (automation/event по контексту).
    // false -> обычный live-control без записи.
    bool recordEnabled{false};
    float bpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{QuantizeMode::Bar};
    // Флаг встроенного метронома (клик по сетке 1/16).
    bool metronomeEnabled{false};
    uint8_t activeTrack{0};
    uint64_t sampleTime{0};
    // true, когда в hidden preview-voice запущено предпрослушивание сэмпла.
    // Это UI/control-флаг: используется для отображения preview-playhead в SampleEdit.
    bool previewPlaying{false};
    // Нормализованный playhead hidden preview-voice [0..1].
    // Актуален только когда previewPlaying=true.
    float previewPlayhead01{0.0f};
};

struct UiTrackStateView {
    uint8_t id{0};
    UiTrackState state{UiTrackState::Empty};
    uint32_t bars{4};
    float stretchRatio{1.0f};
    float gain01{1.0f};
    // true = трек заглушен (в master не микшируется).
    bool muted{false};
    // true = трек вооружен для записи/овerdub (для будущего record flow).
    bool armed{false};
    // Основной режим трека:
    // - Looper: длинные клипы/лупы, ignore-if-playing, manual-stop.
    // - Note: note-driven playback, retrigger-on-note-on, stop-by-note-off.
    UiTrackPlaybackMode playbackMode{UiTrackPlaybackMode::Looper};
    // Высокоуровневый UX-профиль режима (4 пресета).
    UiTrackPlaybackProfile playbackProfile{UiTrackPlaybackProfile::Loop};
    bool loop{false};
    // Границы playback-региона внутри клипа в нормализованном виде [0..1].
    float trimStart01{0.0f};
    float trimEnd01{1.0f};
    // Нормализованный playhead выбранного трека в пределах trim-региона [0..1].
    // Это именно трековый курсор, не глобальный sequencer/pattern playhead.
    float playhead01{0.0f};
    // true: скорость трека темпо-синхронизирована с project BPM/TS (stretch-to-bars).
    // false: скорость трека ручная и не пересчитывается от BPM.
    bool tempoSync{true};
    uint8_t fxCount{0};
    // Канонические ID FX по слотам (слот 0 -> fxChainIds[0] и т.д.).
    // UI использует это для именования списка FX и подбора профиля параметров в редакторе.
    std::vector<std::string> fxChainIds{};
    // Состояние FX-слотов: 1 = enabled, 0 = bypass.
    // Индекс совпадает с fxChainIds/слотом.
    std::vector<uint8_t> fxEnabled{};
    std::string clipName;
    // Абсолютный/рабочий путь к загруженному сэмплу (для сервисов анализа).
    std::string clipPath;
};

struct UiTelemetryState {
    uint64_t totalCallbacks{0};
    uint64_t xruns{0};
    bool rtQueueOverflow{false};
};

// Runtime-состояние pattern-подсистемы для UI.
// Важно: это отдельный домен, не часть transport.
struct UiPatternState {
    // Текущий активный паттерн.
    PatternId activeId{kInvalidPatternId};
    // Целевой паттерн, ожидающий квантизированного switch.
    PatternId pendingId{kInvalidPatternId};
    // true, если switch заявлен и еще не применен.
    bool armed{false};
    // Количество паттернов в банке (для UI-индикации).
    uint16_t bankSize{0};
};

// Тип lane в Sequencer View.
enum class UiSequencerLaneKind : uint8_t {
    Event = 0,
    Automation
};

// Краткое описание одного lane для списка в Sequencer View.
struct UiSequencerLaneView {
    uint16_t laneId{0};
    UiSequencerLaneKind kind{UiSequencerLaneKind::Event};
    EventLaneOp eventOp{EventLaneOp::SnapshotRecall};
    SequencerParamTarget target{};
    std::string label{};
    uint32_t pointCount{0};
};

// Нормализованное представление объекта lane (точка automation или событие event lane).
struct UiSequencerPointView {
    uint64_t objectId{0};
    SequencerTick tick{0};
    float value{0.0f};
    // Дополнительный small payload (например snapshot index/note id).
    uint16_t aux{0};
    // Короткая подпись для event lane (например S1/MUTE/NOTE).
    std::string label{};
};

// Runtime-снимок секвенсора для UI-слоя.
// Важно: это чисто control/view модель. RT-проигрывание остается в lane-сервисах.
struct UiSequencerState {
    PatternId patternId{kInvalidPatternId};
    uint16_t ppq{kSequencerPpq};
    uint32_t lengthBars{64};
    SequencerTick lengthTicks{static_cast<SequencerTick>(kSequencerPpq * 4u * 64u)};
    SequencerQuantize quant{SequencerQuantize::Quarter};
    // true: при wrap цикла параметры возвращаются к base snapshot (ResetOnLoop).
    // false: параметры продолжают текущее состояние (Continue).
    bool resetOnLoop{true};

    // true  -> пользователь в lane-focus режиме (редактирование точки/события).
    // false -> пользователь в режиме списка lane-ов.
    bool laneFocused{false};
    uint16_t activeLane{0};
    uint16_t activePoint{0};

    // Playhead текущего transport времени в tick-домене active pattern.
    SequencerTick playheadTick{0};
    // Scrub-позиция редактора (knob1).
    SequencerTick scrubTick{0};
    // Простой zoom-тир [1..8] для coarse/fine шагов.
    uint16_t zoom{1};
    // Tool/mode переключатель (knob6), зарезервирован под будущие инструменты.
    uint16_t tool{0};

    std::vector<UiSequencerLaneView> lanes{};
    std::vector<UiSequencerPointView> points{};
    std::string laneTitle{};
};

struct UiState {
    UiTransportState transport{};
    std::vector<UiTrackStateView> tracks{};
    UiTelemetryState telemetry{};
    UiPatternState pattern{};
    UiSequencerState sequencer{};
};

struct IUiRenderer {
    virtual ~IUiRenderer() = default;
    virtual void render(const UiState& state) = 0;
};

} // namespace avantgarde
