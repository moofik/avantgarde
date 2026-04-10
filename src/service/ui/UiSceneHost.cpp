#include "service/ui/UiSceneHost.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace avantgarde {
namespace {

// На одной странице показываем один трек.
// Значение используется только в UI-навигации и не влияет на аудио-движок.
constexpr std::size_t kTracksPerPage = 1;

// Сдвиг активного трека назад с циклическим переходом.
// Если current вышел за диапазон (например после изменения числа треков),
// сначала зажимаем его к последнему валидному индексу.
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

// Сдвиг активного трека вперед с циклическим переходом.
// Если current невалидный, начинаем с нулевого индекса.
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

// Вычисление номера UI-страницы по индексу трека.
// Это чисто визуальная навигация: номер страницы не отправляется в RT.
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

// Признак, что gesture относится к active-action-pointer модели:
// перемещение фокуса, изменение значения, apply, undo/redo и т.д.
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

// Преобразуем жест в операцию UiAction::Op.
// Это центральная точка сопоставления "кнопка -> семантика действия".
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

// Операции, которые считаются подтверждением/запуском действия.
bool isApplyLikeOp(UiAction::Op op) noexcept {
    return op == UiAction::Op::Apply ||
           op == UiAction::Op::Press;
}

} // namespace

bool UiSceneHost::registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget) {
    if (!widget) {
        return false;
    }
    // Host владеет виджетом и гарантирует один активный экземпляр на сцену.
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

bool UiSceneHost::buildPreparedActive(UiPreparedLayout& out, const UiState& rtState) const {
    // Всегда очищаем выходной контейнер до сборки нового кадра,
    // чтобы рендерер не получил "хвосты" от предыдущей сцены.
    out = UiPreparedLayout{};
    const auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        const std::string msg = "UiSceneHost: no widget registered for active scene";
        std::fprintf(stderr, "[UI][RENDER][ERROR] %s\n", msg.c_str());
        throw std::runtime_error(msg);
    }
    // Виджет собирает только декларативный prepared-layout.
    // Геометрию и пиксели строит отдельный рендер-слой.
    if (!widget->buildPreparedLayout(out, rtState, nav_)) {
        std::string msg = "UiSceneHost: widget '";
        msg += widget->id();
        msg += "' failed to build prepared layout";
        std::fprintf(stderr, "[UI][RENDER][ERROR] %s\n", msg.c_str());
        throw std::runtime_error(msg);
    }
    return true;
}

UiActionCatalog UiSceneHost::queryGlobalActions_(const UiState& rtState) const {
    UiActionCatalog out{};

    // Локальный helper уменьшает повторение boilerplate.
    auto push = [&out](UiAction action) {
        out.actions.push_back(std::move(action));
    };

    {
        // Play/Stop храним как bool-value, чтобы UI мог показать текущий статус.
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
        // Универсальный "назад": контекстный выход из текущей сцены.
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
        // Пагинация имеет смысл только если треков больше одной страницы.
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
        // Курсор глобальных action-ов переживает перерисовки,
        // поэтому зажимаем его к текущему размеру каталога.
        out.currentIndex = std::min<uint16_t>(
            nav_.globalActionIndex,
            static_cast<uint16_t>(out.actions.size() - 1U));
        // Явно помечаем выбранный action, чтобы виджет мог отрисовать индикацию.
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput UiSceneHost::onGlobalAction_(UiAction& action, const UiState& rtState) {
    WidgetOutput out{};
    // По умолчанию считаем, что global action распознан host-ом.
    out.handled = true;
    if (!action.state.enabled) {
        // Disabled действие не генерирует intents.
        return out;
    }

    switch (action.def.id) {
        case UiAction::Id::GlobalPlayStop: {
            if (!isApplyLikeOp(action.op)) {
                return out;
            }
            // Интент работает как toggle на основании актуального transport-state.
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
            // Шаг 1: FxEditor -> FxList.
            if (nav_.scene == UiScene::FxEditor && widgets_[sceneIndex_(UiScene::FxList)]) {
                UiIntent it{};
                it.type = UiIntentType::Back;
                it.scene = UiScene::FxList;
                it.resetSceneActionIndex = true;
                out.intents.push_back(std::move(it));
                return out;
            }
            // Шаг 2: любая непустая сцена -> Tracks.
            if (nav_.scene != UiScene::Tracks) {
                UiIntent it{};
                it.type = UiIntentType::Back;
                it.scene = UiScene::Tracks;
                it.resetSceneActionIndex = true;
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
            // Обновляем и selectedTrack, и trackPage, чтобы UI и state
            // оставались согласованными после перелистывания.
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
        case UiAction::Id::SceneTrackLooperMode:
        case UiAction::Id::SceneTrackPlaybackProfile:
        case UiAction::Id::SceneTrackMute:
        case UiAction::Id::SceneTrackArm:
        case UiAction::Id::SceneTrackSpeed:
        case UiAction::Id::SceneTrackGain:
        case UiAction::Id::SceneTrackTrimStart:
        case UiAction::Id::SceneTrackTrimEnd:
        case UiAction::Id::SceneQuantize:
        case UiAction::Id::SceneTempoBpm:
        case UiAction::Id::ScenePatternPrev:
        case UiAction::Id::ScenePatternNext:
        case UiAction::Id::SceneDetectProjectBpm:
        case UiAction::Id::SceneAddFx:
        case UiAction::Id::SceneAddReverb:
        case UiAction::Id::SceneOpenManager:
        case UiAction::Id::SceneOpenFxList:
        case UiAction::Id::SceneTrackMenuLoadSample:
        case UiAction::Id::SceneTrackMenuClear:
        case UiAction::Id::SceneTrackMenuFxList:
        case UiAction::Id::SceneTrackMenuSampleEdit:
        case UiAction::Id::SceneFxTypeSelect:
        case UiAction::Id::SceneFxSlotSelect:
        case UiAction::Id::SceneFxEnabled:
        case UiAction::Id::SceneFxRemove:
        case UiAction::Id::SceneFxOpenEditor:
        case UiAction::Id::SceneFxParamSelect:
        case UiAction::Id::SceneFxParamValue:
        case UiAction::Id::SceneFxBack:
        default:
            // Не-global id: передаем управление scene-слою.
            out.handled = false;
            return out;
    }
}

WidgetOutput UiSceneHost::handleGesture(UiGesture action, const UiState& rtState) {
    // Единая нормализация аппаратных клавиш в "виртуальные" действия.
    // Это позволяет поддерживать один и тот же pipeline на desktop и device.
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
        // Переключаем контекст active-action-pointer:
        // Scene scope <-> Global scope.
        nav_.actionScope = (nav_.actionScope == UiAction::Scope::Scene)
                               ? UiAction::Scope::Global
                               : UiAction::Scope::Scene;
        return WidgetOutput{true, {}};
    }

    // Direct-select (track/pattern) обрабатывается в SamplerApplication,
    // где есть доступ к payload UiGestureEvent::value.
    if (action == UiGesture::SelectPrevTrack) {
        // Старый direct-control путь без action-pointer.
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
        // Открываем сцену только если виджет менеджера зарегистрирован.
        if (widgets_[sceneIndex_(UiScene::Manager)]) {
            UiIntent openIntent{};
            openIntent.type = UiIntentType::OpenScene;
            openIntent.scene = UiScene::Manager;
            openIntent.resetCursor = true;
            openIntent.resetScroll = true;
            openIntent.resetSceneActionIndex = true;
            return WidgetOutput{true, {openIntent}};
        }
        return {};
    }
    if (action == UiGesture::BackScene) {
        // Специальный случай: если в FxList открыт popup выбора эффекта,
        // сначала закрываем popup жестом самого виджета, а не выходим из сцены.
        if (nav_.scene == UiScene::FxList &&
            nav_.fxAddPopupOpen &&
            widgets_[sceneIndex_(UiScene::FxList)]) {
            return widgets_[sceneIndex_(UiScene::FxList)]->onGesture(UiGesture::BackScene, rtState, nav_);
        }
        if (nav_.scene == UiScene::FxEditor && widgets_[sceneIndex_(UiScene::FxList)]) {
            UiIntent backIntent{};
            backIntent.type = UiIntentType::Back;
            backIntent.scene = UiScene::FxList;
            backIntent.resetSceneActionIndex = true;
            return WidgetOutput{true, {backIntent}};
        }
        if (nav_.scene != UiScene::Tracks) {
            UiIntent backIntent{};
            backIntent.type = UiIntentType::Back;
            backIntent.scene = UiScene::Tracks;
            backIntent.resetSceneActionIndex = true;
            return WidgetOutput{true, {backIntent}};
        }
        return {};
    }

    // Глобальные transport/track hotkeys больше не обрабатываются в Application legacy-switch.
    // Все команды заворачиваются в intents прямо здесь.
    if (action == UiGesture::PlayActiveTrack || action == UiGesture::StopActiveTrack) {
        // С исторических причин жест называется *ActiveTrack, но семантика
        // теперь глобальная: запускаем/останавливаем транспорт целиком.
        UiIntent it{};
        it.type = UiIntentType::SetTransportPlaying;
        it.value = (action == UiGesture::PlayActiveTrack) ? 1.0f : 0.0f;
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::QuantNone ||
        action == UiGesture::QuantBeat ||
        action == UiGesture::QuantBar) {
        // Значения 0/1/2 совпадают с enum QuantizeMode в dispatcher-слое.
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
        // Защищаемся от выхода за рабочий диапазон BPM.
        const float dir = (action == UiGesture::BpmUp) ? 1.0f : -1.0f;
        it.value = std::clamp(rtState.transport.bpm + dir, 20.0f, 300.0f);
        return WidgetOutput{true, {it}};
    }
    if (action == UiGesture::ToggleMetronome) {
        UiIntent it{};
        it.type = UiIntentType::SetMetronomeEnabled;
        it.value = rtState.transport.metronomeEnabled ? 0.0f : 1.0f;
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
        // selectedTrack может устареть после изменения конфигурации проекта,
        // поэтому зажимаем индекс перед чтением трековых полей.
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
        // speed-step фиксированный для hotkey-пути; pointer-путь берет step из action.def.
        const float dir = (action == UiGesture::TrackSpeedUp) ? 1.0f : -1.0f;
        it.value = std::clamp(rtState.tracks[t].stretchRatio + dir * 0.05f, 0.25f, 4.0f);
        return WidgetOutput{true, {it}};
    }

    auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        // Если для активной сцены виджет не зарегистрирован,
        // возвращаем "не обработано" без генерации интентов.
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
        // В зависимости от scope берем action-каталог либо у host-а, либо у виджета.
        UiActionCatalog catalog = globalScope
                                      ? queryGlobalActions_(rtState)
                                      : widget->queryAvailableActions(rtState, nav_);
        if (catalog.actions.empty()) {
            // В режиме pointer пустой каталог = жест обработан, но делать нечего.
            return WidgetOutput{true, {}};
        }

        // Курсор action-ов живет в nav_ и сохраняется между кадрами.
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
        if (!active.state.enabled &&
            (op == UiAction::Op::Apply ||
             op == UiAction::Op::Press ||
             op == UiAction::Op::AdjustPrev ||
             op == UiAction::Op::AdjustNext)) {
            return WidgetOutput{true, {}};
        }
        // delta рассчитываем только для относительных операций изменения.
        if (op == UiAction::Op::AdjustPrev) {
            active.delta = -std::max(0.0f, active.def.step);
        } else if (op == UiAction::Op::AdjustNext) {
            active.delta = std::max(0.0f, active.def.step);
        }
        if (globalScope) {
            // Global scope не вызывает виджет напрямую.
            return onGlobalAction_(active, rtState);
        }
        // Scene scope: виджет сам маппит action в intents.
        return widget->onAction(active, rtState, nav_);
    }

    // Финальный fallback: отдаем жест виджету как есть.
    return widget->onGesture(action, rtState, nav_);
}

} // namespace avantgarde
