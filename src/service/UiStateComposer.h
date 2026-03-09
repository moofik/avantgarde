#pragma once

#include <cstdint>

#include "contracts/IUi.h"

namespace avantgarde {

struct UiRuntimeTelemetryView {
    uint64_t totalCallbacks{0};
    uint64_t xruns{0};
    bool rtQueueOverflow{false};
    uint32_t blockFrames{0};
};

class UiStateComposer {
public:
    UiState compose(const UiState& baseState,
                    const UiRuntimeTelemetryView& runtimeTelemetry) const noexcept;
};

} // namespace avantgarde
