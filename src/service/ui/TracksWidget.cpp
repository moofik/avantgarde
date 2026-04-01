#include "service/ui/TracksWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "service/ui/GbFrameComposer.h"

namespace avantgarde {
namespace {

constexpr std::size_t kTracksPerPage = 2;

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

} // namespace

TracksWidget::TracksWidget() noexcept = default;

TracksWidget::TracksWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth),
      headerTitle_(options.headerTitle),
      speedStep_(options.speedStep > 0.0f ? options.speedStep : 0.05f),
      bpmStep_(options.bpmStep > 0.0f ? options.bpmStep : 1.0f) {}

const char* TracksWidget::id() const noexcept {
    return "tracks";
}

void TracksWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    // Всегда рендерим полный кадр заново, чтобы не копить артефакты.
    out.clear();
    // Композер собирает готовый монохромный GB-кадр.
    const std::string frame = GbFrameComposer::buildMonochromeFrame(
        rtState,
        frameWidth_,
        headerTitle_,
        static_cast<std::size_t>(navState.trackPage),
        buildActionStatusLine_(rtState, navState));

    // Конвертируем цельный текст кадра в line-buffer виджета.
    std::istringstream in(frame);
    std::string line;
    while (std::getline(in, line)) {
        out.lines.push_back(line);
    }
}

WidgetOutput TracksWidget::onInput(UiInputAction, const UiState&, UiNavState&) {
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
        a.def.id = UiAction::Id::SceneOpenManager;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Open Manager";
        a.state.enabled = true;
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
    auto push = [&out](UiIntent intent) {
        out.intents.push_back(std::move(intent));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneTrackSelect: {
            if (totalTracks == 0) break;
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.selectedTrack = wrapPrevTrack(selectedTrack, totalTracks);
            } else if (action.op == UiAction::Op::AdjustNext || action.op == UiAction::Op::Apply) {
                navState.selectedTrack = wrapNextTrack(selectedTrack, totalTracks);
            } else {
                break;
            }
            navState.trackPage = pageForTrack(navState.selectedTrack, totalTracks);
            UiIntent it{};
            it.type = UiIntentType::SetActiveTrack;
            it.track = navState.selectedTrack;
            push(std::move(it));
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
            push(std::move(it));
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
            push(std::move(it));
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
            push(std::move(it));
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
            push(std::move(it));
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
            push(std::move(it));
        } break;

        case UiAction::Id::SceneOpenManager: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            navState.scene = UiScene::Manager;
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            push(std::move(it));
        } break;

        case UiAction::Id::None:
        case UiAction::Id::GlobalPlayStop:
        case UiAction::Id::GlobalUndo:
        case UiAction::Id::GlobalBack:
        case UiAction::Id::GlobalPagePrev:
        case UiAction::Id::GlobalPageNext:
        case UiAction::Id::GlobalMasterVolume:
        case UiAction::Id::SceneTrackGain:
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
        case UiAction::Id::SceneOpenManager:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        default:
            std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
            break;
    }
    return std::string(buf);
}

} // namespace avantgarde
