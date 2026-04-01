#include "app/SamplerApplication.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "contracts/IUiInput.h"
#include "contracts/ids.h"
#include "service/ui/UiWidgetFactory.h"

namespace avantgarde {
namespace {

float quantModeToIntentValue(QuantizeMode mode) noexcept {
    switch (mode) {
        case QuantizeMode::None: return 0.0f;
        case QuantizeMode::Beat: return 1.0f;
        case QuantizeMode::Bar: return 2.0f;
        default: return 2.0f;
    }
}

QuantizeMode quantModeFromIntentValue(float value) noexcept {
    const int q = static_cast<int>(std::lround(value));
    if (q <= 0) {
        return QuantizeMode::None;
    }
    if (q == 1) {
        return QuantizeMode::Beat;
    }
    return QuantizeMode::Bar;
}

} // namespace

SamplerApplication::~SamplerApplication() {
    // Безопасное завершение всех циклов/потоков в любом сценарии выхода.
    stopUi_.store(true, std::memory_order_release);
    if (controlThread_.joinable()) {
        controlThread_.join();
    }
    if (uiThread_.joinable()) {
        uiThread_.join();
    }
    io_.stopTerminalInput();
    engine_.stop();
}

int SamplerApplication::run(const SamplerAppConfig& config) {
    // 1) Инициализация движка и IO.
    UiState bootstrap{};
    std::string error;
    if (!engine_.init(config.engine, config.audioHost, bootstrap, error)) {
        std::printf("%s\n", error.c_str());
        return 2;
    }
    if (!io_.init(config.io, error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    trCtl_ = bootstrap.transport;
    tracksCtl_ = bootstrap.tracks;
    trCtl_.activeTrack = clampUiTrack_(trCtl_.activeTrack);

    // Стартовая загрузка клипов живет в application-слое, а не в config движка.
    for (const SamplerAppConfig::StartupClipLoad& load : config.startupClipLoads) {
        if (load.path.empty()) {
            continue;
        }
        if (load.track >= tracksCtl_.size()) {
            continue;
        }
        const uint8_t t = load.track;
        std::string clipName;
        if (!engine_.loadSampleToTrack(t, load.path, clipName)) {
            continue;
        }
        tracksCtl_[t].clipName = clipName;
        tracksCtl_[t].muted = false;
        tracksCtl_[t].armed = false;
        tracksCtl_[t].loop = true;
    }

    refreshAllTrackViewStates_();

    // Публикуем начальный снапшот в UI store.
    UiState initialState{};
    initialState.transport = trCtl_;
    initialState.tracks = tracksCtl_;
    uiStore_.setState(initialState);
    clearHistory_();

    // 2) Сборка scene-графа (каждая функциональная сцена = виджет).
    UiWidgetFactory widgetFactory(
        UiWidgetFactoryOptions{
            .frameWidth = config.gbTextWidth,
            .tracksHeaderTitle = "AVANTGARDE",
        });
    (void)sceneHost_.registerWidget(UiScene::Tracks, widgetFactory.create(UiScene::Tracks));
    (void)sceneHost_.registerWidget(UiScene::Manager, widgetFactory.create(UiScene::Manager));
    sceneHost_.setScene(UiScene::Tracks);
    sceneHost_.nav().selectedTrack = trCtl_.activeTrack;
    sceneHost_.nav().trackPage = static_cast<uint16_t>(trCtl_.activeTrack / 2U);

    // 3) Запуск аудио.
    if (!engine_.start(error)) {
        std::printf("%s\n", error.c_str());
        return 3;
    }

    // 4) Запуск фонового input (terminal) и control-потока.
    stopUi_.store(false, std::memory_order_release);
    io_.startTerminalInput(stopUi_);

    controlThread_ = std::thread([this]() {
        while (!stopUi_.load(std::memory_order_acquire)) {
            UiInputEvent ev{};
            if (!io_.readNextInputEvent(ev)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!handleInputEvent_(ev.action)) {
                break;
            }
        }
        // Гарантированно гасим preview-голос при завершении control loop.
        engine_.previewStop();
    });

    // 5) Для оконного режима рендер идет в main thread, иначе - отдельный поток.
    if (!io_.renderOnMainThread()) {
        uiThread_ = std::thread([this]() {
            while (!stopUi_.load(std::memory_order_acquire)) {
                renderUiOnce_();
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
        });
    }

    // 6) Главный цикл: pump событий окна + рендер.
    while (!stopUi_.load(std::memory_order_acquire)) {
        if (io_.readWindowEvents()) {
            renderUiOnce_();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // 7) Аккуратный stop/join.
    stopUi_.store(true, std::memory_order_release);
    if (controlThread_.joinable()) {
        controlThread_.join();
    }
    if (uiThread_.joinable()) {
        uiThread_.join();
    }
    io_.stopTerminalInput();
    engine_.stop();
    return 0;
}

uint8_t SamplerApplication::clampUiTrack_(uint8_t track) const noexcept {
    if (tracksCtl_.empty()) {
        return 0;
    }
    return (track >= tracksCtl_.size()) ? static_cast<uint8_t>(tracksCtl_.size() - 1) : track;
}

bool SamplerApplication::buildUndoIntent_(const UiIntent& forward, UiIntent& undoOut) const {
    undoOut = UiIntent{};
    switch (forward.type) {
        case UiIntentType::SetActiveTrack: {
            if (tracksCtl_.empty()) {
                return false;
            }
            undoOut = forward;
            undoOut.track = clampUiTrack_(trCtl_.activeTrack);
            return true;
        }
        case UiIntentType::SetTrackMuted: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(forward.track);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracksCtl_[t].muted ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackArmed: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(forward.track);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracksCtl_[t].armed ? 1.0f : 0.0f;
            return true;
        }
        case UiIntentType::SetTrackSpeed: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(forward.track);
            undoOut = forward;
            undoOut.track = t;
            undoOut.value = tracksCtl_[t].stretchRatio;
            return true;
        }
        case UiIntentType::SetTransportQuant:
            undoOut = forward;
            undoOut.value = quantModeToIntentValue(trCtl_.quant);
            return true;
        case UiIntentType::SetTransportBpm:
            undoOut = forward;
            undoOut.value = trCtl_.bpm;
            return true;
        default:
            // Остальные intent'ы в текущем MVP не откатываем.
            return false;
    }
}

bool SamplerApplication::applyIntent_(const UiIntent& intent) {
    // Формула слоя application:
    //   intent = f(action, navState, uiState) уже посчитан внутри виджета.
    // Здесь мы только применяем intent к engine/UI-store и не знаем деталей action.
    switch (intent.type) {
        case UiIntentType::LoadSampleToTrack: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(intent.track);
            std::string clipName;
            if (!engine_.loadSampleToTrack(t, intent.path, clipName)) {
                return false;
            }
            // После успешной загрузки подправляем view-state трека.
            tracksCtl_[t].clipName = clipName;
            tracksCtl_[t].muted = false;
            tracksCtl_[t].loop = true;
            refreshTrackViewState_(t);
            uiStore_.setTrack(t, tracksCtl_[t]);
            return true;
        }
        case UiIntentType::PreviewRequest:
            engine_.previewRequest(intent.path);
            return true;
        case UiIntentType::PreviewStop:
            engine_.previewStop();
            return true;
        case UiIntentType::SetActiveTrack: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t next = clampUiTrack_(intent.track);
            if (next == trCtl_.activeTrack) {
                return false;
            }
            trCtl_.activeTrack = next;
            uiStore_.setTransport(trCtl_);
            return true;
        }
        case UiIntentType::SetTrackMuted: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(intent.track);
            const bool muted = (intent.value >= 0.5f);
            if (tracksCtl_[t].muted == muted) {
                return false;
            }
            tracksCtl_[t].muted = muted;
            (void)engine_.setTrackMuted(t, muted);
            refreshTrackViewState_(t);
            uiStore_.setTrack(t, tracksCtl_[t]);
            return true;
        }
        case UiIntentType::SetTrackArmed: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(intent.track);
            const bool armed = (intent.value >= 0.5f);
            if (tracksCtl_[t].armed == armed) {
                return false;
            }
            tracksCtl_[t].armed = armed;
            (void)engine_.setTrackArmed(t, armed);
            uiStore_.setTrack(t, tracksCtl_[t]);
            return true;
        }
        case UiIntentType::SetTrackSpeed: {
            if (tracksCtl_.empty()) {
                return false;
            }
            const uint8_t t = clampUiTrack_(intent.track);
            const float next = std::clamp(intent.value, 0.25f, 4.0f);
            if (std::fabs(tracksCtl_[t].stretchRatio - next) < 1e-6f) {
                return false;
            }
            tracksCtl_[t].stretchRatio = next;
            (void)engine_.setTrackSpeed(t, tracksCtl_[t].stretchRatio);
            uiStore_.setTrack(t, tracksCtl_[t]);
            return true;
        }
        case UiIntentType::SetTransportQuant: {
            const QuantizeMode next = quantModeFromIntentValue(intent.value);
            if (trCtl_.quant == next) {
                return false;
            }
            trCtl_.quant = next;
            engine_.setQuantize(trCtl_.quant);
            uiStore_.setTransport(trCtl_);
            return true;
        }
        case UiIntentType::SetTransportBpm: {
            const float next = std::clamp(intent.value, 20.0f, 300.0f);
            if (std::fabs(trCtl_.bpm - next) < 1e-6f) {
                return false;
            }
            trCtl_.bpm = next;
            engine_.setTempo(trCtl_.bpm);
            uiStore_.setTransport(trCtl_);
            return true;
        }
        case UiIntentType::OpenScene:
            // OpenScene пока обрабатывается scene host'ом на уровне маршрутизации.
            // Для MVP здесь ничего дополнительно делать не нужно.
            return false;
        case UiIntentType::Back:
        case UiIntentType::None:
        case UiIntentType::AddFxToTrack:
        case UiIntentType::RemoveFxFromTrack:
        case UiIntentType::OpenFxEditor:
        case UiIntentType::SetFxParam:
        case UiIntentType::EnginePlayTrack:
        case UiIntentType::EngineStopTrack:
        case UiIntentType::EngineSetQuant:
        case UiIntentType::EngineSetBpm:
        case UiIntentType::EngineSetTrackSpeed:
        default:
            return false;
    }
}

void SamplerApplication::dispatchIntent_(const UiIntent& intent) {
    UiIntent undoIntent{};
    const bool isUndoable = buildUndoIntent_(intent, undoIntent);
    const bool changed = applyIntent_(intent);
    if (!changed) {
        return;
    }
    if (!isUndoable) {
        // Любое новое действие сбрасывает redo-цепочку,
        // даже если его нельзя откатить (иначе redo становится несогласованным).
        redoStack_.clear();
        return;
    }

    if (undoStack_.size() >= kHistoryDepth_) {
        undoStack_.pop_front();
    }
    undoStack_.push_back(UndoEntry{undoIntent, intent});
    redoStack_.clear();
}

bool SamplerApplication::undoLast_() {
    if (undoStack_.empty()) {
        return false;
    }
    const UndoEntry entry = undoStack_.back();
    undoStack_.pop_back();

    const bool changed = applyIntent_(entry.undoIntent);
    if (redoStack_.size() >= kHistoryDepth_) {
        redoStack_.pop_front();
    }
    redoStack_.push_back(entry);
    return changed;
}

bool SamplerApplication::redoLast_() {
    if (redoStack_.empty()) {
        return false;
    }
    const UndoEntry entry = redoStack_.back();
    redoStack_.pop_back();

    const bool changed = applyIntent_(entry.redoIntent);
    if (undoStack_.size() >= kHistoryDepth_) {
        undoStack_.pop_front();
    }
    undoStack_.push_back(entry);
    return changed;
}

void SamplerApplication::clearHistory_() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}

void SamplerApplication::refreshTrackViewState_(uint8_t track) noexcept {
    if (tracksCtl_.empty()) {
        return;
    }
    const uint8_t t = clampUiTrack_(track);
    UiTrackStateView& tr = tracksCtl_[t];
    if (tr.clipName.empty()) {
        tr.state = UiTrackState::Empty;
    } else if (trCtl_.playing) {
        tr.state = UiTrackState::Playing;
    } else {
        tr.state = UiTrackState::Stopped;
    }
}

void SamplerApplication::refreshAllTrackViewStates_() noexcept {
    for (std::size_t i = 0; i < tracksCtl_.size(); ++i) {
        refreshTrackViewState_(static_cast<uint8_t>(i));
    }
}

void SamplerApplication::renderUiOnce_() {
    // RT telemetry читается из engine слоя без блокировок UI state.
    const SamplerEngineTelemetry telemetryRt = engine_.telemetryAndResetOverflow();
    UiRuntimeTelemetryView telemetry{};
    telemetry.totalCallbacks = telemetryRt.totalCallbacks;
    telemetry.xruns = telemetryRt.xruns;
    telemetry.rtQueueOverflow = telemetryRt.rtQueueOverflow;
    telemetry.blockFrames = telemetryRt.blockFrames;

    const UiState state = uiComposer_.compose(uiStore_.snapshot(), telemetry);

    // Рендер сцены держим под sceneMutex_, чтобы не гоняться с control-потоком.
    UiTextBuffer sceneText{};
    UiScene scene = UiScene::Tracks;
    bool hasSceneFrame = false;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        scene = sceneHost_.scene();
        hasSceneFrame = sceneHost_.renderActive(sceneText, state);
    }

    std::string frame;
    if (hasSceneFrame) {
        // Преобразуем line-buffer в единый кадр для backend renderer.
        for (const std::string& line : sceneText.lines) {
            frame += line;
            frame.push_back('\n');
        }
    }
    const bool showHeaderOverlay = (scene == UiScene::Tracks);
    io_.render(state, frame, showHeaderOverlay);
}

bool SamplerApplication::handleInputEvent_(UiInputAction action) {
    if (action == UiInputAction::Quit) {
        stopUi_.store(true, std::memory_order_release);
        return false;
    }

    // Глобальные hotkey undo/redo (доступны в любой сцене).
    // F2/F9 резервируются под аппаратные кнопки, ActionUndo/ActionRedo —
    // под универсальный слой pointer-команд.
    if (action == UiInputAction::ActionUndo || action == UiInputAction::F2) {
        (void)undoLast_();
        return true;
    }
    if (action == UiInputAction::ActionRedo || action == UiInputAction::F9) {
        (void)redoLast_();
        return true;
    }

    // ВАЖНО: snapshot берем до sceneMutex_, чтобы избежать lock inversion с UiStateStore.
    const UiState widgetState = uiStore_.snapshot();
    WidgetOutput widgetOut{};
    bool activeTrackChanged = false;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        widgetOut = sceneHost_.handleInput(action, widgetState);
        if (action == UiInputAction::SelectPrevTrack || action == UiInputAction::SelectNextTrack) {
            trCtl_.activeTrack = clampUiTrack_(sceneHost_.nav().selectedTrack);
            activeTrackChanged = true;
        }
    }
    if (activeTrackChanged) {
        uiStore_.setTransport(trCtl_);
    }
    for (const UiIntent& intent : widgetOut.intents) {
        dispatchIntent_(intent);
    }

    // Если активен Manager, scene-local логика уже обработана виджетом.
    bool managerScene = false;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        managerScene = (sceneHost_.scene() == UiScene::Manager);
    }
    if (managerScene) {
        return true;
    }

    switch (action) {
        case UiInputAction::SelectPrevTrack:
        case UiInputAction::SelectNextTrack:
        case UiInputAction::TrackPagePrev:
        case UiInputAction::TrackPageNext:
        case UiInputAction::OpenManager:
        case UiInputAction::BackScene:
        case UiInputAction::ListUp:
        case UiInputAction::ListDown:
        case UiInputAction::ListEnter:
        case UiInputAction::ListParent:
        case UiInputAction::PreviewPlay:
        case UiInputAction::PreviewAutoToggle:
            break;
        case UiInputAction::PlayActiveTrack: {
            trCtl_.playing = true;
            engine_.setTransportPlaying(true);
            refreshAllTrackViewStates_();
            for (std::size_t i = 0; i < tracksCtl_.size(); ++i) {
                uiStore_.setTrack(i, tracksCtl_[i]);
            }
            uiStore_.setTransport(trCtl_);
        } break;
        case UiInputAction::StopActiveTrack: {
            trCtl_.playing = false;
            engine_.setTransportPlaying(false);
            refreshAllTrackViewStates_();
            for (std::size_t i = 0; i < tracksCtl_.size(); ++i) {
                uiStore_.setTrack(i, tracksCtl_[i]);
            }
            uiStore_.setTransport(trCtl_);
        } break;
        case UiInputAction::UnmuteActiveTrack: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].muted = false;
            (void)engine_.setTrackMuted(t, false);
            refreshTrackViewState_(t);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::MuteActiveTrack: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].muted = true;
            (void)engine_.setTrackMuted(t, true);
            refreshTrackViewState_(t);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::MuteToggleActiveTrack: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].muted = !tracksCtl_[t].muted;
            (void)engine_.setTrackMuted(t, tracksCtl_[t].muted);
            refreshTrackViewState_(t);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::ArmToggleActiveTrack: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].armed = !tracksCtl_[t].armed;
            (void)engine_.setTrackArmed(t, tracksCtl_[t].armed);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::TrackSpeedUp: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].stretchRatio = std::clamp(tracksCtl_[t].stretchRatio + 0.05f, 0.25f, 4.0f);
            (void)engine_.setTrackSpeed(t, tracksCtl_[t].stretchRatio);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::TrackSpeedDown: {
            if (tracksCtl_.empty()) {
                break;
            }
            const uint8_t t = clampUiTrack_(trCtl_.activeTrack);
            tracksCtl_[t].stretchRatio = std::clamp(tracksCtl_[t].stretchRatio - 0.05f, 0.25f, 4.0f);
            (void)engine_.setTrackSpeed(t, tracksCtl_[t].stretchRatio);
            uiStore_.setTrack(t, tracksCtl_[t]);
        } break;
        case UiInputAction::QuantNone:
            // Quantize меняется и в engine, и в UI модели.
            engine_.setQuantize(QuantizeMode::None);
            trCtl_.quant = QuantizeMode::None;
            uiStore_.setTransport(trCtl_);
            break;
        case UiInputAction::QuantBeat:
            engine_.setQuantize(QuantizeMode::Beat);
            trCtl_.quant = QuantizeMode::Beat;
            uiStore_.setTransport(trCtl_);
            break;
        case UiInputAction::QuantBar:
            engine_.setQuantize(QuantizeMode::Bar);
            trCtl_.quant = QuantizeMode::Bar;
            uiStore_.setTransport(trCtl_);
            break;
        case UiInputAction::BpmUp:
            // BPM clamp на control-стороне, затем отправка в engine.
            trCtl_.bpm = std::clamp(trCtl_.bpm + 1.0f, 20.0f, 300.0f);
            engine_.setTempo(trCtl_.bpm);
            uiStore_.setTransport(trCtl_);
            break;
        case UiInputAction::BpmDown:
            trCtl_.bpm = std::clamp(trCtl_.bpm - 1.0f, 20.0f, 300.0f);
            engine_.setTempo(trCtl_.bpm);
            uiStore_.setTransport(trCtl_);
            break;
        case UiInputAction::None:
        case UiInputAction::Quit:
        default:
            break;
    }

    return true;
}

} // namespace avantgarde
