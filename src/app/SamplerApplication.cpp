#include "app/SamplerApplication.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#include "contracts/IUiInput.h"
#include "contracts/ids.h"
#include "service/ui/UiWidgetFactory.h"

namespace avantgarde {

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
    history_.clear();

    // 2) Сборка scene-графа (каждая функциональная сцена = виджет).
    UiWidgetFactory widgetFactory(
        UiWidgetFactoryOptions{
            .frameWidth = config.gbTextWidth,
            .tracksHeaderTitle = "AVANTGARDE",
        });
    (void)sceneHost_.registerWidget(UiScene::Tracks, widgetFactory.create(UiScene::Tracks));
    (void)sceneHost_.registerWidget(UiScene::Manager, widgetFactory.create(UiScene::Manager));
    (void)sceneHost_.registerWidget(UiScene::FxList, widgetFactory.create(UiScene::FxList));
    (void)sceneHost_.registerWidget(UiScene::FxEditor, widgetFactory.create(UiScene::FxEditor));
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

void SamplerApplication::dispatchWidgetIntent_(const UiIntent& intent) {
    UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_};

    UiIntent undoIntent{};
    const bool undoable = intentApplier_.buildUndoIntent(intent, trCtl_, tracksCtl_, undoIntent);
    const bool changed = intentApplier_.apply(intent, ctx);
    if (!changed) {
        return;
    }
    if (!undoable) {
        // Новое изменение всегда обрезает redo-ветку,
        // даже если само действие не поддерживает undo.
        history_.clearRedo();
        return;
    }
    HistoryTransactionManager::Change change{undoIntent, intent};
    if (history_.inTransaction()) {
        (void)history_.record(std::move(change));
    } else {
        (void)history_.pushAtomic(std::move(change));
    }
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
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_};
        (void)history_.undo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
        return true;
    }
    if (action == UiInputAction::ActionRedo || action == UiInputAction::F9) {
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_};
        (void)history_.redo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
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
    // Пакет scene-intent'ов от одного input события пишем как одну транзакцию.
    const bool txOpened = !widgetOut.intents.empty() && history_.begin();
    for (const UiIntent& intent : widgetOut.intents) {
        dispatchWidgetIntent_(intent);
    }
    if (txOpened) {
        (void)history_.commit();
    }

    // Если действие было обработано scene/widget слоем,
    // не даем ему "протечь" в legacy hotkey switch ниже.
    if (widgetOut.handled) {
        return true;
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
