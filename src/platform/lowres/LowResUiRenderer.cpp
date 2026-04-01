#include "platform/lowres/LowResUiRenderer.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace avantgarde {
namespace {

const char* quantToShort(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return "N";
        case QuantizeMode::Beat: return "B";
        case QuantizeMode::Bar: return "R";
        default: return "?";
    }
}

const char* trackStateToShort(UiTrackState s) noexcept {
    switch (s) {
        case UiTrackState::Empty: return "E";
        case UiTrackState::Stopped: return "S";
        case UiTrackState::Playing: return "P";
        case UiTrackState::Recording: return "R";
        default: return "?";
    }
}

std::string clipShort(const std::string& clipName) {
    if (clipName.empty()) {
        return "-";
    }
    if (clipName.size() <= 18) {
        return clipName;
    }
    return clipName.substr(0, 15) + "...";
}

float speedTo01(float speed) noexcept {
    constexpr float kMin = 0.25f;
    constexpr float kMax = 4.0f;
    const float clamped = std::clamp(speed, kMin, kMax);
    return (clamped - kMin) / (kMax - kMin);
}

} // namespace

LowResUiRenderer::LowResUiRenderer(IDisplay& display) noexcept
    : display_(display) {}

void LowResUiRenderer::render(const UiState& state) {
    display_.beginFrame();
    display_.clear();

    const std::size_t totalTracks = state.tracks.size();
    const std::size_t pageSize = 2;
    const std::size_t totalPages = std::max<std::size_t>(1, (totalTracks + pageSize - 1U) / pageSize);
    const std::size_t activeTrack = (totalTracks == 0)
                                        ? 0U
                                        : std::min<std::size_t>(state.transport.activeTrack, totalTracks - 1U);
    const std::size_t pageIndex = (totalTracks == 0) ? 0U : (activeTrack / pageSize);
    const std::size_t pageStart = pageIndex * pageSize;
    const std::size_t pageEnd = std::min<std::size_t>(pageStart + pageSize, totalTracks);

    char line[128]{};
    std::snprintf(line, sizeof(line), "TRN %s %.1f %u/%u Q%s",
                  state.transport.playing ? "PLAY" : "STOP",
                  state.transport.bpm,
                  static_cast<unsigned>(state.transport.tsNum),
                  static_cast<unsigned>(state.transport.tsDen),
                  quantToShort(state.transport.quant));
    display_.drawText(0, 0, line);

    std::snprintf(line, sizeof(line), "ACT:T%u XR:%llu P:%u/%u",
                  static_cast<unsigned>(activeTrack + 1U),
                  static_cast<unsigned long long>(state.telemetry.xruns),
                  static_cast<unsigned>(pageIndex + 1U),
                  static_cast<unsigned>(totalPages));
    display_.drawText(0, 1, line);

    std::snprintf(line, sizeof(line), "OVF:%c",
                  state.telemetry.rtQueueOverflow ? 'Y' : 'N');
    display_.drawText(0, 2, line);

    const uint16_t barWidth = display_.width() > 24 ? static_cast<uint16_t>(display_.width() - 24U) : 8U;
    for (std::size_t i = pageStart; i < pageEnd; ++i) {
        const auto& tr = state.tracks[i];
        const uint16_t y = static_cast<uint16_t>(4 + (i - pageStart) * 5);

        std::snprintf(line, sizeof(line), "T%u%s %s %s",
                      static_cast<unsigned>(tr.id + 1),
                      tr.id == activeTrack ? "*" : " ",
                      trackStateToShort(tr.state),
                      clipShort(tr.clipName).c_str());
        display_.drawText(0, y, line);

        std::snprintf(line, sizeof(line), "bars:%u fx:%u loop:%c",
                      static_cast<unsigned>(tr.bars),
                      static_cast<unsigned>(tr.fxCount),
                      tr.loop ? 'Y' : 'N');
        display_.drawText(0, static_cast<uint16_t>(y + 1), line);

        std::snprintf(line, sizeof(line), "spd:%1.2f", tr.stretchRatio);
        display_.drawText(0, static_cast<uint16_t>(y + 2), line);
        display_.drawBar(9, static_cast<uint16_t>(y + 2), barWidth, speedTo01(tr.stretchRatio));

        std::snprintf(line, sizeof(line), "gn :%1.2f", tr.gain01);
        display_.drawText(0, static_cast<uint16_t>(y + 3), line);
        display_.drawBar(9, static_cast<uint16_t>(y + 3), barWidth, tr.gain01);
    }

    display_.present();
}

} // namespace avantgarde
