#include "service/ui/widgets/TracksWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "service/ui/UiCapabilityService.h"
#include "service/ui/UiBindResolver.h"
#include "service/ui/UiTargetResolver.h"
#include "service/ui/layout/UiNodeComponentComposer.h"

namespace avantgarde {
namespace {

constexpr std::size_t kTracksPerPage = 1;

uint8_t clampTrackIndex(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    return (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
}

uint8_t wrapPrevTrack(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    return (track == 0U) ? static_cast<uint8_t>(totalTracks - 1U) : static_cast<uint8_t>(track - 1U);
}

uint8_t wrapNextTrack(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t last = static_cast<uint8_t>(totalTracks - 1U);
    return (track >= last) ? 0U : static_cast<uint8_t>(track + 1U);
}

uint16_t pageForTrack(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t safe = clampTrackIndex(track, totalTracks);
    return static_cast<uint16_t>(safe / kTracksPerPage);
}

float quantToValue(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return 0.0f;
        case QuantizeMode::Beat: return 1.0f;
        case QuantizeMode::Bar: return 2.0f;
        default: return 2.0f;
    }
}

QuantizeMode valueToQuant(float v) noexcept {
    const int idx = static_cast<int>(std::lround(v));
    switch (idx) {
        case 0: return QuantizeMode::None;
        case 1: return QuantizeMode::Beat;
        case 2:
        default: return QuantizeMode::Bar;
    }
}

const char* quantToStr(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return "NONE";
        case QuantizeMode::Beat: return "BEAT";
        case QuantizeMode::Bar: return "BAR";
        default: return "UNK";
    }
}

const char* onOff(bool v) noexcept {
    return v ? "ON" : "OFF";
}

const char* playbackProfileToStr(UiTrackPlaybackProfile profile) noexcept {
    switch (profile) {
        case UiTrackPlaybackProfile::Pattern: return "PATTERN";
        case UiTrackPlaybackProfile::PatternOnce: return "PATTERN ONCE";
        case UiTrackPlaybackProfile::Loop: return "LOOP";
        case UiTrackPlaybackProfile::OneShot:
        default:
            return "ONESHOT";
    }
}

uint8_t playbackProfileToIndex(UiTrackPlaybackProfile profile) noexcept {
    return static_cast<uint8_t>(profile);
}

UiTrackPlaybackProfile indexToPlaybackProfile(uint8_t index) noexcept {
    switch (index) {
        case 0: return UiTrackPlaybackProfile::Pattern;
        case 1: return UiTrackPlaybackProfile::PatternOnce;
        case 2: return UiTrackPlaybackProfile::Loop;
        case 3:
        default:
            return UiTrackPlaybackProfile::OneShot;
    }
}

std::string clipShort(const std::string& clipName, std::size_t maxLen) {
    if (clipName.empty()) {
        return "-";
    }
    if (clipName.size() <= maxLen) {
        return clipName;
    }
    if (maxLen <= 3U) {
        return clipName.substr(0, maxLen);
    }
    return clipName.substr(0, maxLen - 3U) + "...";
}

bool intentFromTarget(std::string_view targetCanonical,
                      uint8_t selectedTrack,
                      float value,
                      UiIntent& out) {
    if (targetCanonical == "param.track.selected.speed") {
        out.type = UiIntentType::SetTrackSpeed;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.gain") {
        out.type = UiIntentType::SetTrackGain;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.looper_mode") {
        out.type = UiIntentType::SetTrackLooperMode;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.playback_profile") {
        out.type = UiIntentType::SetTrackPlaybackProfile;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.mute") {
        out.type = UiIntentType::SetTrackMuted;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.arm") {
        out.type = UiIntentType::SetTrackArmed;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.transport.bpm") {
        out.type = UiIntentType::SetTransportBpm;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.transport.quant") {
        out.type = UiIntentType::SetTransportQuant;
        out.value = value;
        return true;
    }
    return false;
}

float speedTo01(float speed) noexcept {
    constexpr float kMin = 0.25f;
    constexpr float kMax = 4.0f;
    const float clamped = std::clamp(speed, kMin, kMax);
    return (clamped - kMin) / (kMax - kMin);
}

const char* trackStateToStr(UiTrackState s) noexcept {
    switch (s) {
        case UiTrackState::Empty: return "EMPTY";
        case UiTrackState::Stopped: return "STOP ";
        case UiTrackState::Playing: return "PLAY ";
        case UiTrackState::Recording: return "REC  ";
        default: return "UNK  ";
    }
}

} // namespace

TracksWidget::TracksWidget() noexcept = default;

TracksWidget::TracksWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth),
      headerTitle_(options.headerTitle),
      bpmStep_(options.bpmStep > 0.0f ? options.bpmStep : 1.0f) {
    if (options.layoutTemplate.has_value()) {
        layoutTemplate_ = options.layoutTemplate;
        buildLayoutModel_(*options.layoutTemplate);
    }
}

const char* TracksWidget::id() const noexcept {
    return "tracks";
}

bool TracksWidget::buildPreparedLayout(UiPreparedLayout& out,
                                       const UiState& rtState,
                                       const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 34U);
    UiPreparedParams preparedParams = buildPreparedLayoutParams_(rtState, navState);
    uint16_t frameHeightHint = 12U;
    if (auto h = preparedParams.findInteger("frame.heightHint"); h.has_value()) {
        frameHeightHint = static_cast<uint16_t>(std::clamp<int32_t>(*h, 4, 4096));
    }

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("tracks")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(frameHeightHint);

    UiNodeComponentComposer::compose(UiScene::Tracks, *layoutTemplate_, rtState, navState, preparedParams, builder);

    out = std::move(builder).build();
    return true;
}

UiPreparedParams TracksWidget::buildPreparedLayoutParams_(const UiState& rtState,
                                                          const UiNavState& navState) const {
    UiPreparedParams params{};

    const std::size_t totalTracks = rtState.tracks.size();
    const std::size_t totalPages = std::max<std::size_t>(1U, (totalTracks + kTracksPerPage - 1U) / kTracksPerPage);
    const uint8_t selectedTrack = clampTrackIndex(navState.selectedTrack, totalTracks);
    const uint8_t activeTrack = (totalTracks == 0U)
                                    ? 0U
                                    : clampTrackIndex(rtState.transport.activeTrack, totalTracks);
    // В режиме "1 page = 1 active track" всегда показываем страницу активного трека.
    const std::size_t pageIndex = (totalTracks == 0U)
                                      ? 0U
                                      : std::min<std::size_t>(activeTrack, totalPages - 1U);
    const std::size_t pageStart = pageIndex * kTracksPerPage;
    const std::size_t pageEnd = std::min<std::size_t>(pageStart + kTracksPerPage, totalTracks);

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 34U);
    const std::size_t inner = static_cast<std::size_t>(frameWidth - 2U);
    const std::size_t clipWidth = (inner > 38U) ? (inner - 38U) : 16U;

    std::string transportLine{};
    {
        char line[256]{};
        std::snprintf(line, sizeof(line), " TRN:%s BPM:%5.1f TS:%u/%u Q:%s MET:%c REC:%c OVF:%c ",
                      rtState.transport.playing ? "PLAY" : "STOP",
                      rtState.transport.bpm,
                      static_cast<unsigned>(rtState.transport.tsNum),
                      static_cast<unsigned>(rtState.transport.tsDen),
                      quantToStr(rtState.transport.quant),
                      rtState.transport.metronomeEnabled ? 'Y' : 'N',
                      rtState.transport.recordEnabled ? 'Y' : 'N',
                      rtState.telemetry.rtQueueOverflow ? 'Y' : 'N');
        transportLine = line;
    }

    std::string activeLine{};
    {
        char line[256]{};
        const bool patternPending = rtState.pattern.pendingId != kInvalidPatternId;
        const char* armedMark = rtState.pattern.armed ? "*" : "";
        std::string pendingPart{};
        if (patternPending) {
            pendingPart = "->" + std::to_string(static_cast<unsigned>(rtState.pattern.pendingId));
        }
        std::snprintf(line, sizeof(line), " ACTIVE:T%u XRUN:%llu PG:%u/%u PAT:%u%s%s ",
                      static_cast<unsigned>(activeTrack + 1U),
                      static_cast<unsigned long long>(rtState.telemetry.xruns),
                      static_cast<unsigned>(pageIndex + 1U),
                      static_cast<unsigned>(totalPages),
                      static_cast<unsigned>(rtState.pattern.activeId == kInvalidPatternId ? 0U : rtState.pattern.activeId),
                      pendingPart.c_str(),
                      armedMark);
        activeLine = line;
    }

    std::vector<std::string> trackRows{};
    int32_t selectedRow = -1;
    if (totalTracks == 0U) {
        trackRows.push_back(" no tracks configured ");
        selectedRow = 0;
    } else {
        char line[256]{};
        for (std::size_t i = pageStart; i < pageEnd; ++i) {
            const UiTrackStateView& tr = rtState.tracks[i];
            const uint8_t uiTrackIndex = static_cast<uint8_t>(i);
            const bool isActive = (uiTrackIndex == activeTrack);
            const bool isSelected = (uiTrackIndex == selectedTrack);
            if (isSelected && selectedRow < 0) {
                selectedRow = static_cast<int32_t>(trackRows.size());
            }

            std::snprintf(line, sizeof(line), " %s T%u %-5s clip:%s",
                          isActive ? "▶" : " ",
                          static_cast<unsigned>(uiTrackIndex + 1U),
                          trackStateToStr(tr.state),
                          clipShort(tr.clipName, clipWidth).c_str());
            trackRows.emplace_back(line);

            const uint32_t safeBars = std::max<uint32_t>(1U, tr.bars);
            uint32_t currentBar = 0U;
            if (!tr.clipName.empty()) {
                const float ph01 = std::clamp(tr.playhead01, 0.0f, 0.999999f);
                currentBar = std::min<uint32_t>(
                    safeBars,
                    static_cast<uint32_t>(std::floor(ph01 * static_cast<float>(safeBars))) + 1U);
            }
            std::snprintf(line, sizeof(line), "   bars:%u current bar:%u",
                          static_cast<unsigned>(safeBars),
                          static_cast<unsigned>(currentBar));
            trackRows.emplace_back(line);

            std::snprintf(line, sizeof(line), "   mode:%s",
                          playbackProfileToStr(tr.playbackProfile));
            trackRows.emplace_back(line);

            if (i + 1U < pageEnd) {
                trackRows.emplace_back(" ");
            }
        }
        if (selectedRow < 0) {
            selectedRow = 0;
        }
    }

    const std::string title = !layout_.title.empty() ? layout_.title : headerTitle_;
    const std::string actionLine = buildActionStatusLine_(rtState, navState);
    std::string keysLine1{};
    std::string keysLine2{};
    if (!layout_.keysHint.empty()) {
        const std::size_t sep = layout_.keysHint.find('|');
        if (sep != std::string::npos) {
            keysLine1 = layout_.keysHint.substr(0, sep);
            keysLine2 = layout_.keysHint.substr(sep + 1U);
        } else {
            keysLine1 = layout_.keysHint;
        }
    } else {
        keysLine1 = " keys [j/k focus] [/? adjust] [o apply] [m metronome] [v rec] [n seq] [shift+n pattern]";
        keysLine2 = "      [F2 undo] [F9 redo] [F4 manager] [F10 detect BPM] [F11/F12 pages] [q] [e/r/t/y snap]";
    }
    const std::string keys = keysLine2.empty() ? keysLine1 : (keysLine1 + " " + keysLine2);

    params.text["status.scene.title"] = title;
    params.text["status.transport"] = transportLine;
    params.text["status.transport.active"] = activeLine;
    params.text["status.action"] = actionLine;
    params.text["status.keys"] = keys;
    params.text["status.keys.1"] = keysLine1;
    params.text["status.keys.2"] = keysLine2;
    params.text["header_title"] = title;
    params.text["transport_line"] = transportLine;
    params.text["active_line"] = activeLine;
    params.text["action_status"] = actionLine;
    params.text["keys_hint"] = keys;
    params.text["keys_hint_line_1"] = keysLine1;
    params.text["keys_hint_line_2"] = keysLine2;

    params.rows["tracks_body"] = trackRows;
    params.integer["tracks_body.selected"] = selectedRow;
    // В режиме 1-track/page у сцены есть фиксированный "низ":
    // separator + action + keys + track knobs + anim-slot.
    // При слишком маленьком hint нижние строки визуально подрезаются.
    const std::size_t minInnerRows = 18U;
    const std::size_t dynamicRows = 14U + std::max<std::size_t>(1U, trackRows.size());
    params.integer["frame.heightHint"] = static_cast<int32_t>(std::max(minInnerRows, dynamicRows));

    const UiTrackStateView* selectedTrackView = nullptr;
    if (totalTracks > 0U) {
        selectedTrackView = &rtState.tracks[selectedTrack];
    }
    const float selectedSpeed01 = selectedTrackView ? speedTo01(selectedTrackView->stretchRatio) : 0.0f;
    const float selectedGain01 = selectedTrackView ? std::clamp(selectedTrackView->gain01, 0.0f, 1.0f) : 0.0f;
    const bool selectedMuted = selectedTrackView && selectedTrackView->muted;
    const bool selectedArmed = selectedTrackView && selectedTrackView->armed;
    const bool selectedLoop = selectedTrackView && selectedTrackView->loop;
    const bool selectedRecordActive =
        selectedTrackView && selectedTrackView->armed && rtState.transport.recordEnabled && rtState.transport.playing;
    const uint8_t selectedFxCount = selectedTrackView ? selectedTrackView->fxCount : 0U;
    bool selectedFxEnabledAny = false;
    if (selectedTrackView && selectedFxCount > 0U) {
        const std::size_t count = static_cast<std::size_t>(selectedFxCount);
        if (selectedTrackView->fxEnabled.empty()) {
            // Legacy/переходный случай: если статусы слотов не синхронизированы,
            // считаем добавленный FX активным по умолчанию.
            selectedFxEnabledAny = true;
        } else {
            const std::size_t known = std::min<std::size_t>(count, selectedTrackView->fxEnabled.size());
            for (std::size_t i = 0; i < known; ++i) {
                if (selectedTrackView->fxEnabled[i] != 0U) {
                    selectedFxEnabledAny = true;
                    break;
                }
            }
            // Если есть слоты без явного флага enabled, трактуем их как активные.
            if (!selectedFxEnabledAny && count > known) {
                selectedFxEnabledAny = true;
            }
        }
    }
    const char* selectedPlayState = selectedTrackView ? trackStateToStr(selectedTrackView->state) : "EMPTY";
    const char* selectedModeText = selectedTrackView
                                       ? playbackProfileToStr(selectedTrackView->playbackProfile)
                                       : "-";
    constexpr float kMinBpm = 40.0f;
    constexpr float kMaxBpm = 220.0f;
    const float bpm01 = std::clamp((rtState.transport.bpm - kMinBpm) / (kMaxBpm - kMinBpm), 0.0f, 1.0f);
    const float trackPlayhead01 =
        selectedTrackView ? std::clamp(selectedTrackView->playhead01, 0.0f, 1.0f) : 0.0f;

    params.number["track.selected.speed"] = selectedSpeed01;
    params.number["track.selected.gain"] = selectedGain01;
    const uint8_t selectedProfileIndex =
        selectedTrackView ? playbackProfileToIndex(selectedTrackView->playbackProfile) : 2U;
    params.number["track.selected.playback_profile"] = static_cast<float>(selectedProfileIndex) / 3.0f;
    params.number["track.selected.mute"] = selectedMuted ? 1.0f : 0.0f;
    params.number["track.selected.arm"] = selectedArmed ? 1.0f : 0.0f;
    params.text["track.selected.mode"] = std::string("MODE: ") + selectedModeText;
    params.integer["track.selected.playback_profile.selectedIndex"] = selectedProfileIndex;
    params.text["track.selected.playback_profile.label"] = "MODE";
    params.text["track.selected.icon.loop"] = selectedLoop ? "LP:Y" : "LP:N";
    params.text["track.selected.icon.mute"] = selectedMuted ? "MT:Y" : "MT:N";
    params.text["track.selected.icon.arm"] =
        selectedRecordActive ? "● REC" : (selectedArmed ? "● ARM" : "○ ARM");
    params.text["track.selected.icon.play"] = std::string("ST:") + selectedPlayState;
    params.text["track.selected.icon.fx"] = std::string("FX:") + std::to_string(static_cast<unsigned>(selectedFxCount));
    params.flag["track.selected.fx.enabled"] = selectedFxEnabledAny;
    params.flag["track.selected.recording.armed"] = selectedRecordActive;
    params.flag["track.selected.arm.enabled"] = selectedArmed;
    params.text["track.selected.fx.icon.path"] = "images/icon.png";
    params.number["transport.bpm"] = bpm01;
    params.number["track.selected.playhead"] = trackPlayhead01;
    params.text["track.selected.playhead.label"] = "";
    params.text["track.selected.playhead.animKey"] = "track.selected.playhead";
    params.number["fx.anim.current"] = selectedGain01;
    params.number["current"] = selectedGain01;
    params.text["fx.anim.current.label"] = "";
    params.text["fx.anim.current.animKey"] = "fx.anim.current";
    params.text["current.label"] = "";
    params.text["current.animKey"] = "fx.anim.current";

    UiAction::Id selectedActionId = UiAction::Id::None;
    {
        const UiActionCatalog actions = queryAvailableActions(rtState, navState);
        if (!actions.actions.empty()) {
            const uint16_t idx = std::min<uint16_t>(actions.currentIndex, static_cast<uint16_t>(actions.actions.size() - 1U));
            selectedActionId = actions.actions[idx].def.id;
        }
    }
    params.flag["track.selected.playback_profile.selected"] = (selectedActionId == UiAction::Id::SceneTrackPlaybackProfile);
    params.flag["transport.bpm.selected"] = (selectedActionId == UiAction::Id::SceneTempoBpm);

    return params;
}

WidgetOutput TracksWidget::onGesture(UiGesture, const UiState&, UiNavState&) {
    // У этого экрана пока нет собственной scene-local логики ввода.
    // Все действия треков обрабатываются на application/control-слое.
    return {};
}

UiActionCatalog TracksWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};
    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t selectedTrack = clampTrackIndex(navState.selectedTrack, totalTracks);
    const UiCapabilityState trackProfileCap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "track.selected.playback_profile",
        layout_.targetTrackPlaybackProfile,
        rtState,
        navState);
    const UiCapabilityState trackMuteCap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "track.selected.mute",
        layout_.targetTrackMute,
        rtState,
        navState);
    const UiCapabilityState trackArmCap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "track.selected.arm",
        layout_.targetTrackArm,
        rtState,
        navState);
    const UiCapabilityState transportQuantCap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "transport.quant",
        layout_.targetTransportQuant,
        rtState,
        navState);
    const UiCapabilityState transportBpmCap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "transport.bpm",
        layout_.targetTransportBpm,
        rtState,
        navState);
    const bool hasTrack = trackProfileCap.hasSelectedTrack;

    auto pushAction = [&out](UiAction action) {
        out.actions.push_back(std::move(action));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "Track Select";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, totalTracks));
        a.def.step = 1.0f;
        a.state.enabled = hasTrack;
        a.state.value = static_cast<float>(selectedTrack + 1U);
        pushAction(std::move(a));
    }
    {
        const bool trackValid = hasTrack;
        const uint8_t profileIndex =
            trackValid ? playbackProfileToIndex(rtState.tracks[selectedTrack].playbackProfile) : 2U;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackPlaybackProfile;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "Track Mode";
        a.def.minValue = 0.0f;
        a.def.maxValue = 3.0f;
        a.def.step = 1.0f;
        a.state.enabled = trackValid && trackProfileCap.targetActive;
        a.state.value = static_cast<float>(profileIndex);
        pushAction(std::move(a));
    }
    {
        const bool trackValid = hasTrack;
        const bool muted = trackValid ? rtState.tracks[selectedTrack].muted : false;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackMute;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Track Mute";
        a.state.enabled = trackValid && trackMuteCap.targetActive;
        a.state.value = muted ? 1.0f : 0.0f;
        pushAction(std::move(a));
    }
    {
        const bool trackValid = hasTrack;
        const bool armed = trackValid ? rtState.tracks[selectedTrack].armed : false;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackArm;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Track Arm";
        a.state.enabled = trackValid && trackArmCap.targetActive;
        a.state.value = armed ? 1.0f : 0.0f;
        pushAction(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneQuantize;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "Quantization";
        a.def.minValue = 0.0f;
        a.def.maxValue = 2.0f;
        a.def.step = 1.0f;
        a.state.enabled = transportQuantCap.targetActive;
        a.state.value = quantToValue(rtState.transport.quant);
        pushAction(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTempoBpm;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Tempo BPM";
        a.def.minValue = 20.0f;
        a.def.maxValue = 300.0f;
        a.def.step = bpmStep_;
        a.state.enabled = transportBpmCap.targetActive;
        a.state.value = rtState.transport.bpm;
        pushAction(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::ScenePatternPrev;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Pattern Prev";
        a.state.enabled = (rtState.pattern.bankSize > 1U);
        pushAction(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::ScenePatternNext;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Pattern Next";
        a.state.enabled = (rtState.pattern.bankSize > 1U);
        pushAction(std::move(a));
    }
    {
        const bool trackValid = hasTrack &&
                                !rtState.tracks[selectedTrack].clipPath.empty();
        UiAction a{};
        a.def.id = UiAction::Id::SceneDetectProjectBpm;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Detect BPM";
        a.state.enabled = trackValid;
        pushAction(std::move(a));
    }
    {
        const bool trackValid = hasTrack;
        UiAction a{};
        a.def.id = UiAction::Id::SceneOpenFxList;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Add FX";
        a.state.enabled = trackValid;
        pushAction(std::move(a));
    }
    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput TracksWidget::onAction(UiAction& action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;
    if (!action.state.enabled) {
        return out;
    }

    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t selectedTrack = clampTrackIndex(navState.selectedTrack, totalTracks);

    // Главный принцип для scene-слоя:
    //   UiAction + navState (+rtState для текущих значений) => UiIntent.
    // То есть именно виджет решает, какой intent должен выйти наружу.
    auto pushIntentToWidgetOutput = [&out](UiIntent intent) {
        out.intents.push_back(std::move(intent));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneTrackSelect: {
            if (totalTracks == 0) break;
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.selectedTrack = wrapPrevTrack(selectedTrack, totalTracks);
            } else if (action.op == UiAction::Op::AdjustNext) {
                navState.selectedTrack = wrapNextTrack(selectedTrack, totalTracks);
            } else if (action.op == UiAction::Op::Apply ||
                       action.op == UiAction::Op::Press) {
                UiIntent open{};
                open.type = UiIntentType::OpenScene;
                open.scene = UiScene::TrackContext;
                open.resetCursor = true;
                open.resetScroll = true;
                open.resetSceneActionIndex = true;
                pushIntentToWidgetOutput(std::move(open));
                break;
            } else {
                break;
            }
            navState.trackPage = pageForTrack(navState.selectedTrack, totalTracks);
            UiIntent it{};
            it.type = UiIntentType::SetActiveTrack;
            it.track = navState.selectedTrack;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackPlaybackProfile: {
            if (totalTracks == 0) break;
            if (!UiCapabilityService::isTargetActive(
                    UiScene::Tracks, layout_.targetTrackPlaybackProfile, rtState, navState)) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            int next = static_cast<int>(playbackProfileToIndex(rtState.tracks[selectedTrack].playbackProfile));
            if (action.op == UiAction::Op::AdjustPrev) {
                next = (next + 3) % 4;
            } else {
                next = (next + 1) % 4;
            }
            const float nextValue = static_cast<float>(next);
            if (!intentFromTarget(layout_.targetTrackPlaybackProfile, selectedTrack, nextValue, it)) {
                it.type = UiIntentType::SetTrackPlaybackProfile;
                it.track = selectedTrack;
                it.value = nextValue;
            }
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackMute: {
            if (totalTracks == 0) break;
            if (!UiCapabilityService::isTargetActive(UiScene::Tracks, layout_.targetTrackMute, rtState, navState)) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            const float nextValue = rtState.tracks[selectedTrack].muted ? 0.0f : 1.0f;
            if (!intentFromTarget(layout_.targetTrackMute, selectedTrack, nextValue, it)) {
                it.type = UiIntentType::SetTrackMuted;
                it.track = selectedTrack;
                it.value = nextValue;
            }
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackArm: {
            if (totalTracks == 0) break;
            if (!UiCapabilityService::isTargetActive(UiScene::Tracks, layout_.targetTrackArm, rtState, navState)) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            const float nextValue = rtState.tracks[selectedTrack].armed ? 0.0f : 1.0f;
            if (!intentFromTarget(layout_.targetTrackArm, selectedTrack, nextValue, it)) {
                it.type = UiIntentType::SetTrackArmed;
                it.track = selectedTrack;
                it.value = nextValue;
            }
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneQuantize: {
            if (!UiCapabilityService::isTargetActive(UiScene::Tracks, layout_.targetTransportQuant, rtState, navState)) {
                break;
            }
            if (action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Apply) {
                break;
            }
            int v = static_cast<int>(std::lround(quantToValue(rtState.transport.quant)));
            if (action.op == UiAction::Op::AdjustPrev) {
                v = (v + 2) % 3;
            } else {
                v = (v + 1) % 3;
            }
            UiIntent it{};
            const float next = static_cast<float>(v);
            if (!intentFromTarget(layout_.targetTransportQuant, selectedTrack, next, it)) {
                it.type = UiIntentType::SetTransportQuant;
                it.value = next;
            }
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTempoBpm: {
            if (!UiCapabilityService::isTargetActive(UiScene::Tracks, layout_.targetTransportBpm, rtState, navState)) {
                break;
            }
            if (action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float step = (action.def.step > 0.0f) ? action.def.step : bpmStep_;
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float next = std::clamp(rtState.transport.bpm + dir * step, 20.0f, 300.0f);
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTransportBpm, selectedTrack, next, it)) {
                it.type = UiIntentType::SetTransportBpm;
                it.value = next;
            }
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::ScenePatternPrev: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustPrev) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SwitchPatternPrev;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::ScenePatternNext: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SwitchPatternNext;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneDetectProjectBpm: {
            if (totalTracks == 0) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            if (rtState.tracks[selectedTrack].clipPath.empty()) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::DetectProjectBpmFromTrack;
            it.track = selectedTrack;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneOpenFxList: {
            if (totalTracks == 0) break;
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::FxList;
            it.resetSceneActionIndex = true;
            it.resetSelectedFx = true;
            it.closeFxAddPopup = true;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::None:
        case UiAction::Id::GlobalPlayStop:
        case UiAction::Id::GlobalUndo:
        case UiAction::Id::GlobalBack:
        case UiAction::Id::GlobalPagePrev:
        case UiAction::Id::GlobalPageNext:
        case UiAction::Id::GlobalMasterVolume:
        case UiAction::Id::SceneAddFx:
        case UiAction::Id::SceneAddReverb:
        case UiAction::Id::SceneOpenManager:
        case UiAction::Id::SceneFxSlotSelect:
        case UiAction::Id::SceneFxEnabled:
        case UiAction::Id::SceneFxOpenEditor:
        case UiAction::Id::SceneTrackMenuLoadSample:
        case UiAction::Id::SceneTrackMenuClear:
        case UiAction::Id::SceneTrackMenuFxList:
        case UiAction::Id::SceneFxParamSelect:
        case UiAction::Id::SceneFxParamValue:
        case UiAction::Id::SceneFxBack:
        default:
            out.handled = false;
            break;
    }

    return out;
}

std::string TracksWidget::buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const {
    const UiActionCatalog catalog = queryAvailableActions(rtState, navState);
    if (catalog.actions.empty()) {
        return " action: - ";
    }
    const std::size_t idx = std::min<std::size_t>(catalog.currentIndex, catalog.actions.size() - 1U);
    const UiAction& a = catalog.actions[idx];

    char buf[192]{};
    switch (a.def.id) {
        case UiAction::Id::SceneTrackSelect:
            std::snprintf(buf, sizeof(buf), " action:%s = T%u ", a.def.label.c_str(), static_cast<unsigned>(std::lround(a.state.value)));
            break;
        case UiAction::Id::SceneTrackPlaybackProfile: {
            const UiTrackPlaybackProfile p = indexToPlaybackProfile(
                static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(a.state.value)), 0, 3)));
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), playbackProfileToStr(p));
        }
            break;
        case UiAction::Id::SceneTrackMute:
        case UiAction::Id::SceneTrackArm:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), onOff(a.state.value >= 0.5f));
            break;
        case UiAction::Id::SceneTrackSpeed:
            std::snprintf(buf, sizeof(buf), " action:%s = %.2f ", a.def.label.c_str(), a.state.value);
            break;
        case UiAction::Id::SceneTrackGain:
            std::snprintf(buf, sizeof(buf), " action:%s = %.2f ", a.def.label.c_str(), a.state.value);
            break;
        case UiAction::Id::SceneQuantize:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), quantToStr(valueToQuant(a.state.value)));
            break;
        case UiAction::Id::SceneTempoBpm:
            std::snprintf(buf, sizeof(buf), " action:%s = %.1f ", a.def.label.c_str(), a.state.value);
            break;
        case UiAction::Id::ScenePatternPrev:
        case UiAction::Id::ScenePatternNext:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        case UiAction::Id::SceneDetectProjectBpm:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        case UiAction::Id::SceneOpenFxList:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        default:
            std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
            break;
    }
    return std::string(buf);
}

void TracksWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "tracks") {
        return;
    }

    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (layout_.title.empty() &&
            node.type == UiLayoutNodeType::StatusBar &&
            !node.text.empty()) {
            layout_.title = node.text;
        }
        // Кастомную строку подсказок удобно задавать через text-узел
        // с id="keys_hint".
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout_.keysHint = node.text;
        }
        if (node.type == UiLayoutNodeType::Knob ||
            node.type == UiLayoutNodeType::Switch) {
            const UiBindResolution bind = UiBindResolver::resolve(UiScene::Tracks, node.type, node.bind);
            if (!bind.ok) {
                return;
            }
            const UiTargetResolution target = UiTargetResolver::resolve(
                UiScene::Tracks,
                node.type,
                node.target,
                bind.canonical);
            if (!target.ok) {
                return;
            }
            if (bind.canonical == "track.selected.speed") {
                layout_.targetTrackSpeed = target.canonical;
            } else if (bind.canonical == "track.selected.gain") {
                layout_.targetTrackGain = target.canonical;
            } else if (bind.canonical == "track.selected.playback_profile" ||
                       bind.canonical == "track.selected.looper_mode" ||
                       bind.canonical == "track.selected.looper.mode") {
                layout_.targetTrackPlaybackProfile = target.canonical;
            } else if (bind.canonical == "track.selected.mute" ||
                       bind.canonical == "track.selected.muted") {
                layout_.targetTrackMute = target.canonical;
            } else if (bind.canonical == "track.selected.arm" ||
                       bind.canonical == "track.selected.armed") {
                layout_.targetTrackArm = target.canonical;
            } else if (bind.canonical == "transport.bpm") {
                layout_.targetTransportBpm = target.canonical;
            } else if (bind.canonical == "transport.quant") {
                layout_.targetTransportQuant = target.canonical;
            }
        }
    });
    layout_.enabled = true;
}

} // namespace avantgarde
