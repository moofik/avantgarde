#pragma once

#include <cstdint>
#include <vector>

#include "contracts/ISequencer.h"

namespace avantgarde {

/**
 * @brief Базовая реализация EventLane (дискретные события секвенсора).
 *
 * Назначение:
 * - Хранит разовые события в абсолютном sampleTime.
 * - Поддерживает чтение диапазона событий для RT/dispatch слоя.
 * - Поддерживает события snapshot-recall (под фазу макросов/сценариев).
 */
class EventLane final : public IEventLane {
public:
    uint64_t addEvent(const EventLaneEvent& ev) override;
    bool removeEvent(uint64_t eventId) override;
    void clear() noexcept override;

    void collectEventsInRange(uint64_t beginSampleInclusive,
                              uint64_t endSampleExclusive,
                              std::vector<EventLaneEvent>& out) const override;

    /**
     * @brief Доступ к полному списку событий для диагностики/тестов.
     */
    const std::vector<EventLaneEvent>& events() const noexcept;
    // Обновить событие целиком по eventId.
    bool updateEvent(uint64_t eventId, const EventLaneEvent& next) noexcept;
    // Сместить событие по времени.
    bool nudgeEventTime(uint64_t eventId, int64_t deltaSamples) noexcept;

private:
    static void sortEvents_(std::vector<EventLaneEvent>& events);

private:
    std::vector<EventLaneEvent> events_{};
    uint64_t nextEventId_{1};
};

} // namespace avantgarde
