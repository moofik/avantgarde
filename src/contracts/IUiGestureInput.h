#pragma once

#include <cstdint>

namespace avantgarde {

// Низкоуровневые пользовательские жесты/команды управления.
// Это слой "физического" ввода (клавиши/энкодеры/кнопки),
// не бизнес-действия домена.
enum class UiGesture : uint8_t {
    None = 0,
    Quit,
    // Прямой выбор трека (индекс/номер передается в UiGestureEvent::value).
    SelectTrackDirect,
    // Прямой выбор паттерна (номер паттерна передается в UiGestureEvent::value).
    SelectPatternDirect,
    SelectPrevTrack,
    SelectNextTrack,
    TrackPagePrev,
    TrackPageNext,
    OpenSequencer,
    // Быстрый вход сразу в lane-редактор секвенсора.
    OpenPatternEdit,
    OpenManager,
    // Запрос на открытие sample-контекстного меню (scene-local, из SampleEdit).
    OpenSampleContextMenu,
    BackScene,
    ListUp,
    ListDown,
    ListEnter,
    ListParent,
    PreviewPlay,
    PreviewAutoToggle,
    PlayActiveTrack,
    StopActiveTrack,
    UnmuteActiveTrack,
    MuteActiveTrack,
    MuteToggleActiveTrack,
    ArmToggleActiveTrack,
    TrackSpeedUp,
    TrackSpeedDown,
    QuantNone,
    QuantBeat,
    QuantBar,
    BpmUp,
    BpmDown,
    ToggleMetronome,
    // Глобальный жест REC (единая кнопка записи для всех типов записи).
    // Точный тип записываемых данных определяется контекстом выбранного трека.
    Record,
    // Явное удаление выбранного объекта (например точки automation).
    DeleteObject,
    // Прямой выбор snapshot-слота (1..4 передается в UiGestureEvent::value).
    SnapshotSlotDirect,

    // Active Action Pointer controls (encoder-like model).
    ActionFocusPrev,
    ActionFocusNext,
    ActionAdjustPrev,
    ActionAdjustNext,
    ActionApply,
    ActionUndo,
    ActionRedo,
    ActionScopeToggle,
    ActionQuick,
    ActionAlt,
    ActionPress,
    ActionRelease,

    // F-row for hardware-like mapping.
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
};

// Сырые аппаратные контролы (еще до интерпретации в scene-aware UiGesture).
// Это единый "физический" слой ввода, который формирует platform-mapper.
enum class PrimitiveControl : uint8_t {
    None = 0,
    Quit,
    SelectTrack1,
    SelectTrack2,
    SelectTrack3,
    SelectTrack4,
    SelectPattern1,
    SelectPattern2,
    SelectPattern3,
    SelectPattern4,
    SelectPrevTrack,
    SelectNextTrack,
    TrackPagePrev,
    TrackPageNext,
    OpenSequencer,
    OpenPatternEdit,
    OpenManager,
    BackScene,
    ListUp,
    ListDown,
    ListEnter,
    ListParent,
    PreviewPlay,
    PreviewAutoToggle,
    PlayActiveTrack,
    StopActiveTrack,
    UnmuteActiveTrack,
    MuteActiveTrack,
    MuteToggleActiveTrack,
    ArmToggleActiveTrack,
    TrackSpeedUp,
    TrackSpeedDown,
    QuantNone,
    QuantBeat,
    QuantBar,
    BpmUp,
    BpmDown,
    ToggleMetronome,
    Record,
    DeleteObject,
    Snapshot1,
    Snapshot2,
    Snapshot3,
    Snapshot4,
    ActionFocusPrev,
    ActionFocusNext,
    ActionAdjustPrev,
    ActionAdjustNext,
    ActionApply,
    ActionUndo,
    ActionRedo,
    ActionScopeToggle,
    ActionQuick,
    ActionAlt,
    ActionPress,
    ActionRelease,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
};

// Фаза физического нажатия.
enum class PrimitivePhase : uint8_t {
    Down = 0,
    Up,
    Repeat
};

// Сырое событие с платформенного ввода.
struct PrimitiveInputEvent {
    PrimitiveControl control{PrimitiveControl::None};
    PrimitivePhase phase{PrimitivePhase::Down};
    uint64_t timestampMs{0};
};

// Тип нажатия физической кнопки/клавиши.
// Tap    - короткое нажатие,
// Hold   - удержание дольше порога,
// Repeat - автоповтор удерживаемой клавиши.
enum class UiPressType : uint8_t {
    Tap = 0,
    Hold,
    Repeat
};

struct UiGestureEvent {
    UiGesture action{UiGesture::None};
    // Числовой payload для параметризованных жестов (например direct-select 1..N).
    int16_t value{0};
    UiPressType press{UiPressType::Tap};
};

struct IUiGestureInput {
    virtual ~IUiGestureInput() = default;
    virtual bool poll(UiGestureEvent& out) noexcept = 0;
};

} // namespace avantgarde
