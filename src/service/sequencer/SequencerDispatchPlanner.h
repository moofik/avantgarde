#pragma once

#include <cstdint>
#include <vector>

#include "contracts/ISequencer.h"

namespace avantgarde {

class AutomationLane;
class EventLane;

/**
 * @brief Унифицированная запись dispatch-плана для одного тика/диапазона.
 *
 * Для MVP нужен детерминированный merge двух lane-потоков:
 * - EventLane (дискретные события)
 * - AutomationLane (кривые параметров)
 */
struct SequencerDispatchItem {
    enum class Source : uint8_t {
        Event = 0,
        Automation = 1
    };

    uint64_t sampleTime{0};
    Source source{Source::Automation};
    EventLaneEvent event{};
    AutomationPointEvent automation{};
};

/**
 * @brief Строит отсортированный dispatch-план из EventLane + AutomationLane.
 *
 * Правило порядка при равном sampleTime:
 * 1) EventLane
 * 2) AutomationLane
 */
class SequencerDispatchPlanner final {
public:
    static void buildRange(const EventLane& eventLane,
                           const AutomationLane& automationLane,
                           uint64_t beginSampleInclusive,
                           uint64_t endSampleExclusive,
                           std::vector<SequencerDispatchItem>& out);
};

} // namespace avantgarde
