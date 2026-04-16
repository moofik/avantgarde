#include "service/sequencer/SequencerDispatchPlanner.h"

#include <algorithm>

#include "service/sequencer/AutomationLane.h"
#include "service/sequencer/EventLane.h"

namespace avantgarde {

void SequencerDispatchPlanner::buildRange(const EventLane& eventLane,
                                          const AutomationLane& automationLane,
                                          uint64_t beginSampleInclusive,
                                          uint64_t endSampleExclusive,
                                          std::vector<SequencerDispatchItem>& out) {
    std::vector<EventLaneEvent> events{};
    std::vector<AutomationPointEvent> automation{};
    eventLane.collectEventsInRange(beginSampleInclusive, endSampleExclusive, events);
    automationLane.collectEventsInRange(beginSampleInclusive, endSampleExclusive, automation);

    out.reserve(out.size() + events.size() + automation.size());
    for (const EventLaneEvent& ev : events) {
        SequencerDispatchItem item{};
        item.sampleTime = ev.sampleTime;
        item.source = SequencerDispatchItem::Source::Event;
        item.event = ev;
        out.push_back(item);
    }
    for (const AutomationPointEvent& av : automation) {
        SequencerDispatchItem item{};
        item.sampleTime = av.point.sampleTime;
        item.source = SequencerDispatchItem::Source::Automation;
        item.automation = av;
        out.push_back(item);
    }

    std::sort(out.begin(), out.end(),
              [](const SequencerDispatchItem& a, const SequencerDispatchItem& b) {
                  if (a.sampleTime != b.sampleTime) {
                      return a.sampleTime < b.sampleTime;
                  }
                  // Ключевой инвариант: при равном времени Event всегда раньше Automation.
                  if (a.source != b.source) {
                      return a.source == SequencerDispatchItem::Source::Event;
                  }
                  const uint64_t aId = (a.source == SequencerDispatchItem::Source::Event)
                                           ? a.event.eventId
                                           : a.automation.eventId;
                  const uint64_t bId = (b.source == SequencerDispatchItem::Source::Event)
                                           ? b.event.eventId
                                           : b.automation.eventId;
                  return aId < bId;
              });
}

} // namespace avantgarde

