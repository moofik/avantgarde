#include "app/SamplerApplication.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

#include "app/AppDiagnostics.h"
#include "contracts/IUiGestureInput.h"
#include "contracts/ids.h"
#include "service/ui/UiWidgetFactory.h"

namespace avantgarde {

SamplerApplication::~SamplerApplication() {
    // Безопасное завершение всех циклов/потоков в любом сценарии выхода.
    AppDiagnostics::log(AppLogLevel::Info, "SamplerApplication dtor: shutdown begin");
    stopUi_.store(true, std::memory_order_release);
    if (controlThread_.joinable()) {
        controlThread_.join();
    }
    engine_.stop();
    AppDiagnostics::log(AppLogLevel::Info, "SamplerApplication dtor: shutdown complete");
}

int SamplerApplication::run(const SamplerAppConfig& config) {
    AppDiagnostics::logf(AppLogLevel::Info,
                         "run begin: tracks=%u startupClips=%zu",
                         static_cast<unsigned>(config.engine.trackCount),
                         config.startupClipLoads.size());
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
        tracksCtl_[t].clipPath = load.path;
        tracksCtl_[t].muted = false;
        tracksCtl_[t].armed = false;
        tracksCtl_[t].loop = true;
        tracksCtl_[t].playbackProfile = UiTrackPlaybackProfile::Loop;
        tracksCtl_[t].trimStart01 = 0.0f;
        tracksCtl_[t].trimEnd01 = 1.0f;
    }

    refreshAllTrackViewStates_();

    // Публикуем начальный снапшот в UI store.
    UiState initialState{};
    initialState.transport = trCtl_;
    initialState.tracks = tracksCtl_;
    initialState.pattern = bootstrap.pattern;
    uiStore_.setState(initialState);
    history_.clear();

    // 2) Сборка scene-графа (каждая функциональная сцена = виджет).
    try {
        UiWidgetFactory widgetFactory(
            UiWidgetFactoryOptions{
                .frameWidth = 60U,
                .tracksHeaderTitle = "AVANTGARDE",
            });
        (void)sceneHost_.registerWidget(UiScene::Tracks, widgetFactory.create(UiScene::Tracks));
        (void)sceneHost_.registerWidget(UiScene::TrackContext, widgetFactory.create(UiScene::TrackContext));
        (void)sceneHost_.registerWidget(UiScene::SampleEdit, widgetFactory.create(UiScene::SampleEdit));
        (void)sceneHost_.registerWidget(UiScene::Manager, widgetFactory.create(UiScene::Manager));
        (void)sceneHost_.registerWidget(UiScene::FxList, widgetFactory.create(UiScene::FxList));
        (void)sceneHost_.registerWidget(UiScene::FxEditor, widgetFactory.create(UiScene::FxEditor));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[APP][INIT][ERROR] %s\n", ex.what());
        return 4;
    }
    sceneHost_.setScene(UiScene::Tracks);
    sceneHost_.nav().selectedTrack = trCtl_.activeTrack;
    sceneHost_.nav().trackPage = static_cast<uint16_t>(trCtl_.activeTrack / 2U);

    // 3) Запуск аудио.
    if (!engine_.start(error)) {
        std::printf("%s\n", error.c_str());
        return 3;
    }

    // 4) Запуск control-потока (обработка input -> intents).
    stopUi_.store(false, std::memory_order_release);

    controlThread_ = std::thread([this]() {
        try {
            while (!stopUi_.load(std::memory_order_acquire)) {
                if (engine_.processPendingPatternSwitches()) {
                    syncPatternStateToUi_();
                }

                UiGestureEvent ev{};
                if (!io_.readNextInputEvent(ev)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (!handleGesture_(ev)) {
                    break;
                }
                syncPatternStateToUi_();
            }
            // Гарантированно гасим preview-голос при завершении control loop.
            engine_.previewStop();
        } catch (const std::exception& ex) {
            AppDiagnostics::logf(AppLogLevel::Fatal, "control thread exception: %s", ex.what());
            stopUi_.store(true, std::memory_order_release);
        } catch (...) {
            AppDiagnostics::log(AppLogLevel::Fatal, "control thread exception: unknown");
            stopUi_.store(true, std::memory_order_release);
        }
    });

    // 5) Главный цикл main thread: pump событий окна + рендер кадра.
    while (!stopUi_.load(std::memory_order_acquire)) {
        try {
            if (io_.readWindowEvents()) {
                renderUiOnce_();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch (const std::exception& ex) {
            AppDiagnostics::logf(AppLogLevel::Fatal, "main loop exception: %s", ex.what());
            stopUi_.store(true, std::memory_order_release);
            break;
        } catch (...) {
            AppDiagnostics::log(AppLogLevel::Fatal, "main loop exception: unknown");
            stopUi_.store(true, std::memory_order_release);
            break;
        }
    }

    // 6) Аккуратный stop/join.
    stopUi_.store(true, std::memory_order_release);
    if (controlThread_.joinable()) {
        controlThread_.join();
    }
    engine_.stop();
    AppDiagnostics::log(AppLogLevel::Info, "run complete");
    return 0;
}

uint8_t SamplerApplication::clampUiTrack_(uint8_t track) const noexcept {
    if (tracksCtl_.empty()) {
        return 0;
    }
    return (track >= tracksCtl_.size()) ? static_cast<uint8_t>(tracksCtl_.size() - 1) : track;
}

void SamplerApplication::dispatchWidgetIntent_(const UiIntent& intent) {
    UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav()};

    UiIntent undoIntent{};
    const bool undoable = intentApplier_.buildUndoIntent(intent, trCtl_, tracksCtl_, undoIntent);
    bool changed = false;
    if (intent.type == UiIntentType::OpenScene ||
        intent.type == UiIntentType::Back) {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        changed = intentApplier_.apply(intent, ctx);
    } else {
        changed = intentApplier_.apply(intent, ctx);
    }
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
    UiPreparedLayout prepared{};
    bool hasSceneFrame = false;
    UiScene activeScene = UiScene::Tracks;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        activeScene = sceneHost_.scene();
        try {
            hasSceneFrame = sceneHost_.buildPreparedActive(prepared, state);
        } catch (const std::exception& ex) {
            static auto lastLogAt = std::chrono::steady_clock::time_point{};
            const auto now = std::chrono::steady_clock::now();
            if (lastLogAt.time_since_epoch().count() == 0 ||
                now - lastLogAt > std::chrono::seconds(2)) {
                AppDiagnostics::logf(AppLogLevel::Error,
                                     "buildPreparedActive failed scene=%u: %s",
                                     static_cast<unsigned>(activeScene),
                                     ex.what());
                lastLogAt = now;
            }
            hasSceneFrame = false;
        }
    }

    try {
        io_.render(state, hasSceneFrame ? &prepared : nullptr);
    } catch (const std::exception& ex) {
        AppDiagnostics::logf(AppLogLevel::Error, "io.render failed: %s", ex.what());
    } catch (...) {
        AppDiagnostics::log(AppLogLevel::Error, "io.render failed: unknown");
    }
}

bool SamplerApplication::handleGesture_(const UiGestureEvent& ev) {
    UiGesture action = ev.action;
    if (action == UiGesture::Quit) {
        stopUi_.store(true, std::memory_order_release);
        return false;
    }

    // Direct-select не требует apply:
    // 1..4 -> SetActiveTrack, Shift+1..4 -> SwitchPatternSet.
    if (action == UiGesture::SelectTrackDirect) {
        if (tracksCtl_.empty()) {
            return true;
        }
        const int raw = static_cast<int>(ev.value) - 1;
        const int maxTrack = static_cast<int>(tracksCtl_.size()) - 1;
        const uint8_t targetTrack = static_cast<uint8_t>(std::clamp(raw, 0, std::max(0, maxTrack)));
        {
            std::lock_guard<std::mutex> lock(sceneMutex_);
            sceneHost_.nav().selectedTrack = targetTrack;
            sceneHost_.nav().trackPage = targetTrack;
        }
        UiIntent it{};
        it.type = UiIntentType::SetActiveTrack;
        it.track = targetTrack;
        dispatchWidgetIntent_(it);
        return true;
    }
    if (action == UiGesture::SelectPatternDirect) {
        UiIntent it{};
        it.type = UiIntentType::SwitchPatternSet;
        it.value = static_cast<float>(std::max<int16_t>(1, ev.value));
        dispatchWidgetIntent_(it);
        syncPatternStateToUi_();
        return true;
    }

    // Глобальные hotkey undo/redo (доступны в любой сцене).
    // F2/F9 резервируются под аппаратные кнопки, ActionUndo/ActionRedo —
    // под универсальный слой pointer-команд.
    if (action == UiGesture::ActionUndo || action == UiGesture::F2) {
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav()};
        (void)history_.undo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
        return true;
    }
    if (action == UiGesture::ActionRedo || action == UiGesture::F9) {
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav()};
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
        try {
            widgetOut = sceneHost_.handleGesture(action, widgetState);
        } catch (const std::exception& ex) {
            AppDiagnostics::logf(AppLogLevel::Error, "sceneHost.handleGesture failed: %s", ex.what());
            return true;
        } catch (...) {
            AppDiagnostics::log(AppLogLevel::Error, "sceneHost.handleGesture failed: unknown");
            return true;
        }
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

void SamplerApplication::syncPatternStateToUi_() {
    (void)engine_.syncUiCache(trCtl_, tracksCtl_);
    UiState merged = uiStore_.snapshot();
    merged.transport = trCtl_;
    merged.tracks = tracksCtl_;
    merged.pattern = engine_.patternUiState();
    uiStore_.setState(merged);
}

} // namespace avantgarde
