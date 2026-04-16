#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "ITransport.h"
#include "types.h"

namespace avantgarde {

// Базовая музыкальная сетка секвенсора (MVP).
// 96 тиков на четверть — компромисс между точностью и нагрузкой.
// Примеры:
// - 1/16 = 24 ticks
// - 1/32 = 12 ticks
// - 1/8 triplet = 32 ticks
using SequencerTick = uint32_t;
constexpr uint16_t kSequencerPpq = 96u;

// Сетка квантизации секвенсора (редактор + запись lane-событий).
// Это отдельный домен от transport QuantizeMode, потому что секвенсору
// нужна более мелкая сетка (1/16, 1/8), даже если transport живет в coarse-режиме.
enum class SequencerQuantize : uint8_t {
    None = 0,
    Sixteenth = 1,
    Eighth = 2,
    Quarter = 3,
    Bar = 4
};

// Универсальный адрес automation/event таргета.
// module оставлен для будущих сложных FX-цепочек (в MVP обычно 0).
struct SequencerParamTarget {
    int16_t track{-1};
    int16_t slot{-1};
    uint16_t module{0};
    uint16_t param{0};
};

// Интерполяция между соседними automation-точками.
enum class AutomationInterpolationMode : uint8_t {
    Hold = 0,
    Linear = 1
};

// Одна automation-точка в абсолютном времени транспорта (sampleTime).
struct AutomationPoint {
    uint64_t sampleTime{0};
    float value{0.0f};
};

// Нормализованная automation-запись в lane.
struct AutomationPointEvent {
    uint64_t eventId{0};
    SequencerParamTarget target{};
    AutomationInterpolationMode interpolation{AutomationInterpolationMode::Linear};
    AutomationPoint point{};
};

// Результат commit одного gesture-батча.
struct AutomationGestureCommitResult {
    uint64_t batchId{0};
    uint32_t insertedPoints{0};
    uint64_t quantizedStartSample{0};
};

// Контракт automation-lane (MVP: запись, квантизированный commit, batch undo/redo).
struct IAutomationLane {
    virtual ~IAutomationLane() = default;
    virtual bool beginGesture(const SequencerParamTarget& target,
                              AutomationInterpolationMode interpolation) = 0;
    virtual bool pushGesturePoint(uint64_t sampleTime, float value) = 0;
    virtual bool commitGesture(const TransportRtSnapshot& transport,
                               QuantizeMode quantize,
                               AutomationGestureCommitResult& out) = 0;
    virtual void cancelGesture() noexcept = 0;
    virtual bool undoLastGesture() noexcept = 0;
    virtual bool redoLastGesture() noexcept = 0;
    virtual void collectEventsInRange(uint64_t beginSampleInclusive,
                                      uint64_t endSampleExclusive,
                                      std::vector<AutomationPointEvent>& out) const = 0;
};

// Дискретные события второй фазы секвенсора (EventLane).
enum class EventLaneOp : uint8_t {
    FxBypassSet = 0,
    TrackMuteSet = 1,
    TrackArmSet = 2,
    TrackPitchSet = 3,
    SnapshotRecall = 4,
    NoteOn = 5,
    NoteOff = 6
};

// Типизированные payload-структуры EventLane.
// Нужны для строгой модели данных и сериализации project.json без "магии"
// в index/value полях.
struct EventSnapshotRecallPayload {
    uint16_t snapshotId{0};
};
struct EventTrackMutePayload {
    bool muted{false};
};
struct EventTrackArmPayload {
    bool armed{false};
};
struct EventFxBypassPayload {
    bool bypass{false};
};
struct EventTrackPitchPayload {
    float semitones{0.0f};
};
struct EventNoteOnPayload {
    uint8_t note{60};
    uint8_t velocity{100};
    float detune{0.0f};
};
struct EventNoteOffPayload {
    uint8_t note{60};
};

using EventLanePayload = std::variant<std::monostate,
                                      EventSnapshotRecallPayload,
                                      EventTrackMutePayload,
                                      EventTrackArmPayload,
                                      EventFxBypassPayload,
                                      EventTrackPitchPayload,
                                      EventNoteOnPayload,
                                      EventNoteOffPayload>;

// Событие EventLane в абсолютном времени транспорта (sampleTime).
struct EventLaneEvent {
    uint64_t eventId{0};
    // Legacy-временной домен для текущего playback-пайплайна.
    uint64_t sampleTime{0};
    // Канонический домен editor/model (PPQ).
    SequencerTick tick{0};
    EventLaneOp op{EventLaneOp::SnapshotRecall};
    SequencerParamTarget target{};
    // Новый типизированный payload. Для новых сценариев использовать именно его.
    EventLanePayload payload{};
    // Legacy-поля оставлены для совместимости существующего кода.
    uint16_t index{0};
    float value{0.0f};
    uint16_t snapshotId{0}; // используется при op == SnapshotRecall
};

// Контракт EventLane (M2): хранение/чтение дискретных событий.
struct IEventLane {
    virtual ~IEventLane() = default;
    virtual uint64_t addEvent(const EventLaneEvent& ev) = 0;
    virtual bool removeEvent(uint64_t eventId) = 0;
    virtual void clear() noexcept = 0;
    virtual void collectEventsInRange(uint64_t beginSampleInclusive,
                                      uint64_t endSampleExclusive,
                                      std::vector<EventLaneEvent>& out) const = 0;
};

} // namespace avantgarde
