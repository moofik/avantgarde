#include "service/UiStateStore.h"

namespace avantgarde {

UiStateStore::UiStateStore() {
    state_.tracks.clear();
}

UiState UiStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void UiStateStore::setState(const UiState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
}

void UiStateStore::setTransport(const UiTransportState& transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.transport = transport;
}

void UiStateStore::setTrack(std::size_t index, const UiTrackStateView& track) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= state_.tracks.size()) {
        return;
    }
    state_.tracks[index] = track;
}

void UiStateStore::setTelemetry(const UiTelemetryState& telemetry) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.telemetry = telemetry;
}

void UiStateStore::setPattern(const UiPatternState& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.pattern = pattern;
}

} // namespace avantgarde
