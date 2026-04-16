#include "service/ui/widgets/SequencerWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "service/ui/layout/UiNodeComponentComposer.h"

namespace avantgarde {
namespace {

constexpr uint16_t kMinZoom = 1U;
constexpr uint16_t kMaxZoom = 8U;

float baseTickStepForZoom(uint16_t zoom) noexcept {
    // Чем больше zoom, тем меньше шаг редактирования (coarse -> fine).
    const uint16_t z = std::clamp<uint16_t>(zoom, kMinZoom, kMaxZoom);
    constexpr float kSteps[kMaxZoom] = {96.0f, 48.0f, 24.0f, 12.0f, 6.0f, 3.0f, 2.0f, 1.0f};
    return kSteps[z - 1U];
}

float safeLengthTicks(const UiSequencerState& seq) noexcept {
    return std::max<float>(1.0f, static_cast<float>(std::max<SequencerTick>(1U, seq.lengthTicks)));
}

std::string formatSecondsShort(float seconds) {
    const int total = std::max<int>(0, static_cast<int>(std::floor(seconds)));
    const int min = total / 60;
    const int sec = total % 60;
    char out[64]{};
    if (min > 0) {
        std::snprintf(out, sizeof(out), "%d min %02d sec", min, sec);
    } else {
        std::snprintf(out, sizeof(out), "%d sec", sec);
    }
    return out;
}

} // namespace

SequencerWidget::SequencerWidget() noexcept = default;

SequencerWidget::SequencerWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth),
      mode_(options.mode) {
    if (options.layoutTemplate.has_value()) {
        layoutTemplate_ = options.layoutTemplate;
        buildLayoutModel_(*options.layoutTemplate);
    }
}

const char* SequencerWidget::id() const noexcept {
    return (mode_ == Mode::Lane) ? "sequencer_lane" : "sequencer";
}

uint16_t SequencerWidget::clampLane_(uint16_t lane, const UiSequencerState& seq) noexcept {
    if (seq.lanes.empty()) {
        return 0;
    }
    return std::min<uint16_t>(lane, static_cast<uint16_t>(seq.lanes.size() - 1U));
}

uint16_t SequencerWidget::clampPoint_(uint16_t point, const UiSequencerState& seq) noexcept {
    if (seq.points.empty()) {
        return 0;
    }
    return std::min<uint16_t>(point, static_cast<uint16_t>(seq.points.size() - 1U));
}

const char* SequencerWidget::quantToStr_(SequencerQuantize q) noexcept {
    switch (q) {
        case SequencerQuantize::None: return "NONE";
        case SequencerQuantize::Sixteenth: return "1/16";
        case SequencerQuantize::Eighth: return "1/8";
        case SequencerQuantize::Quarter: return "1/4";
        case SequencerQuantize::Bar: return "BAR";
        default: return "UNK";
    }
}

const char* SequencerWidget::laneKindToStr_(UiSequencerLaneKind kind) noexcept {
    return (kind == UiSequencerLaneKind::Automation) ? "AUTO" : "EVT";
}

std::vector<float> SequencerWidget::buildLaneWave_(const UiSequencerState& seq) {
    constexpr std::size_t kCols = 160U;
    std::vector<float> wave(kCols, 0.0f);
    if (seq.points.empty()) {
        return wave;
    }
    const float lenTicks = safeLengthTicks(seq);
    auto toIdx = [&](SequencerTick tick) -> std::size_t {
        const float norm = std::clamp(static_cast<float>(tick) / lenTicks, 0.0f, 1.0f);
        const float x = norm * static_cast<float>(kCols - 1U);
        return static_cast<std::size_t>(std::lround(x));
    };

    const UiSequencerLaneKind kind =
        (seq.activeLane < seq.lanes.size()) ? seq.lanes[seq.activeLane].kind : UiSequencerLaneKind::Event;
    if (kind == UiSequencerLaneKind::Event) {
        for (const UiSequencerPointView& point : seq.points) {
            const std::size_t x = toIdx(point.tick);
            wave[x] = 1.0f;
            if (x > 0U) {
                wave[x - 1U] = std::max(wave[x - 1U], 0.35f);
            }
            if (x + 1U < wave.size()) {
                wave[x + 1U] = std::max(wave[x + 1U], 0.35f);
            }
        }
        return wave;
    }

    // Automation lane: линейная интерполяция между точками.
    std::vector<UiSequencerPointView> pts = seq.points;
    std::sort(pts.begin(), pts.end(), [](const UiSequencerPointView& a, const UiSequencerPointView& b) {
        if (a.tick != b.tick) {
            return a.tick < b.tick;
        }
        return a.objectId < b.objectId;
    });

    const std::size_t firstX = toIdx(pts.front().tick);
    const float firstV = std::clamp(pts.front().value, 0.0f, 1.0f);
    for (std::size_t x = 0; x <= firstX && x < wave.size(); ++x) {
        wave[x] = firstV;
    }

    for (std::size_t i = 0; i + 1U < pts.size(); ++i) {
        const std::size_t x0 = toIdx(pts[i].tick);
        const std::size_t x1 = toIdx(pts[i + 1U].tick);
        const float v0 = std::clamp(pts[i].value, 0.0f, 1.0f);
        const float v1 = std::clamp(pts[i + 1U].value, 0.0f, 1.0f);
        if (x1 <= x0) {
            wave[x0] = v1;
            continue;
        }
        const float span = static_cast<float>(x1 - x0);
        for (std::size_t x = x0; x <= x1 && x < wave.size(); ++x) {
            const float t = static_cast<float>(x - x0) / span;
            wave[x] = v0 + (v1 - v0) * t;
        }
    }

    const std::size_t lastX = toIdx(pts.back().tick);
    const float lastV = std::clamp(pts.back().value, 0.0f, 1.0f);
    for (std::size_t x = lastX; x < wave.size(); ++x) {
        wave[x] = lastV;
    }

    return wave;
}

std::string SequencerWidget::buildActionStatusLine_(const UiState& rtState,
                                                    const UiNavState& navState) const {
    const UiActionCatalog actions = queryAvailableActions(rtState, navState);
    if (actions.actions.empty()) {
        return " action: - ";
    }
    const uint16_t idx = std::min<uint16_t>(actions.currentIndex, static_cast<uint16_t>(actions.actions.size() - 1U));
    const UiAction& a = actions.actions[idx];
    char buf[256]{};
    if (a.def.valueKind == UiAction::ValueKind::None) {
        std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), " action:%s = %.2f ", a.def.label.c_str(), a.state.value);
    }
    if (mode_ == Mode::Lane) {
        std::string s = buf;
        s += "[LANE]";
        return s;
    }
    return buf;
}

UiPreparedParams SequencerWidget::buildPreparedLayoutParams_(const UiState& rtState,
                                                             const UiNavState& navState) const {
    UiPreparedParams params{};
    const UiSequencerState& seq = rtState.sequencer;
    const uint16_t laneIndex = clampLane_(navState.sequencerLane, seq);
    const uint16_t pointIndex = clampPoint_(navState.sequencerObject, seq);

    char title[128]{};
    const unsigned p = static_cast<unsigned>(seq.patternId == kInvalidPatternId ? 0U : seq.patternId);
    std::snprintf(title, sizeof(title), " %s %02u ", layout_.title.c_str(), p);
    params.text["status.scene.title"] = title;
    params.text["header_title"] = title;

    char meta[256]{};
    const float safeTsDen = std::max<float>(1.0f, static_cast<float>(rtState.transport.tsDen));
    const float beatsPerBar = (std::max<float>(1.0f, static_cast<float>(rtState.transport.tsNum)) * 4.0f) / safeTsDen;
    const float ticksPerBar = std::max<float>(1.0f, static_cast<float>(std::max<uint16_t>(1U, seq.ppq)) * beatsPerBar);
    const float loopTick = static_cast<float>(seq.playheadTick);
    const uint32_t currentBar = std::min<uint32_t>(
        std::max<uint32_t>(1U, seq.lengthBars),
        static_cast<uint32_t>(std::floor(loopTick / ticksPerBar)) + 1U);
    std::snprintf(meta, sizeof(meta), " LEN:%u BAR Q:%s REC:%s PLAY:%s BAR:%u/%u ",
                  static_cast<unsigned>(seq.lengthBars),
                  quantToStr_(seq.quant),
                  rtState.transport.recordEnabled ? "ON" : "OFF",
                  rtState.transport.playing ? "ON" : "OFF",
                  static_cast<unsigned>(currentBar),
                  static_cast<unsigned>(std::max<uint32_t>(1U, seq.lengthBars)));
    params.text["status.sequencer.meta"] = meta;
    params.text["sequencer_meta"] = meta;

    const char* loopMode = seq.resetOnLoop ? "RESET ON LOOP" : "CONTINUE";
    params.text["status.sequencer.mode"] = std::string(" MODE: ") + loopMode + " ";
    params.text["sequencer_mode"] = params.text["status.sequencer.mode"];

    char state[256]{};
    const float seconds = (rtState.transport.bpm > 0.0f)
                              ? (static_cast<float>(seq.playheadTick) / static_cast<float>(std::max<uint16_t>(1U, seq.ppq))) *
                                    (60.0f / rtState.transport.bpm)
                              : 0.0f;
    std::snprintf(state, sizeof(state), " VIEW:%s  LANE:%u/%u  OBJ:%u/%u  PH:%s ",
                  (mode_ == Mode::Lane) ? "LANE" : "LIST",
                  static_cast<unsigned>(laneIndex + 1U),
                  static_cast<unsigned>(std::max<std::size_t>(1U, seq.lanes.size())),
                  static_cast<unsigned>(pointIndex + 1U),
                  static_cast<unsigned>(std::max<std::size_t>(1U, seq.points.size())),
                  formatSecondsShort(seconds).c_str());
    params.text["status.sequencer.state"] = state;
    params.text["sequencer_state"] = state;

    std::vector<std::string> laneRows{};
    laneRows.reserve(std::max<std::size_t>(1U, seq.lanes.size()));
    if (seq.lanes.empty()) {
        laneRows.push_back(" no lanes ");
    } else {
        for (const UiSequencerLaneView& lane : seq.lanes) {
            char row[192]{};
            std::snprintf(row,
                          sizeof(row),
                          " %u %-4s %s",
                          static_cast<unsigned>(lane.laneId + 1U),
                          laneKindToStr_(lane.kind),
                          lane.label.c_str());
            laneRows.emplace_back(row);
        }
    }
    params.rows["sequencer_lanes"] = laneRows;
    params.integer["sequencer_lanes.selected"] = static_cast<int32_t>(seq.lanes.empty() ? 0 : laneIndex);

    std::string laneTitle = seq.laneTitle.empty() ? std::string(" parameter: - ") : (" " + seq.laneTitle + " ");
    params.text["lane_title"] = laneTitle;
    params.text["status.sequencer.lane_title"] = laneTitle;

    std::vector<std::string> pointRows{};
    pointRows.reserve(std::max<std::size_t>(1U, seq.points.size()));
    if (seq.points.empty()) {
        pointRows.push_back(" no points ");
    } else {
        for (std::size_t i = 0; i < seq.points.size(); ++i) {
            const UiSequencerPointView& point = seq.points[i];
            char row[96]{};
            std::snprintf(row, sizeof(row), " P%02u   %s", static_cast<unsigned>(i + 1U), point.label.c_str());
            pointRows.emplace_back(row);
        }
    }
    params.rows["lane_points"] = pointRows;
    params.integer["lane_points.selected"] = static_cast<int32_t>(seq.points.empty() ? 0 : pointIndex);

    const float lenTicks = safeLengthTicks(seq);
    const float playhead01 = std::clamp(static_cast<float>(seq.playheadTick) / lenTicks, 0.0f, 1.0f);
    params.waves["lane_curve"] = buildLaneWave_(seq);
    params.number["lane_curve.trim_start"] = 0.0f;
    params.number["lane_curve.trim_end"] = 1.0f;
    params.number["lane_curve.playhead"] = playhead01;
    params.integer["lane_curve.selected"] = seq.points.empty() ? -1 : static_cast<int32_t>(pointIndex);
    params.flag["lane_curve.curve_mode"] = true;
    std::vector<float> markerXs{};
    std::vector<float> markerYs{};
    markerXs.reserve(seq.points.size());
    markerYs.reserve(seq.points.size());
    for (const UiSequencerPointView& point : seq.points) {
        markerXs.push_back(std::clamp(static_cast<float>(point.tick) / lenTicks, 0.0f, 1.0f));
        markerYs.push_back(std::clamp(point.value, 0.0f, 1.0f));
    }
    params.waves["lane_curve.markers.x"] = std::move(markerXs);
    params.waves["lane_curve.markers.y"] = std::move(markerYs);

    std::string action = buildActionStatusLine_(rtState, navState);
    const float totalSeconds = (rtState.transport.bpm > 0.0f)
                                   ? (static_cast<float>(seq.lengthTicks) / static_cast<float>(std::max<uint16_t>(1U, seq.ppq))) *
                                         (60.0f / rtState.transport.bpm)
                                   : 0.0f;
    params.text["lane_timeline"] =
        " time: " + formatSecondsShort(seconds) + " / " + formatSecondsShort(totalSeconds) + " ";

    if (!seq.points.empty()) {
        const UiSequencerPointView& psel = seq.points[pointIndex];
        char valueBuf[64]{};
        std::snprintf(valueBuf, sizeof(valueBuf), " val:%0.3f", psel.value);
        action += valueBuf;
    }
    params.text["status.action"] = action;
    params.text["action_status"] = params.text["status.action"];
    params.text["status.keys"] = layout_.keysHint;
    params.text["keys_hint"] = layout_.keysHint;
    params.integer["frame.heightHint"] = (mode_ == Mode::Lane) ? 26 : 24;
    return params;
}

bool SequencerWidget::buildPreparedLayout(UiPreparedLayout& out,
                                          const UiState& rtState,
                                          const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    UiPreparedParams params = buildPreparedLayoutParams_(rtState, navState);
    uint16_t frameHeightHint = 24U;
    if (auto h = params.findInteger("frame.heightHint"); h.has_value()) {
        frameHeightHint = static_cast<uint16_t>(std::clamp<int32_t>(*h, 8, 4096));
    }

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("sequencer")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(std::max<uint16_t>(frameWidth_, 36U))
        .frameHeightHint(frameHeightHint);

    UiNodeComponentComposer::compose(
        (mode_ == Mode::Lane) ? UiScene::SequencerLane : UiScene::Sequencer,
        *layoutTemplate_,
        rtState,
        navState,
        params,
        builder);
    out = std::move(builder).build();
    return true;
}

WidgetOutput SequencerWidget::onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    const UiSequencerState& seq = rtState.sequencer;

    if (action == UiGesture::BackScene || action == UiGesture::ListParent) {
        UiIntent back{};
        back.type = UiIntentType::Back;
        back.scene = UiScene::Tracks;
        back.resetSceneActionIndex = true;
        out.handled = true;
        out.intents.push_back(std::move(back));
        return out;
    }

    if (action == UiGesture::ListEnter) {
        if (mode_ == Mode::List) {
            UiIntent open{};
            open.type = UiIntentType::OpenScene;
            open.scene = UiScene::SequencerLane;
            open.resetCursor = true;
            open.resetScroll = true;
            open.resetSceneActionIndex = true;
            out.handled = true;
            out.intents.push_back(std::move(open));
        } else {
            UiIntent it{};
            it.type = UiIntentType::SequencerAddObjectAtCursor;
            out.handled = true;
            out.intents.push_back(std::move(it));
        }
        return out;
    }

    if (action == UiGesture::DeleteObject && mode_ == Mode::Lane && !seq.points.empty()) {
        UiIntent it{};
        it.type = UiIntentType::SequencerDeleteSelectedObject;
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }
    if (action == UiGesture::DeleteObject && mode_ == Mode::List && !seq.lanes.empty()) {
        UiIntent it{};
        it.type = UiIntentType::SequencerDeleteSelectedLane;
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }

    if (action == UiGesture::ListUp || action == UiGesture::ListDown) {
        UiIntent it{};
        if (mode_ == Mode::Lane) {
            it.type = UiIntentType::SequencerSetActiveObject;
            const uint16_t cur = clampPoint_(navState.sequencerObject, seq);
            if (action == UiGesture::ListUp) {
                it.value = static_cast<float>(cur == 0U ? std::max<std::size_t>(0U, seq.points.size() - 1U) : cur - 1U);
            } else {
                const uint16_t max = seq.points.empty() ? 0U : static_cast<uint16_t>(seq.points.size() - 1U);
                it.value = static_cast<float>(cur >= max ? 0U : cur + 1U);
            }
        } else {
            it.type = UiIntentType::SequencerSetActiveLane;
            const uint16_t cur = clampLane_(navState.sequencerLane, seq);
            if (action == UiGesture::ListUp) {
                it.value = static_cast<float>(cur == 0U ? std::max<std::size_t>(0U, seq.lanes.size() - 1U) : cur - 1U);
            } else {
                const uint16_t max = seq.lanes.empty() ? 0U : static_cast<uint16_t>(seq.lanes.size() - 1U);
                it.value = static_cast<float>(cur >= max ? 0U : cur + 1U);
            }
        }
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }

    return {};
}

UiActionCatalog SequencerWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};
    const UiSequencerState& seq = rtState.sequencer;
    const bool laneMode = (mode_ == Mode::Lane);
    const bool hasLane = !seq.lanes.empty();
    const bool hasPoint = !seq.points.empty();
    const float stepTicks = baseTickStepForZoom(navState.sequencerZoom);

    auto push = [&out](UiAction a) {
        out.actions.push_back(std::move(a));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerLaneSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "Lane";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, seq.lanes.size()));
        a.def.step = 1.0f;
        a.state.enabled = hasLane;
        a.state.value = static_cast<float>(clampLane_(navState.sequencerLane, seq) + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerLaneFocus;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = laneMode ? "Back To Lanes" : "Open Lane";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = 1.0f;
        a.state.enabled = hasLane;
        a.state.value = laneMode ? 1.0f : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerScrub;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Scrub";
        a.def.minValue = 0.0f;
        a.def.maxValue = safeLengthTicks(seq);
        a.def.step = stepTicks;
        a.state.enabled = true;
        a.state.value = static_cast<float>(navState.sequencerScrubTick);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerObjectSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "Object";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, seq.points.size()));
        a.def.step = 1.0f;
        a.state.enabled = laneMode && hasPoint;
        a.state.value = static_cast<float>(clampPoint_(navState.sequencerObject, seq) + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerMoveObject;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Move Time";
        a.def.minValue = -safeLengthTicks(seq);
        a.def.maxValue = safeLengthTicks(seq);
        a.def.step = stepTicks;
        a.state.enabled = laneMode && hasPoint;
        a.state.value = 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerValue;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Value";
        a.def.minValue = -1.0f;
        a.def.maxValue = 1.0f;
        a.def.step = 0.05f;
        a.state.enabled = laneMode && hasPoint;
        a.state.value = 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerZoom;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "Zoom";
        a.def.minValue = static_cast<float>(kMinZoom);
        a.def.maxValue = static_cast<float>(kMaxZoom);
        a.def.step = 1.0f;
        a.state.enabled = true;
        a.state.value = static_cast<float>(std::clamp<uint16_t>(navState.sequencerZoom, kMinZoom, kMaxZoom));
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerTool;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "Tool";
        a.def.minValue = 0.0f;
        a.def.maxValue = 3.0f;
        a.def.step = 1.0f;
        a.state.enabled = true;
        a.state.value = static_cast<float>(std::min<uint16_t>(navState.sequencerTool, 3U));
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerAddObject;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Add";
        a.state.enabled = hasLane;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneSequencerDeleteObject;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = laneMode ? "Delete Point" : "Delete Lane";
        a.state.enabled = laneMode ? hasPoint : hasLane;
        push(std::move(a));
    }

    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput SequencerWidget::onAction(UiAction& action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;

    const UiSequencerState& seq = rtState.sequencer;
    const uint16_t laneIndex = clampLane_(navState.sequencerLane, seq);
    const uint16_t pointIndex = clampPoint_(navState.sequencerObject, seq);
    const float stepTicks = baseTickStepForZoom(navState.sequencerZoom);
    const float maxTick = safeLengthTicks(seq);

    auto push = [&out](UiIntent it) {
        out.intents.push_back(std::move(it));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneSequencerLaneSelect: {
            if (seq.lanes.empty()) {
                break;
            }
            uint16_t next = laneIndex;
            if (action.op == UiAction::Op::AdjustPrev) {
                next = (laneIndex == 0U) ? static_cast<uint16_t>(seq.lanes.size() - 1U) : static_cast<uint16_t>(laneIndex - 1U);
            } else if (action.op == UiAction::Op::AdjustNext) {
                next = (laneIndex + 1U >= seq.lanes.size()) ? 0U : static_cast<uint16_t>(laneIndex + 1U);
            } else if (action.op == UiAction::Op::Apply || action.op == UiAction::Op::Press) {
                if (mode_ == Mode::List) {
                    UiIntent open{};
                    open.type = UiIntentType::OpenScene;
                    open.scene = UiScene::SequencerLane;
                    open.resetCursor = true;
                    open.resetScroll = true;
                    open.resetSceneActionIndex = true;
                    push(std::move(open));
                }
                break;
            } else {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerSetActiveLane;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerLaneFocus: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            UiIntent nav{};
            nav.type = UiIntentType::OpenScene;
            nav.scene = (mode_ == Mode::Lane) ? UiScene::Sequencer : UiScene::SequencerLane;
            nav.resetCursor = true;
            nav.resetScroll = true;
            nav.resetSceneActionIndex = true;
            push(std::move(nav));
        } break;

        case UiAction::Id::SceneSequencerScrub: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float delta = (action.op == UiAction::Op::AdjustPrev) ? -stepTicks : stepTicks;
            const float next = std::clamp(static_cast<float>(navState.sequencerScrubTick) + delta, 0.0f, maxTick);
            UiIntent it{};
            it.type = UiIntentType::SequencerSetScrubTick;
            it.value = next;
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerObjectSelect: {
            if (mode_ != Mode::Lane || seq.points.empty()) {
                break;
            }
            uint16_t next = pointIndex;
            if (action.op == UiAction::Op::AdjustPrev) {
                next = (pointIndex == 0U) ? static_cast<uint16_t>(seq.points.size() - 1U) : static_cast<uint16_t>(pointIndex - 1U);
            } else if (action.op == UiAction::Op::AdjustNext) {
                next = (pointIndex + 1U >= seq.points.size()) ? 0U : static_cast<uint16_t>(pointIndex + 1U);
            } else {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerSetActiveObject;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerMoveObject: {
            if (mode_ != Mode::Lane || seq.points.empty()) {
                break;
            }
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerNudgeObjectTime;
            it.value = (action.op == UiAction::Op::AdjustPrev) ? -stepTicks : stepTicks;
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerValue: {
            if (mode_ != Mode::Lane || seq.points.empty()) {
                break;
            }
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerAdjustObjectValue;
            it.value = (action.op == UiAction::Op::AdjustPrev) ? -0.05f : 0.05f;
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerZoom: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const int cur = std::clamp<int>(navState.sequencerZoom, kMinZoom, kMaxZoom);
            const int next = std::clamp<int>(cur + ((action.op == UiAction::Op::AdjustPrev) ? -1 : 1),
                                             kMinZoom,
                                             kMaxZoom);
            UiIntent it{};
            it.type = UiIntentType::SequencerSetZoom;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerTool: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const int cur = std::clamp<int>(navState.sequencerTool, 0, 3);
            int next = cur + ((action.op == UiAction::Op::AdjustPrev) ? -1 : 1);
            if (next < 0) {
                next = 3;
            } else if (next > 3) {
                next = 0;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerSetTool;
            it.value = static_cast<float>(next);
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerAddObject: {
            if (action.op != UiAction::Op::Apply && action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::SequencerAddObjectAtCursor;
            push(std::move(it));
        } break;

        case UiAction::Id::SceneSequencerDeleteObject: {
            if (action.op != UiAction::Op::Apply && action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            if (mode_ == Mode::Lane) {
                if (seq.points.empty()) {
                    break;
                }
                it.type = UiIntentType::SequencerDeleteSelectedObject;
            } else {
                if (seq.lanes.empty()) {
                    break;
                }
                it.type = UiIntentType::SequencerDeleteSelectedLane;
            }
            push(std::move(it));
        } break;

        default:
            out.handled = false;
            break;
    }

    return out;
}

void SequencerWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    layout_.enabled = true;
    if (!tpl.widgetId.empty()) {
        layout_.title = tpl.widgetId;
    }
    if (mode_ == Mode::Lane) {
        layout_.keysHint = " keys [j/k obj] [f7/f8 edit] [f1 add] [d del] [o apply] [esc back] ";
    } else {
        layout_.keysHint = " keys [j/k lane] [enter lane] [d del lane] [f5/f6 focus] [f7/f8 adjust] [esc back] ";
    }
}

} // namespace avantgarde
