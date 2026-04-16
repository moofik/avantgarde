#pragma once

#include <cstdint>
#include <vector>

#include "contracts/IClipTrack.h"
#include "contracts/ITrack.h"

namespace avantgarde {

// Domain-level резолвер возможностей трека.
// Позволяет держать основной пул как ITrack* и получать optional capability
// (например IClipTrack) только в тех use-case, где она реально нужна.
class TrackFeatureResolver final {
public:
    void bind(const std::vector<ITrack*>* tracks) noexcept;

    ITrack* track(uint8_t trackId) const noexcept;

    IClipTrack* clipTrack(uint8_t trackId) const noexcept;

private:
    const std::vector<ITrack*>* tracks_{nullptr};
};

} // namespace avantgarde
