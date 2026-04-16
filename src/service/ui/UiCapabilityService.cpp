#include "service/ui/UiCapabilityService.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "service/ui/UiBindNormalizer.h"
#include "service/ui/UiBindRegistry.h"

namespace avantgarde {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string toLowerAscii(std::string_view value) {
    std::string out(value);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

bool isDigits(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

bool UiCapabilityService::hasSelectedTrack(const UiState& state, const UiNavState& nav) noexcept {
    if (state.tracks.empty()) {
        return false;
    }
    return nav.selectedTrack < state.tracks.size();
}

bool UiCapabilityService::hasSelectedFx(const UiState& state, const UiNavState& nav) noexcept {
    if (!hasSelectedTrack(state, nav)) {
        return false;
    }
    const UiTrackStateView& track = state.tracks[nav.selectedTrack];
    if (track.fxCount == 0U) {
        return false;
    }
    return nav.selectedFx < static_cast<uint16_t>(track.fxCount);
}

bool UiCapabilityService::isBindSupported(std::string_view bindCanonical, std::string& errorOut) {
    if (bindCanonical.empty()) {
        errorOut.clear();
        return true;
    }
    return UiBindRegistry::instance().isCanonicalSupported(bindCanonical, errorOut);
}

bool UiCapabilityService::isBindAvailable(UiScene,
                                          std::string_view bindCanonical,
                                          const UiState& state,
                                          const UiNavState& nav) noexcept {
    const std::string key = UiBindNormalizer::normalize(bindCanonical);
    if (key.empty()) {
        return true;
    }

    if (startsWith(key, "status.") ||
        startsWith(key, "transport.") ||
        startsWith(key, "ui.")) {
        return true;
    }
    if (startsWith(key, "track.selected.") ||
        startsWith(key, "param.track.selected.")) {
        return hasSelectedTrack(state, nav);
    }
    if (startsWith(key, "fx.anim.") ||
        startsWith(key, "fx.selected.param.") ||
        startsWith(key, "param.fx.selected.") ||
        startsWith(key, "fx.selected.")) {
        return hasSelectedFx(state, nav);
    }
    // Для новых namespace-ключей считаем bind доступным по умолчанию:
    // это не блокирует расширение схемы.
    return true;
}

bool UiCapabilityService::isTargetSupported(std::string_view targetCanonical, std::string& errorOut) noexcept {
    const std::string key = UiBindNormalizer::normalize(targetCanonical);
    if (key.empty()) {
        errorOut.clear();
        return true;
    }

    if (startsWith(key, "param.track.selected.")) {
        const std::string_view field = std::string_view(key).substr(std::string_view("param.track.selected.").size());
        if (field == "speed" ||
            field == "gain" ||
            field == "playback_profile" ||
            field == "start" ||
            field == "end" ||
            field == "tempo_sync" ||
            field == "tempo.sync" ||
            field == "looper_mode" ||
            field == "looper.mode" ||
            field == "mute" ||
            field == "muted" ||
            field == "arm" ||
            field == "armed") {
            errorOut.clear();
            return true;
        }
        errorOut = "Unsupported track target field: '" + std::string(field) + "'";
        return false;
    }

    if (startsWith(key, "param.transport.")) {
        const std::string_view field = std::string_view(key).substr(std::string_view("param.transport.").size());
        if (field == "bpm" ||
            field == "quant" ||
            field == "playing" ||
            field == "metronome" ||
            field == "metronome_enabled") {
            errorOut.clear();
            return true;
        }
        errorOut = "Unsupported transport target field: '" + std::string(field) + "'";
        return false;
    }

    if (startsWith(key, "param.fx.selected.")) {
        const std::string_view tail = std::string_view(key).substr(std::string_view("param.fx.selected.").size());
        if (tail == "selected" || isDigits(tail)) {
            errorOut.clear();
            return true;
        }
        errorOut = "FX target expects <index|selected>.";
        return false;
    }

    errorOut = "Unsupported target namespace.";
    return false;
}

bool UiCapabilityService::isTargetActive(UiScene,
                                         std::string_view targetCanonical,
                                         const UiState& state,
                                         const UiNavState& nav) noexcept {
    const std::string key = UiBindNormalizer::normalize(targetCanonical);
    if (key.empty()) {
        return true;
    }
    if (startsWith(key, "param.track.selected.")) {
        return hasSelectedTrack(state, nav);
    }
    if (startsWith(key, "param.fx.selected.")) {
        return hasSelectedFx(state, nav);
    }
    if (startsWith(key, "param.transport.")) {
        return true;
    }
    return false;
}

UiCapabilityState UiCapabilityService::resolve(UiScene scene,
                                               std::string_view bindCanonical,
                                               std::string_view targetCanonical,
                                               const UiState& state,
                                               const UiNavState& nav) {
    UiCapabilityState out{};
    out.hasSelectedTrack = hasSelectedTrack(state, nav);
    out.hasSelectedFx = hasSelectedFx(state, nav);

    std::string error{};
    out.bindSupported = isBindSupported(bindCanonical, error);
    if (!out.bindSupported) {
        out.reason = error;
    }
    out.bindAvailable = isBindAvailable(scene, bindCanonical, state, nav);
    if (out.reason.empty() && !out.bindAvailable) {
        out.reason = "Bind is not available in current context.";
    }

    error.clear();
    out.targetSupported = isTargetSupported(targetCanonical, error);
    if (out.reason.empty() && !out.targetSupported) {
        out.reason = error;
    }
    out.targetActive = isTargetActive(scene, targetCanonical, state, nav);
    if (out.reason.empty() && !out.targetActive) {
        out.reason = "Target is not active in current context.";
    }
    return out;
}

bool UiCapabilityService::evaluateCondition(UiScene scene,
                                            const UiLayoutNode& node,
                                            std::string_view condition,
                                            const UiState& state,
                                            const UiNavState& nav,
                                            const UiPreparedParams& params,
                                            bool defaultValue) noexcept {
    std::string raw = toLowerAscii(condition);
    if (raw.empty()) {
        return defaultValue;
    }

    bool invert = false;
    if (!raw.empty() && raw.front() == '!') {
        invert = true;
        raw.erase(raw.begin());
    }
    if (raw.empty()) {
        return defaultValue;
    }

    auto finalize = [invert](bool value) noexcept {
        return invert ? !value : value;
    };

    if (raw == "true") {
        return finalize(true);
    }
    if (raw == "false") {
        return finalize(false);
    }

    const UiCapabilityState cap = resolve(scene, node.bind, node.target, state, nav);
    if (raw == "track.selected.exists" || raw == "selected.track.exists") {
        return finalize(cap.hasSelectedTrack);
    }
    if (raw == "fx.selected.exists" || raw == "selected.fx.exists") {
        return finalize(cap.hasSelectedFx);
    }
    if (raw == "bind.supported") {
        return finalize(cap.bindSupported);
    }
    if (raw == "bind.available") {
        return finalize(cap.bindAvailable);
    }
    if (raw == "target.supported") {
        return finalize(cap.targetSupported);
    }
    if (raw == "target.active") {
        return finalize(cap.targetActive);
    }

    if (auto value = params.findFlag(raw); value.has_value()) {
        return finalize(*value);
    }
    if (auto value = params.findFlag(condition); value.has_value()) {
        return finalize(*value);
    }

    // Неизвестное условие не должно внезапно скрывать UI.
    return finalize(defaultValue);
}

} // namespace avantgarde
