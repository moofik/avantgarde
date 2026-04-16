#include "service/sequencer/AutomationLane.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avantgarde {

bool AutomationLane::beginGesture(const SequencerParamTarget& target,
                                  AutomationInterpolationMode interpolation) {
    if (pending_.has_value()) {
        return false;
    }
    PendingGesture g{};
    g.target = target;
    g.interpolation = interpolation;
    pending_ = std::move(g);
    return true;
}

bool AutomationLane::pushGesturePoint(uint64_t sampleTime, float value) {
    if (!pending_.has_value()) {
        return false;
    }
    PendingGesture& g = *pending_;
    // Схлопываем подряд идущие точки с одинаковым временем:
    // внутри одного аудио-тика последняя позиция ручки важнее предыдущей.
    if (!g.points.empty() && g.points.back().sampleTime == sampleTime) {
        g.points.back().value = value;
        return true;
    }
    g.points.push_back(AutomationPoint{
        .sampleTime = sampleTime,
        .value = value
    });
    return true;
}

bool AutomationLane::commitGesture(const TransportRtSnapshot& transport,
                                   QuantizeMode quantize,
                                   AutomationGestureCommitResult& out) {
    out = AutomationGestureCommitResult{};
    if (!pending_.has_value()) {
        return false;
    }
    PendingGesture g = std::move(*pending_);
    pending_.reset();
    if (g.points.empty()) {
        return false;
    }

    std::sort(g.points.begin(), g.points.end(),
              [](const AutomationPoint& a, const AutomationPoint& b) {
                  return a.sampleTime < b.sampleTime;
              });

    const uint64_t srcStart = g.points.front().sampleTime;
    const uint64_t quantizedStart = quantizeForwardSample_(srcStart, transport, quantize);
    const uint64_t shift = (quantizedStart >= srcStart) ? (quantizedStart - srcStart) : 0;

    GestureBatch batch{};
    batch.batchId = nextBatchId_++;
    batch.inserted.reserve(g.points.size());
    for (const AutomationPoint& p : g.points) {
        AutomationPointEvent ev{};
        ev.eventId = nextEventId_++;
        ev.target = g.target;
        ev.interpolation = g.interpolation;
        ev.point.sampleTime = p.sampleTime + shift;
        ev.point.value = p.value;
        events_.push_back(ev);
        batch.inserted.push_back(ev);
    }
    sortEvents_(events_);

    undoStack_.push_back(batch);
    // Любой новый commit ломает ветку redo, как в обычной DAW-модели.
    redoStack_.clear();

    out.batchId = batch.batchId;
    out.insertedPoints = static_cast<uint32_t>(batch.inserted.size());
    out.quantizedStartSample = quantizedStart;
    return true;
}

void AutomationLane::cancelGesture() noexcept {
    pending_.reset();
}

bool AutomationLane::undoLastGesture() noexcept {
    if (undoStack_.empty()) {
        return false;
    }
    GestureBatch batch = std::move(undoStack_.back());
    undoStack_.pop_back();
    removeEventsById_(batch.inserted);
    redoStack_.push_back(std::move(batch));
    return true;
}

bool AutomationLane::redoLastGesture() noexcept {
    if (redoStack_.empty()) {
        return false;
    }
    GestureBatch batch = std::move(redoStack_.back());
    redoStack_.pop_back();
    for (const AutomationPointEvent& ev : batch.inserted) {
        events_.push_back(ev);
    }
    sortEvents_(events_);
    undoStack_.push_back(std::move(batch));
    return true;
}

void AutomationLane::collectEventsInRange(uint64_t beginSampleInclusive,
                                          uint64_t endSampleExclusive,
                                          std::vector<AutomationPointEvent>& out) const {
    for (const AutomationPointEvent& ev : events_) {
        if (ev.point.sampleTime < beginSampleInclusive) {
            continue;
        }
        if (ev.point.sampleTime >= endSampleExclusive) {
            break;
        }
        out.push_back(ev);
    }
}

const std::vector<AutomationPointEvent>& AutomationLane::events() const noexcept {
    return events_;
}

uint64_t AutomationLane::addPoint(const SequencerParamTarget& target,
                                  AutomationInterpolationMode interpolation,
                                  uint64_t sampleTime,
                                  float value) {
    AutomationPointEvent ev{};
    ev.eventId = nextEventId_++;
    ev.target = target;
    ev.interpolation = interpolation;
    ev.point.sampleTime = sampleTime;
    ev.point.value = value;
    events_.push_back(ev);
    sortEvents_(events_);
    clearUndoRedo_();
    return ev.eventId;
}

bool AutomationLane::removeEvent(uint64_t eventId) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const AutomationPointEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    events_.erase(it);
    clearUndoRedo_();
    return true;
}

bool AutomationLane::nudgeEventTime(uint64_t eventId, int64_t deltaSamples) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const AutomationPointEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }

    const uint64_t cur = it->point.sampleTime;
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
    it->point.sampleTime = next;
    sortEvents_(events_);
    clearUndoRedo_();
    return true;
}

bool AutomationLane::setEventTime(uint64_t eventId, uint64_t sampleTime) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const AutomationPointEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    if (it->point.sampleTime == sampleTime) {
        return false;
    }
    it->point.sampleTime = sampleTime;
    sortEvents_(events_);
    clearUndoRedo_();
    return true;
}

bool AutomationLane::setEventValue(uint64_t eventId, float value) noexcept {
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [eventId](const AutomationPointEvent& ev) {
                                     return ev.eventId == eventId;
                                 });
    if (it == events_.end()) {
        return false;
    }
    if (std::fabs(it->point.value - value) < 1e-6f) {
        return false;
    }
    it->point.value = value;
    clearUndoRedo_();
    return true;
}

uint64_t AutomationLane::quantizeForwardSample_(uint64_t now,
                                                const TransportRtSnapshot& transport,
                                                QuantizeMode quantize) noexcept {
    if (quantize == QuantizeMode::None) {
        return now;
    }
    const uint64_t quantum = computeQuantumSamples_(transport, quantize);
    if (quantum <= 1u) {
        return now;
    }
    const uint64_t rem = now % quantum;
    if (rem == 0u) {
        return now;
    }
    return now + (quantum - rem);
}

uint64_t AutomationLane::computeQuantumSamples_(const TransportRtSnapshot& transport,
                                                QuantizeMode quantize) noexcept {
    if (quantize == QuantizeMode::None) {
        return 1u;
    }
    const double bpm = (std::isfinite(transport.bpm) && transport.bpm > 0.0f)
                           ? static_cast<double>(transport.bpm)
                           : 120.0;
    // В lane-коммите используем transport snapshot как источник SR-домена.
    // Здесь нет прямого sampleRate, поэтому берем стандартное для проекта 48k.
    constexpr double kDefaultSampleRate = 48000.0;
    const uint64_t beatSamples =
        static_cast<uint64_t>(std::max(1.0, std::round(kDefaultSampleRate * 60.0 / bpm)));
    if (quantize == QuantizeMode::Beat) {
        return beatSamples;
    }
    const uint8_t num = (transport.tsNum == 0u) ? static_cast<uint8_t>(4u) : transport.tsNum;
    const uint8_t den = (transport.tsDen == 0u) ? static_cast<uint8_t>(4u) : transport.tsDen;
    const uint64_t scaledNum = static_cast<uint64_t>(num) * 4ULL;
    return std::max<uint64_t>(1u, (beatSamples * scaledNum) / den);
}

bool AutomationLane::sameTarget_(const SequencerParamTarget& a,
                                 const SequencerParamTarget& b) noexcept {
    return a.track == b.track && a.slot == b.slot && a.module == b.module && a.param == b.param;
}

void AutomationLane::sortEvents_(std::vector<AutomationPointEvent>& events) {
    std::sort(events.begin(), events.end(),
              [](const AutomationPointEvent& a, const AutomationPointEvent& b) {
                  if (a.point.sampleTime != b.point.sampleTime) {
                      return a.point.sampleTime < b.point.sampleTime;
                  }
                  if (!sameTarget_(a.target, b.target)) {
                      if (a.target.track != b.target.track) {
                          return a.target.track < b.target.track;
                      }
                      if (a.target.slot != b.target.slot) {
                          return a.target.slot < b.target.slot;
                      }
                      if (a.target.module != b.target.module) {
                          return a.target.module < b.target.module;
                      }
                      if (a.target.param != b.target.param) {
                          return a.target.param < b.target.param;
                      }
                  }
                  return a.eventId < b.eventId;
              });
}

void AutomationLane::removeEventsById_(const std::vector<AutomationPointEvent>& inserted) noexcept {
    if (inserted.empty() || events_.empty()) {
        return;
    }
    std::vector<uint64_t> ids{};
    ids.reserve(inserted.size());
    for (const AutomationPointEvent& ev : inserted) {
        ids.push_back(ev.eventId);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    events_.erase(std::remove_if(events_.begin(), events_.end(),
                                 [&ids](const AutomationPointEvent& ev) {
                                     return std::binary_search(ids.begin(), ids.end(), ev.eventId);
                                 }),
                  events_.end());
}

void AutomationLane::clearUndoRedo_() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace avantgarde
