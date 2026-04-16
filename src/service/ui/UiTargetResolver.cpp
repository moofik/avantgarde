#include "service/ui/UiTargetResolver.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "service/ui/UiBindNormalizer.h"

namespace avantgarde {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool parseIndex(std::string_view raw, int32_t& out) noexcept {
    if (raw.empty()) {
        return false;
    }
    uint32_t value = 0;
    for (char ch : raw) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(ch - '0');
        if (value > 65535U) {
            return false;
        }
    }
    out = static_cast<int32_t>(value);
    return true;
}

UiTargetResolution resolveFxTarget(std::string_view key) {
    UiTargetResolution out{};
    out.kind = UiTargetResolution::Kind::FxParam;

    // Канонический write-path.
    static constexpr std::string_view kNewPrefix = "param.fx.selected.";
    if (startsWith(key, kNewPrefix)) {
        const std::string_view tail = key.substr(kNewPrefix.size());
        if (tail == "selected") {
            out.ok = true;
            out.canonical = "param.fx.selected.selected";
            out.paramIndex = -1;
            return out;
        }
        int32_t idx = -1;
        if (!parseIndex(tail, idx)) {
            out.error = "Invalid target param.fx.selected.<index|selected>.";
            return out;
        }
        out.ok = true;
        out.canonical = "param.fx.selected." + std::to_string(idx);
        out.paramIndex = idx;
        return out;
    }

    // Fallback от bind-пути (если target не задан явно в layout):
    // fx.selected.param.<index|selected> -> param.fx.selected.<index|selected>
    static constexpr std::string_view kBindPrefix = "fx.selected.param.";
    if (startsWith(key, kBindPrefix)) {
        const std::string_view tail = key.substr(kBindPrefix.size());
        if (tail == "selected") {
            out.ok = true;
            out.canonical = "param.fx.selected.selected";
            out.paramIndex = -1;
            return out;
        }
        int32_t idx = -1;
        if (!parseIndex(tail, idx)) {
            out.error = "Invalid target fx.selected.param.<index|selected>.";
            return out;
        }
        out.ok = true;
        out.canonical = "param.fx.selected." + std::to_string(idx);
        out.paramIndex = idx;
        return out;
    }

    out.error = "Unsupported FX target.";
    return out;
}

UiTargetResolution resolveTrackTransportTarget(std::string_view key) {
    UiTargetResolution out{};
    out.paramIndex = -1;

    if (key == "param.track.selected.speed" || key == "track.selected.speed") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackSpeed;
        out.canonical = "param.track.selected.speed";
        return out;
    }
    if (key == "param.track.selected.gain" || key == "track.selected.gain") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackGain;
        out.canonical = "param.track.selected.gain";
        return out;
    }
    if (key == "param.track.selected.playback_profile" || key == "track.selected.playback_profile") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackPlaybackProfile;
        out.canonical = "param.track.selected.playback_profile";
        return out;
    }
    if (key == "param.track.selected.start" || key == "track.selected.start") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackTrimStart;
        out.canonical = "param.track.selected.start";
        return out;
    }
    if (key == "param.track.selected.end" || key == "track.selected.end") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackTrimEnd;
        out.canonical = "param.track.selected.end";
        return out;
    }
    if (key == "param.track.selected.tempo_sync" ||
        key == "param.track.selected.tempo.sync" ||
        key == "track.selected.tempo_sync" ||
        key == "track.selected.tempo.sync") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackTempoSync;
        out.canonical = "param.track.selected.tempo_sync";
        return out;
    }
    if (key == "param.track.selected.looper.mode" ||
        key == "param.track.selected.looper_mode" ||
        key == "track.selected.looper.mode" ||
        key == "track.selected.looper_mode") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackLooperMode;
        out.canonical = "param.track.selected.looper_mode";
        return out;
    }
    if (key == "param.track.selected.mute" ||
        key == "param.track.selected.muted" ||
        key == "track.selected.mute" ||
        key == "track.selected.muted") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackMute;
        out.canonical = "param.track.selected.mute";
        return out;
    }
    if (key == "param.track.selected.arm" ||
        key == "param.track.selected.armed" ||
        key == "track.selected.arm" ||
        key == "track.selected.armed") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TrackArm;
        out.canonical = "param.track.selected.arm";
        return out;
    }
    if (key == "param.transport.bpm" || key == "transport.bpm") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TransportBpm;
        out.canonical = "param.transport.bpm";
        return out;
    }
    if (key == "param.transport.quant" ||
        key == "transport.quant" ||
        key == "status.transport.quant") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TransportQuant;
        out.canonical = "param.transport.quant";
        return out;
    }
    if (key == "param.transport.playing" || key == "transport.playing") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TransportPlaying;
        out.canonical = "param.transport.playing";
        return out;
    }
    if (key == "param.transport.metronome" ||
        key == "param.transport.metronome_enabled" ||
        key == "transport.metronome" ||
        key == "transport.metronome_enabled") {
        out.ok = true;
        out.kind = UiTargetResolution::Kind::TransportMetronome;
        out.canonical = "param.transport.metronome_enabled";
        return out;
    }

    out.error = "Unsupported track/transport target.";
    return out;
}

} // namespace

UiTargetResolution UiTargetResolver::resolve(UiScene scene,
                                             UiLayoutNodeType nodeType,
                                             std::string_view rawTarget,
                                             std::string_view fallbackBindCanonical) {
    UiTargetResolution out{};
    const std::string source = UiBindNormalizer::normalize(rawTarget.empty() ? fallbackBindCanonical : rawTarget);
    if (source.empty()) {
        out.error = "Empty target and empty fallback bind.";
        return out;
    }

    if (scene == UiScene::FxEditor &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return resolveFxTarget(source);
    }
    if ((scene == UiScene::Tracks || scene == UiScene::SampleEdit) &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return resolveTrackTransportTarget(source);
    }

    out.ok = true;
    out.canonical = source;
    out.kind = UiTargetResolution::Kind::Unknown;
    out.paramIndex = -1;
    return out;
}

} // namespace avantgarde
