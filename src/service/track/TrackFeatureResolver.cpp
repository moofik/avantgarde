#include "service/track/TrackFeatureResolver.h"

namespace avantgarde {

void TrackFeatureResolver::bind(const std::vector<ITrack*>* tracks) noexcept {
    tracks_ = tracks;
}

ITrack* TrackFeatureResolver::track(uint8_t trackId) const noexcept {
    if (!tracks_ || static_cast<std::size_t>(trackId) >= tracks_->size()) {
        return nullptr;
    }
    return (*tracks_)[trackId];
}

IClipTrack* TrackFeatureResolver::clipTrack(uint8_t trackId) const noexcept {
    ITrack* tr = track(trackId);
    return dynamic_cast<IClipTrack*>(tr);
}

} // namespace avantgarde
