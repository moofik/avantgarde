#include "service/UiStateComposer.h"

namespace avantgarde {

UiState UiStateComposer::compose(const UiState& baseState,
                                 const UiRuntimeTelemetryView& runtimeTelemetry) const noexcept {
    UiState out = baseState;
    out.telemetry.totalCallbacks = runtimeTelemetry.totalCallbacks;
    out.telemetry.xruns = runtimeTelemetry.xruns;
    out.telemetry.rtQueueOverflow = runtimeTelemetry.rtQueueOverflow;
    out.transport.sampleTime = runtimeTelemetry.totalCallbacks * static_cast<uint64_t>(runtimeTelemetry.blockFrames);
    return out;
}

} // namespace avantgarde
