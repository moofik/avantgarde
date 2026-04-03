#include "platform/terminal/AnsiUiRenderer.h"

#include <cstdio>

namespace avantgarde {
namespace {

const char* quantToStr(QuantizeMode q) {
    switch (q) {
        case QuantizeMode::None: return "none";
        case QuantizeMode::Beat: return "beat";
        case QuantizeMode::Bar:  return "bar";
        default: return "?";
    }
}

const char* trackStateToStr(UiTrackState s) {
    switch (s) {
        case UiTrackState::Empty: return "empty";
        case UiTrackState::Stopped: return "stopped";
        case UiTrackState::Playing: return "playing";
        case UiTrackState::Recording: return "recording";
        default: return "?";
    }
}

} // namespace

void AnsiUiRenderer::render(const UiState& state) {
    std::printf("\x1b[2J\x1b[H");
    std::printf("Avantgarde UI v0 (desktop preview)\n");
    std::printf("Transport: %s | BPM %.2f | %u/%u | quant=%s | active=T%u | sample=%llu\n",
                state.transport.playing ? "PLAY" : "STOP",
                state.transport.bpm,
                static_cast<unsigned>(state.transport.tsNum),
                static_cast<unsigned>(state.transport.tsDen),
                quantToStr(state.transport.quant),
                static_cast<unsigned>(state.transport.activeTrack + 1),
                static_cast<unsigned long long>(state.transport.sampleTime));
    std::printf("Telemetry: callbacks=%llu | xruns=%llu | queue_overflow=%s\n\n",
                static_cast<unsigned long long>(state.telemetry.totalCallbacks),
                static_cast<unsigned long long>(state.telemetry.xruns),
                state.telemetry.rtQueueOverflow ? "yes" : "no");

    for (std::size_t i = 0; i < state.tracks.size(); ++i) {
        const auto& tr = state.tracks[i];
        const uint8_t uiTrackIndex = static_cast<uint8_t>(i);
        std::printf("Track %u%s: %-9s | clip=%s | bars=%u | speed=%.3f | gain=%.2f | loop=%s | mute=%s | arm=%s | fx=%u\n",
                    static_cast<unsigned>(uiTrackIndex + 1U),
                    uiTrackIndex == state.transport.activeTrack ? "*" : " ",
                    trackStateToStr(tr.state),
                    tr.clipName.empty() ? "-" : tr.clipName.c_str(),
                    tr.bars,
                    tr.stretchRatio,
                    tr.gain01,
                    tr.loop ? "on" : "off",
                    tr.muted ? "on" : "off",
                    tr.armed ? "on" : "off",
                    static_cast<unsigned>(tr.fxCount));
    }

    std::printf("\nKeys: [j/k focus action] [/? adjust] [o apply] [p play] [s stop] [u unmute] [i mute] [t mute-toggle] [r arm] [F1..F12 softkeys] [q quit]\n");
    std::fflush(stdout);
}

} // namespace avantgarde
