#include "app/SamplerApplication.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#include "contracts/IUiGestureInput.h"
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
            UiGestureEvent ev{};
            if (!io_.readNextInputEvent(ev)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!handleGesture_(ev.action)) {
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

bool SamplerApplication::handleGesture_(UiGesture action) {
    if (action == UiGesture::Quit) {
        stopUi_.store(true, std::memory_order_release);
        return false;
    }

    // Глобальные hotkey undo/redo (доступны в любой сцене).
    // F2/F9 резервируются под аппаратные кнопки, ActionUndo/ActionRedo —
    // под универсальный слой pointer-команд.
    if (action == UiGesture::ActionUndo || action == UiGesture::F2) {
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_};
        (void)history_.undo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
        return true;
    }
    if (action == UiGesture::ActionRedo || action == UiGesture::F9) {
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_};
        (void)history_.redo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
        return true;
    }

    // ВАЖНО: snapshot берем до sceneMutex_, чтобы избежать lock inversion с UiStateStore.
    const UiState widgetState = uiStore_.snapshot();
    WidgetOutput widgetOut{};
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        widgetOut = sceneHost_.handleGesture(action, widgetState);
    }
    // Пакет scene-intent'ов от одного input события пишем как одну транзакцию.
    const bool txOpened = !widgetOut.intents.empty() && history_.begin();
    for (const UiIntent& intent : widgetOut.intents) {
        dispatchWidgetIntent_(intent);
    }
    if (txOpened) {
        (void)history_.commit();
    }
    return true;
}

} // namespace avantgarde
