#include "service/pattern/PatternSnapshotOrchestrator.h"

namespace avantgarde {

bool PatternSnapshotOrchestrator::captureActivePattern(std::span<ISnapshotable* const> sources) noexcept {
    if (!engine_) {
        return false;
    }
    const PatternId active = engine_->activePatternId();
    if (active == kInvalidPatternId) {
        return false;
    }
    PatternState state{};
    if (!builder_.buildFromSnapshotables(active, sources, state)) {
        return false;
    }
    return engine_->putPattern(state);
}

bool PatternSnapshotOrchestrator::putDefaultPattern(PatternId id,
                                                    const PatternTransportSnapshot& transport,
                                                    uint8_t trackCount) {
    if (!engine_) {
        return false;
    }
    const PatternState state = builder_.makeDefaultPattern(id, trackCount, transport);
    return engine_->putPattern(state);
}

} // namespace avantgarde
