#include "service/ui/TracksWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "service/ui/layout/UiNodeComponentComposer.h"
#include "service/ui/layout/SceneFrameAsciiRenderer.h"
#include "service/ui/layout/TracksSceneFrameBuilder.h"

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

const char* playbackModeToStr(UiTrackPlaybackMode mode) noexcept {
    return (mode == UiTrackPlaybackMode::Looper) ? "LOOP" : "NOTE";
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

std::string makeBar(float value01, std::size_t width) {
    const float v = std::clamp(value01, 0.0f, 1.0f);
    const std::size_t filled = static_cast<std::size_t>(v * static_cast<float>(width));
    std::string out;
    out.reserve(width);
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(i < filled ? '#' : '.');
    }
    return out;
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
      speedStep_(options.speedStep > 0.0f ? options.speedStep : 0.05f),
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

    UiNodeComponentComposer::compose(UiScene::Tracks, *layoutTemplate_, preparedParams, builder);

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
    const std::size_t meterWidth = (inner > 40U) ? std::min<std::size_t>(inner - 40U, 24U) : 12U;

    std::string transportLine{};
    {
        char line[256]{};
        std::snprintf(line, sizeof(line), " TRN:%s BPM:%5.1f TS:%u/%u Q:%s MET:%c OVF:%c ",
                      rtState.transport.playing ? "PLAY" : "STOP",
                      rtState.transport.bpm,
                      static_cast<unsigned>(rtState.transport.tsNum),
                      static_cast<unsigned>(rtState.transport.tsDen),
                      quantToStr(rtState.transport.quant),
                      rtState.transport.metronomeEnabled ? 'Y' : 'N',
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

            std::snprintf(line, sizeof(line), "   bars:%u  fx:%u  loop:%c  m:%c  a:%c",
                          static_cast<unsigned>(tr.bars),
                          static_cast<unsigned>(tr.fxCount),
                          tr.loop ? 'Y' : 'N',
                          tr.muted ? 'Y' : 'N',
                          tr.armed ? 'Y' : 'N');
            trackRows.emplace_back(line);

            std::snprintf(line, sizeof(line), "   mode:%s",
                          playbackModeToStr(tr.playbackMode));
            trackRows.emplace_back(line);

            std::snprintf(line, sizeof(line), "   spd:%1.2f [%s]",
                          tr.stretchRatio,
                          makeBar(speedTo01(tr.stretchRatio), meterWidth).c_str());
            trackRows.emplace_back(line);

            std::snprintf(line, sizeof(line), "   g  :%1.2f [%s]",
                          tr.gain01,
                          makeBar(tr.gain01, meterWidth).c_str());
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
        keysLine1 = " keys [j/k focus] [/? adjust] [o apply] [m metronome] [F10 detect BPM]";
        keysLine2 = "      [F2 undo] [F9 redo] [F4 manager] [F11/F12 pages] [q]";
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
    const bool selectedLooperEnabled =
        selectedTrackView && selectedTrackView->playbackMode == UiTrackPlaybackMode::Looper;
    constexpr float kMinBpm = 40.0f;
    constexpr float kMaxBpm = 220.0f;
    const float bpm01 = std::clamp((rtState.transport.bpm - kMinBpm) / (kMaxBpm - kMinBpm), 0.0f, 1.0f);

    params.number["track.selected.speed"] = selectedSpeed01;
    params.number["track.selected.gain"] = selectedGain01;
    params.number["track.selected.looper_mode"] = selectedLooperEnabled ? 1.0f : 0.0f;
    params.text["track.selected.mode"] = selectedLooperEnabled ? "MODE: LOOPER" : "MODE: NOTE";
    params.integer["track.selected.looper_mode.selectedIndex"] = selectedLooperEnabled ? 1 : 0;
    params.text["track.selected.looper_mode.label"] = "LOOPER";
    params.number["transport.bpm"] = bpm01;
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
    params.flag["track.selected.speed.selected"] = (selectedActionId == UiAction::Id::SceneTrackSpeed);
    params.flag["track.selected.gain.selected"] = (selectedActionId == UiAction::Id::SceneTrackGain);
    params.flag["track.selected.looper_mode.selected"] = (selectedActionId == UiAction::Id::SceneTrackLooperMode);
    params.flag["transport.bpm.selected"] = (selectedActionId == UiAction::Id::SceneTempoBpm);

    return params;
}

void TracksWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    // Всегда рендерим полный кадр заново, чтобы не копить артефакты.
    out.clear();
    const SceneFrame frame = TracksSceneFrameBuilder::build(
        rtState,
        navState,
        frameWidth_,
        layout_.enabled && !layout_.title.empty() ? layout_.title : headerTitle_,
        buildActionStatusLine_(rtState, navState),
        layout_.enabled && !layout_.keysHint.empty() ? layout_.keysHint : std::string_view{});
    out.lines = SceneFrameAsciiRenderer::render(frame);
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
        a.state.enabled = (totalTracks > 0);
        a.state.value = static_cast<float>(selectedTrack + 1U);
        pushAction(std::move(a));
    }
    {
        const bool trackValid = (totalTracks > 0);
        const bool looperEnabled =
            trackValid && rtState.tracks[selectedTrack].playbackMode == UiTrackPlaybackMode::Looper;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackLooperMode;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Looper";
        a.state.enabled = trackValid;
        a.state.value = looperEnabled ? 1.0f : 0.0f;
        pushAction(std::move(a));
    }
    {
        const bool trackValid = (totalTracks > 0);
        const bool muted = trackValid ? rtState.tracks[selectedTrack].muted : false;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackMute;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Track Mute";
        a.state.enabled = trackValid;
        a.state.value = muted ? 1.0f : 0.0f;
        pushAction(std::move(a));
    }
    {
        const bool trackValid = (totalTracks > 0);
        const bool armed = trackValid ? rtState.tracks[selectedTrack].armed : false;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackArm;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Track Arm";
        a.state.enabled = trackValid;
        a.state.value = armed ? 1.0f : 0.0f;
        pushAction(std::move(a));
    }
    {
        const bool trackValid = (totalTracks > 0);
        const float speed = trackValid ? rtState.tracks[selectedTrack].stretchRatio : 1.0f;
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackSpeed;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Track Speed";
        a.def.minValue = 0.25f;
        a.def.maxValue = 4.0f;
        a.def.step = speedStep_;
        a.state.enabled = trackValid;
        a.state.value = speed;
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
        a.state.enabled = true;
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
        a.state.enabled = true;
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
        const bool trackValid = (totalTracks > 0) &&
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
        const bool trackValid = (totalTracks > 0);
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
                navState.scene = UiScene::TrackContext;
                navState.cursor = 0;
                navState.scroll = 0;
                navState.sceneActionIndex = 0;
                UiIntent open{};
                open.type = UiIntentType::OpenScene;
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

        case UiAction::Id::SceneTrackLooperMode: {
            if (totalTracks == 0) break;
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SetTrackLooperMode;
            it.track = selectedTrack;
            const bool looperEnabled = rtState.tracks[selectedTrack].playbackMode == UiTrackPlaybackMode::Looper;
            it.value = looperEnabled ? 0.0f : 1.0f;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackMute: {
            if (totalTracks == 0) break;
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SetTrackMuted;
            it.track = selectedTrack;
            it.value = rtState.tracks[selectedTrack].muted ? 0.0f : 1.0f;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackArm: {
            if (totalTracks == 0) break;
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SetTrackArmed;
            it.track = selectedTrack;
            it.value = rtState.tracks[selectedTrack].armed ? 0.0f : 1.0f;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTrackSpeed: {
            if (totalTracks == 0) break;
            if (action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float step = (action.def.step > 0.0f) ? action.def.step : speedStep_;
            const float minV = (action.def.minValue > 0.0f) ? action.def.minValue : 0.25f;
            const float maxV = (action.def.maxValue > minV) ? action.def.maxValue : 4.0f;
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float next = std::clamp(rtState.tracks[selectedTrack].stretchRatio + dir * step, minV, maxV);
            UiIntent it{};
            it.type = UiIntentType::SetTrackSpeed;
            it.track = selectedTrack;
            it.value = next;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneQuantize: {
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
            it.type = UiIntentType::SetTransportQuant;
            it.value = static_cast<float>(v);
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::SceneTempoBpm: {
            if (action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float step = (action.def.step > 0.0f) ? action.def.step : bpmStep_;
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float next = std::clamp(rtState.transport.bpm + dir * step, 20.0f, 300.0f);
            UiIntent it{};
            it.type = UiIntentType::SetTransportBpm;
            it.value = next;
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
            navState.scene = UiScene::FxList;
            navState.selectedFx = 0;
            navState.fxAddPopupOpen = false;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            pushIntentToWidgetOutput(std::move(it));
        } break;

        case UiAction::Id::None:
        case UiAction::Id::GlobalPlayStop:
        case UiAction::Id::GlobalUndo:
        case UiAction::Id::GlobalBack:
        case UiAction::Id::GlobalPagePrev:
        case UiAction::Id::GlobalPageNext:
        case UiAction::Id::GlobalMasterVolume:
        case UiAction::Id::SceneTrackGain:
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
        case UiAction::Id::SceneTrackLooperMode:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), onOff(a.state.value >= 0.5f));
            break;
        case UiAction::Id::SceneTrackMute:
        case UiAction::Id::SceneTrackArm:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), onOff(a.state.value >= 0.5f));
            break;
        case UiAction::Id::SceneTrackSpeed:
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
    });
    layout_.enabled = true;
}

} // namespace avantgarde
