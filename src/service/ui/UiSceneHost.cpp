#include "service/ui/UiSceneHost.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "service/ui/layout/UiPreparedLayoutAsciiRenderer.h"

namespace avantgarde {
namespace {

constexpr std::size_t kTracksPerPage = 1;

uint8_t selectPrevTrack(uint8_t current, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t last = static_cast<uint8_t>(totalTracks - 1U);
    if (current > last) {
        return last;
    }
    return (current == 0U) ? last : static_cast<uint8_t>(current - 1U);
}

uint8_t selectNextTrack(uint8_t current, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t last = static_cast<uint8_t>(totalTracks - 1U);
    if (current > last) {
        return 0;
    }
    return (current == last) ? 0U : static_cast<uint8_t>(current + 1U);
}

uint16_t pageForTrack(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t safeTrack = (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
    return static_cast<uint16_t>(safeTrack / kTracksPerPage);
}

UiGesture normalizeHardwareAction(UiGesture action) noexcept {
    // F-ряд выступает как аппаратные "софт-кнопки".
    // Приводим их к общим action-командам, чтобы остальная логика была единообразной.
    switch (action) {
        case UiGesture::F1: return UiGesture::ActionApply;
        case UiGesture::F2: return UiGesture::ActionUndo;
        case UiGesture::F3: return UiGesture::ActionScopeToggle;
        case UiGesture::F4: return UiGesture::OpenManager;
        case UiGesture::F5: return UiGesture::ActionFocusPrev;
        case UiGesture::F6: return UiGesture::ActionFocusNext;
        case UiGesture::F7: return UiGesture::ActionAdjustPrev;
        case UiGesture::F8: return UiGesture::ActionAdjustNext;
        case UiGesture::F9: return UiGesture::ActionRedo;
        case UiGesture::F10: return UiGesture::ActionAlt;
        case UiGesture::F11: return UiGesture::TrackPagePrev;
        case UiGesture::F12: return UiGesture::TrackPageNext;
        default:
            return action;
    }
}

bool isPointerAction(UiGesture action) noexcept {
    switch (action) {
        case UiGesture::ActionFocusPrev:
        case UiGesture::ActionFocusNext:
        case UiGesture::ActionAdjustPrev:
        case UiGesture::ActionAdjustNext:
        case UiGesture::ActionApply:
        case UiGesture::ActionUndo:
        case UiGesture::ActionRedo:
        case UiGesture::ActionQuick:
        case UiGesture::ActionAlt:
        case UiGesture::ActionPress:
        case UiGesture::ActionRelease:
            return true;
        default:
            return false;
    }
}

UiAction::Op toActionOp(UiGesture action) noexcept {
    switch (action) {
        case UiGesture::ActionFocusPrev: return UiAction::Op::FocusPrev;
        case UiGesture::ActionFocusNext: return UiAction::Op::FocusNext;
        case UiGesture::ActionAdjustPrev: return UiAction::Op::AdjustPrev;
        case UiGesture::ActionAdjustNext: return UiAction::Op::AdjustNext;
        case UiGesture::ActionApply: return UiAction::Op::Apply;
        case UiGesture::ActionUndo: return UiAction::Op::Undo;
        case UiGesture::ActionRedo: return UiAction::Op::Redo;
        case UiGesture::ActionPress: return UiAction::Op::Press;
        case UiGesture::ActionRelease: return UiAction::Op::Release;
        case UiGesture::ActionQuick: return UiAction::Op::Apply;
        case UiGesture::ActionAlt: return UiAction::Op::Apply;
        default: return UiAction::Op::None;
    }
}

bool isApplyLikeOp(UiAction::Op op) noexcept {
    return op == UiAction::Op::Apply ||
           op == UiAction::Op::Press;
}

} // namespace

bool UiSceneHost::registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget) {
    if (!widget) {
        return false;
    }
    // Registry keeps single owner for each scene widget.
    widgets_[sceneIndex_(scene)] = std::move(widget);
    return true;
}

void UiSceneHost::setScene(UiScene scene) noexcept {
    nav_.scene = scene;
}

UiScene UiSceneHost::scene() const noexcept {
    return nav_.scene;
}

UiNavState& UiSceneHost::nav() noexcept {
    return nav_;
}

const UiNavState& UiSceneHost::nav() const noexcept {
    return nav_;
}

bool UiSceneHost::renderActive(UiTextBuffer& out, const UiState& rtState) const {
    // Всегда начинаем с чистого кадрового буфера для детерминированного рендера.
    out.clear();
    const auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        const std::string msg = "UiSceneHost: no widget registered for active scene";
        std::fprintf(stderr, "[UI][RENDER][ERROR] %s\n", msg.c_str());
        throw std::runtime_error(msg);
    }
    UiPreparedLayout prepared{};
    if (!widget->buildPreparedLayout(prepared, rtState, nav_)) {
        std::string msg = "UiSceneHost: widget '";
        msg += widget->id();
        msg += "' failed to build prepared layout";
        std::fprintf(stderr, "[UI][RENDER][ERROR] %s\n", msg.c_str());
        throw std::runtime_error(msg);
    }
    out.lines = UiPreparedLayoutAsciiRenderer::render(prepared);
    return true;
}

UiActionCatalog UiSceneHost::queryGlobalActions_(const UiState& rtState) const {
    UiActionCatalog out{};

    auto push = [&out](UiAction action) {
        out.actions.push_back(std::move(action));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::GlobalPlayStop;
        a.def.scope = UiAction::Scope::Global;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Transport Play";
        a.state.enabled = true;
        a.state.value = rtState.transport.playing ? 1.0f : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::GlobalBack;
        a.def.scope = UiAction::Scope::Global;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Back";
        a.state.enabled = true;
        push(std::move(a));
    }
    {
        const bool hasMultiPage = rtState.tracks.size() > kTracksPerPage;
        UiAction a{};
        a.def.id = UiAction::Id::GlobalPagePrev;
        a.def.scope = UiAction::Scope::Global;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Track Page Prev";
        a.state.enabled = hasMultiPage;
        push(std::move(a));
    }
    {
        const bool hasMultiPage = rtState.tracks.size() > kTracksPerPage;
        UiAction a{};
        a.def.id = UiAction::Id::GlobalPageNext;
        a.def.scope = UiAction::Scope::Global;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Track Page Next";
        a.state.enabled = hasMultiPage;
        push(std::move(a));
    }

    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(
            nav_.globalActionIndex,
            static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput UiSceneHost::onGlobalAction_(UiAction& action, const UiState& rtState) {
    WidgetOutput out{};
    out.handled = true;
    if (!action.state.enabled) {
        return out;
    }

    switch (action.def.id) {
        case UiAction::Id::GlobalPlayStop: {
            if (!isApplyLikeOp(action.op)) {
                return out;
            }
            UiIntent it{};
            it.type = UiIntentType::SetTransportPlaying;
            it.value = rtState.transport.playing ? 0.0f : 1.0f;
            out.intents.push_back(std::move(it));
            return out;
        }
        case UiAction::Id::GlobalBack: {
            if (!isApplyLikeOp(action.op)) {
                return out;
            }
            if (nav_.scene == UiScene::FxEditor && widgets_[sceneIndex_(UiScene::FxList)]) {
                nav_.scene = UiScene::FxList;
                nav_.sceneActionIndex = 0;
                UiIntent it{};
                it.type = UiIntentType::Back;
                out.intents.push_back(std::move(it));
                return out;
            }
            if (nav_.scene != UiScene::Tracks) {
                nav_.scene = UiScene::Tracks;
                nav_.sceneActionIndex = 0;
                UiIntent it{};
                it.type = UiIntentType::Back;
                out.intents.push_back(std::move(it));
            }
            return out;
        }
        case UiAction::Id::GlobalPagePrev: {
            if (!isApplyLikeOp(action.op) && action.op != UiAction::Op::AdjustPrev) {
                return out;
            }
            if (rtState.tracks.empty()) {
                return out;
            }
            nav_.selectedTrack = selectPrevTrack(nav_.selectedTrack, rtState.tracks.size());
            nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
            UiIntent it{};
            it.type = UiIntentType::SetActiveTrack;
            it.track = nav_.selectedTrack;
            out.intents.push_back(std::move(it));
            return out;
        }
        case UiAction::Id::GlobalPageNext: {
            if (!isApplyLikeOp(action.op) && action.op != UiAction::Op::AdjustNext) {
                return out;
            }
            if (rtState.tracks.empty()) {
                return out;
            }
            nav_.selectedTrack = selectNextTrack(nav_.selectedTrack, rtState.tracks.size());
            nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
            UiIntent it{};
            it.type = UiIntentType::SetActiveTrack;
            it.track = nav_.selectedTrack;
            out.intents.push_back(std::move(it));
            return out;
        }
        case UiAction::Id::None:
        case UiAction::Id::GlobalUndo:
        case UiAction::Id::GlobalMasterVolume:
        case UiAction::Id::SceneTrackSelect:
        case UiAction::Id::SceneTrackMute:
        case UiAction::Id::SceneTrackArm:
        case UiAction::Id::SceneTrackSpeed:
        case UiAction::Id::SceneTrackGain:
        case UiAction::Id::SceneQuantize:
        case UiAction::Id::SceneTempoBpm:
        case UiAction::Id::SceneDetectProjectBpm:
        case UiAction::Id::SceneAddFx:
        case UiAction::Id::SceneAddReverb:
        case UiAction::Id::SceneOpenManager:
        case UiAction::Id::SceneOpenFxList:
        case UiAction::Id::SceneFxTypeSelect:
        case UiAction::Id::SceneFxSlotSelect:
        case UiAction::Id::SceneFxEnabled:
        case UiAction::Id::SceneFxRemove:
        case UiAction::Id::SceneFxOpenEditor:
        case UiAction::Id::SceneFxParamSelect:
        case UiAction::Id::SceneFxParamValue:
        case UiAction::Id::SceneFxBack:
        default:
            out.handled = false;
            return out;
    }
}

WidgetOutput UiSceneHost::handleGesture(UiGesture action, const UiState& rtState) {
    action = normalizeHardwareAction(action);

    // В трековой сцене даем удобные алиасы под active-action-pointer:
    // j/k — смена активного action, Enter — apply.
    if (nav_.scene == UiScene::Tracks) {
        if (action == UiGesture::ListUp) {
            action = UiGesture::ActionFocusPrev;
        } else if (action == UiGesture::ListDown) {
            action = UiGesture::ActionFocusNext;
        } else if (action == UiGesture::ListEnter) {
            action = UiGesture::ActionApply;
        }
    }

    if (action == UiGesture::ActionScopeToggle) {
        nav_.actionScope = (nav_.actionScope == UiAction::Scope::Scene)
                               ? UiAction::Scope::Global
                               : UiAction::Scope::Scene;
        return WidgetOutput{true, {}};
    }

    // Global shortcuts are handled by host to keep behavior consistent across scenes.
    if (action == UiGesture::SelectPrevTrack) {
        nav_.selectedTrack = selectPrevTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        UiIntent it{};
        it.type = UiIntentType::SetActiveTrack;
        it.track = nav_.selectedTrack;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::SelectNextTrack) {
        nav_.selectedTrack = selectNextTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        UiIntent it{};
        it.type = UiIntentType::SetActiveTrack;
        it.track = nav_.selectedTrack;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::TrackPagePrev) {
        if (rtState.tracks.empty()) {
            return WidgetOutput{true, {}};
        }
        nav_.selectedTrack = selectPrevTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        UiIntent it{};
        it.type = UiIntentType::SetActiveTrack;
        it.track = nav_.selectedTrack;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::TrackPageNext) {
        if (rtState.tracks.empty()) {
            return WidgetOutput{true, {}};
        }
        nav_.selectedTrack = selectNextTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        UiIntent it{};
        it.type = UiIntentType::SetActiveTrack;
        it.track = nav_.selectedTrack;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::OpenManager) {
        if (widgets_[sceneIndex_(UiScene::Manager)]) {
            nav_.scene = UiScene::Manager;
            UiIntent openIntent{};
            openIntent.type = UiIntentType::OpenScene;
            return WidgetOutput{true, {openIntent}};
        }
        return {};
    }
    if (action == UiGesture::BackScene) {
        if (nav_.scene == UiScene::FxList &&
            nav_.fxAddPopupOpen &&
            widgets_[sceneIndex_(UiScene::FxList)]) {
            return widgets_[sceneIndex_(UiScene::FxList)]->onGesture(UiGesture::BackScene, rtState, nav_);
        }
        if (nav_.scene == UiScene::FxEditor && widgets_[sceneIndex_(UiScene::FxList)]) {
            nav_.scene = UiScene::FxList;
            nav_.sceneActionIndex = 0;
            UiIntent backIntent{};
            backIntent.type = UiIntentType::Back;
            return WidgetOutput{true, {backIntent}};
        }
        if (nav_.scene != UiScene::Tracks) {
            nav_.scene = UiScene::Tracks;
            nav_.sceneActionIndex = 0;
            UiIntent backIntent{};
            backIntent.type = UiIntentType::Back;
            return WidgetOutput{true, {backIntent}};
        }
        return {};
    }

    // Глобальные transport/track hotkeys больше не обрабатываются в Application legacy-switch.
    // Все команды заворачиваются в intents прямо здесь.
    if (action == UiGesture::PlayActiveTrack || action == UiGesture::StopActiveTrack) {
        UiIntent it{};
        it.type = UiIntentType::SetTransportPlaying;
        it.value = (action == UiGesture::PlayActiveTrack) ? 1.0f : 0.0f;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::QuantNone ||
        action == UiGesture::QuantBeat ||
        action == UiGesture::QuantBar) {
        UiIntent it{};
        it.type = UiIntentType::SetTransportQuant;
        it.value = (action == UiGesture::QuantNone) ? 0.0f : (action == UiGesture::QuantBeat ? 1.0f : 2.0f);
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::BpmUp || action == UiGesture::BpmDown) {
        // В FxEditor те же кнопки меняют значение выбранного FX-параметра.
        if (nav_.scene == UiScene::FxEditor) {
            auto& fxWidget = widgets_[sceneIndex_(UiScene::FxEditor)];
            if (fxWidget) {
                return fxWidget->onGesture(action, rtState, nav_);
            }
        }
        UiIntent it{};
        it.type = UiIntentType::SetTransportBpm;
        const float dir = (action == UiGesture::BpmUp) ? 1.0f : -1.0f;
        it.value = std::clamp(rtState.transport.bpm + dir, 20.0f, 300.0f);
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::MuteActiveTrack ||
        action == UiGesture::UnmuteActiveTrack ||
        action == UiGesture::MuteToggleActiveTrack ||
        action == UiGesture::ArmToggleActiveTrack ||
        action == UiGesture::TrackSpeedUp ||
        action == UiGesture::TrackSpeedDown) {
        if (rtState.tracks.empty()) {
            return WidgetOutput{true, {}};
        }
        const uint8_t t = (nav_.selectedTrack >= rtState.tracks.size())
                              ? static_cast<uint8_t>(rtState.tracks.size() - 1U)
                              : nav_.selectedTrack;
        UiIntent it{};
        it.track = t;
        if (action == UiGesture::MuteActiveTrack ||
            action == UiGesture::UnmuteActiveTrack ||
            action == UiGesture::MuteToggleActiveTrack) {
            it.type = UiIntentType::SetTrackMuted;
            if (action == UiGesture::MuteActiveTrack) {
                it.value = 1.0f;
            } else if (action == UiGesture::UnmuteActiveTrack) {
                it.value = 0.0f;
            } else {
                it.value = rtState.tracks[t].muted ? 0.0f : 1.0f;
            }
            return WidgetOutput{true, {it}};
        }
        if (action == UiGesture::ArmToggleActiveTrack) {
            it.type = UiIntentType::SetTrackArmed;
            it.value = rtState.tracks[t].armed ? 0.0f : 1.0f;
            return WidgetOutput{true, {it}};
        }
        it.type = UiIntentType::SetTrackSpeed;
        const float dir = (action == UiGesture::TrackSpeedUp) ? 1.0f : -1.0f;
        it.value = std::clamp(rtState.tracks[t].stretchRatio + dir * 0.05f, 0.25f, 4.0f);
        return WidgetOutput{true, {it}};
    }

    auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        return {};
    }

    // Быстрый хоткей детекта BPM на основном экране.
    // F10 -> ActionAlt -> "Detect BPM" для активного трека.
    if (nav_.scene == UiScene::Tracks && action == UiGesture::ActionAlt) {
        UiAction a{};
        a.def.id = UiAction::Id::SceneDetectProjectBpm;
        a.op = UiAction::Op::Apply;
        return widget->onAction(a, rtState, nav_);
    }

    // Быстрый режим FX List (device-like flow без menu diving):
    // F5/F6 -> вверх/вниз по слотам, F1 -> apply (edit/add),
    // F7 -> bypass on/off, F8 -> remove.
    if (nav_.scene == UiScene::FxList) {
        if (action == UiGesture::ActionFocusPrev) {
            return widget->onGesture(UiGesture::ListUp, rtState, nav_);
        }
        if (action == UiGesture::ActionFocusNext) {
            return widget->onGesture(UiGesture::ListDown, rtState, nav_);
        }
        if (action == UiGesture::ActionApply) {
            return widget->onGesture(UiGesture::ListEnter, rtState, nav_);
        }
        if (action == UiGesture::ActionAdjustPrev) {
            UiAction a{};
            a.def.id = UiAction::Id::SceneFxEnabled;
            a.op = UiAction::Op::Apply;
            return widget->onAction(a, rtState, nav_);
        }
        if (action == UiGesture::ActionAdjustNext) {
            UiAction a{};
            a.def.id = UiAction::Id::SceneFxRemove;
            a.op = UiAction::Op::Apply;
            return widget->onAction(a, rtState, nav_);
        }
    }

    // Быстрый режим FX Editor:
    // F5/F6 -> выбор параметра, F7/F8 -> изменение значения, F1 -> bypass on/off.
    if (nav_.scene == UiScene::FxEditor) {
        if (action == UiGesture::ActionFocusPrev) {
            return widget->onGesture(UiGesture::ListUp, rtState, nav_);
        }
        if (action == UiGesture::ActionFocusNext) {
            return widget->onGesture(UiGesture::ListDown, rtState, nav_);
        }
        if (action == UiGesture::ActionAdjustPrev) {
            return widget->onGesture(UiGesture::BpmDown, rtState, nav_);
        }
        if (action == UiGesture::ActionAdjustNext) {
            return widget->onGesture(UiGesture::BpmUp, rtState, nav_);
        }
        if (action == UiGesture::ActionApply) {
            UiAction a{};
            a.def.id = UiAction::Id::SceneFxEnabled;
            a.op = UiAction::Op::Apply;
            return widget->onAction(a, rtState, nav_);
        }
    }

    // Active Action Pointer path:
    // action + navState + uiState -> intent (вычисляется в виджете).
    if (isPointerAction(action)) {
        const bool globalScope = (nav_.actionScope == UiAction::Scope::Global);
        UiActionCatalog catalog = globalScope
                                      ? queryGlobalActions_(rtState)
                                      : widget->queryAvailableActions(rtState, nav_);
        if (catalog.actions.empty()) {
            return WidgetOutput{true, {}};
        }

        uint16_t& cursor = globalScope ? nav_.globalActionIndex : nav_.sceneActionIndex;
        cursor = std::min<uint16_t>(cursor, static_cast<uint16_t>(catalog.actions.size() - 1U));

        const UiAction::Op op = toActionOp(action);
        if (op == UiAction::Op::FocusPrev) {
            if (cursor == 0U) {
                cursor = static_cast<uint16_t>(catalog.actions.size() - 1U);
            } else {
                cursor = static_cast<uint16_t>(cursor - 1U);
            }
            return WidgetOutput{true, {}};
        }
        if (op == UiAction::Op::FocusNext) {
            cursor = static_cast<uint16_t>((cursor + 1U) % catalog.actions.size());
            return WidgetOutput{true, {}};
        }

        UiAction active = catalog.actions[cursor];
        active.op = op;
        if (op == UiAction::Op::AdjustPrev) {
            active.delta = -std::max(0.0f, active.def.step);
        } else if (op == UiAction::Op::AdjustNext) {
            active.delta = std::max(0.0f, active.def.step);
        }
        if (globalScope) {
            return onGlobalAction_(active, rtState);
        }
        return widget->onAction(active, rtState, nav_);
    }

    return widget->onGesture(action, rtState, nav_);
}

} // namespace avantgarde
