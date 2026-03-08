#pragma once

#include <mutex>

#include "contracts/IUi.h"

namespace avantgarde {

class UiStateStore {
public:
    UiStateStore();

    UiState snapshot() const;

    void setTransport(const UiTransportState& transport);
    void setTrack(std::size_t index, const UiTrackStateView& track);
    void setTelemetry(const UiTelemetryState& telemetry);

private:
    mutable std::mutex mutex_;
    UiState state_;
};

} // namespace avantgarde
