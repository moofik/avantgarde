#include "service/UiStateStore.h"

namespace avantgarde {

UiStateStore::UiStateStore() {
    state_.tracks[0].id = 0;
    state_.tracks[1].id = 1;
}

UiState UiStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void UiStateStore::setTransport(const UiTransportState& transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.transport = transport;
}

void UiStateStore::setTrack(std::size_t index, const UiTrackStateView& track) {
    if (index >= state_.tracks.size()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_.tracks[index] = track;
}

void UiStateStore::setTelemetry(const UiTelemetryState& telemetry) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.telemetry = telemetry;
}

} // namespace avantgarde
