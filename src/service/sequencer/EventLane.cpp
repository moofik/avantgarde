#include "service/sequencer/EventLane.h"

#include <algorithm>
#include <limits>

namespace avantgarde {

uint64_t EventLane::addEvent(const EventLaneEvent& ev) {
    EventLaneEvent stored = ev;
    if (stored.eventId == 0u) {
        stored.eventId = nextEventId_++;
    } else if (stored.eventId >= nextEventId_) {
        nextEventId_ = stored.eventId + 1u;
    }
    events_.push_back(stored);
    sortEvents_(events_);
    return stored.eventId;
}

bool EventLane::removeEvent(uint64_t eventId) {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const EventLaneEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    events_.erase(it);
    return true;
}

void EventLane::clear() noexcept {
    events_.clear();
}

void EventLane::collectEventsInRange(uint64_t beginSampleInclusive,
                                     uint64_t endSampleExclusive,
                                     std::vector<EventLaneEvent>& out) const {
    for (const EventLaneEvent& ev : events_) {
        if (ev.sampleTime < beginSampleInclusive) {
            continue;
        }
        if (ev.sampleTime >= endSampleExclusive) {
            break;
        }
        out.push_back(ev);
    }
}

const std::vector<EventLaneEvent>& EventLane::events() const noexcept {
    return events_;
}

bool EventLane::updateEvent(uint64_t eventId, const EventLaneEvent& next) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const EventLaneEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    EventLaneEvent updated = next;
    updated.eventId = eventId;
    *it = std::move(updated);
    sortEvents_(events_);
    return true;
}

bool EventLane::nudgeEventTime(uint64_t eventId, int64_t deltaSamples) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const EventLaneEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    const uint64_t cur = it->sampleTime;
    uint64_t next = 0U;
    if (deltaSamples >= 0) {
        const uint64_t delta = static_cast<uint64_t>(deltaSamples);
        next = (cur > std::numeric_limits<uint64_t>::max() - delta)
                   ? std::numeric_limits<uint64_t>::max()
                   : static_cast<uint64_t>(cur + delta);
    } else {
        const uint64_t delta = static_cast<uint64_t>(-deltaSamples);
        next = (delta > cur) ? 0U : static_cast<uint64_t>(cur - delta);
    }
    if (next == cur) {
        return false;
    }
    it->sampleTime = next;
    sortEvents_(events_);
    return true;
}

void EventLane::sortEvents_(std::vector<EventLaneEvent>& events) {
    std::sort(events.begin(), events.end(),
              [](const EventLaneEvent& a, const EventLaneEvent& b) {
                  if (a.sampleTime != b.sampleTime) {
                      return a.sampleTime < b.sampleTime;
                  }
                  return a.eventId < b.eventId;
              });
}

} // namespace avantgarde
