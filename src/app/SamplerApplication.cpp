#include "app/SamplerApplication.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "app/AppDiagnostics.h"
#include "contracts/FxRegistry.h"
#include "contracts/IUiGestureInput.h"
#include "contracts/ids.h"
#include "service/sequencer/SequencerRecordRegistry.h"
#include "service/sequencer/SequencerDispatchPlanner.h"
#include "service/ui/UiWidgetFactory.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace avantgarde {
namespace {

TransportRtSnapshot makeTransportSnapshot(const UiTransportState& tr, uint64_t sampleTime) noexcept {
    TransportRtSnapshot snap{};
    snap.playing = tr.playing;
    snap.tsNum = tr.tsNum;
    snap.tsDen = tr.tsDen;
    snap.ppq = 96;
    snap.bpm = tr.bpm;
    snap.quant = tr.quant;
    snap.swing = 0.0f;
    snap.sampleTime = sampleTime;
    return snap;
}

SequencerQuantize sequencerQuantFromTransportQuant(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return SequencerQuantize::None;
        case QuantizeMode::Beat: return SequencerQuantize::Quarter;
        case QuantizeMode::Bar: return SequencerQuantize::Bar;
        default: return SequencerQuantize::Quarter;
    }
}

SequencerTick sampleToTick(uint64_t sampleTime, float bpm, uint16_t ppq, double sampleRateHz) noexcept {
    const double safeBpm = (std::isfinite(bpm) && bpm > 0.0f) ? static_cast<double>(bpm) : 120.0;
    const double safeSampleRate = (std::isfinite(sampleRateHz) && sampleRateHz > 1.0) ? sampleRateHz : 48000.0;
    const double ticks = (static_cast<double>(sampleTime) * safeBpm * static_cast<double>(ppq)) /
                         (60.0 * safeSampleRate);
    return static_cast<SequencerTick>(std::max<double>(0.0, std::llround(ticks)));
}

uint64_t tickToSample(SequencerTick tick, float bpm, uint16_t ppq, double sampleRateHz) noexcept {
    const double safeBpm = (std::isfinite(bpm) && bpm > 0.0f) ? static_cast<double>(bpm) : 120.0;
    const double safeSampleRate = (std::isfinite(sampleRateHz) && sampleRateHz > 1.0) ? sampleRateHz : 48000.0;
    const double samples = (static_cast<double>(tick) * 60.0 * safeSampleRate) /
                           (safeBpm * static_cast<double>(std::max<uint16_t>(1U, ppq)));
    return static_cast<uint64_t>(std::max<double>(0.0, std::llround(samples)));
}

SequencerTick quantumTicks(SequencerQuantize quant,
                           uint16_t ppq,
                           uint8_t tsNum,
                           uint8_t tsDen) noexcept {
    const uint32_t safePpq = std::max<uint32_t>(1U, ppq);
    switch (quant) {
        case SequencerQuantize::None:
            return 1U;
        case SequencerQuantize::Sixteenth:
            return static_cast<SequencerTick>(std::max<uint32_t>(1U, safePpq / 4U));
        case SequencerQuantize::Eighth:
            return static_cast<SequencerTick>(std::max<uint32_t>(1U, safePpq / 2U));
        case SequencerQuantize::Quarter:
            return static_cast<SequencerTick>(safePpq);
        case SequencerQuantize::Bar: {
            const uint32_t safeNum = std::max<uint32_t>(1U, tsNum);
            const uint32_t safeDen = std::max<uint32_t>(1U, tsDen);
            const double beatsPerBar = (static_cast<double>(safeNum) * 4.0) / static_cast<double>(safeDen);
            return static_cast<SequencerTick>(
                std::max<double>(1.0, std::llround(static_cast<double>(safePpq) * beatsPerBar)));
        }
        default:
            return static_cast<SequencerTick>(safePpq);
    }
}

SequencerTick quantizeForwardTick(SequencerTick tick,
                                  SequencerQuantize quant,
                                  uint16_t ppq,
                                  uint8_t tsNum,
                                  uint8_t tsDen) noexcept {
    if (quant == SequencerQuantize::None) {
        return tick;
    }
    const SequencerTick q = std::max<SequencerTick>(1U, quantumTicks(quant, ppq, tsNum, tsDen));
    const SequencerTick rem = tick % q;
    if (rem == 0U) {
        return tick;
    }
    return tick + (q - rem);
}

double currentRssMiB() noexcept {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t kr =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
#endif
    return -1.0;
}

uint64_t steadyNowMs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool mapIntentToAutomationTarget(const UiIntent& intent, SequencerParamTarget& outTarget) noexcept {
    return SequencerRecordRegistry::mapAutomationTarget(intent, outTarget);
}

bool mapIntentToEvent(const UiIntent& intent, uint64_t sampleTime, EventLaneEvent& outEvent) noexcept {
    return SequencerRecordRegistry::mapEvent(intent, sampleTime, outEvent);
}

SequencerTick normalizePatternTick(SequencerTick tick, SequencerTick lengthTicks) noexcept {
    const SequencerTick len = std::max<SequencerTick>(1U, lengthTicks);
    return tick % len;
}

bool isTickDueInWindow(SequencerTick phaseTick,
                       SequencerTick prevPhase,
                       SequencerTick nowPhase,
                       bool wrapped,
                       bool fullCycle) noexcept {
    if (fullCycle) {
        return true;
    }
    if (!wrapped) {
        return phaseTick > prevPhase && phaseTick <= nowPhase;
    }
    return (phaseTick > prevPhase) || (phaseTick <= nowPhase);
}

uint32_t relativePhaseOrder(SequencerTick phaseTick,
                            SequencerTick prevPhase,
                            SequencerTick lengthTicks) noexcept {
    const SequencerTick len = std::max<SequencerTick>(1U, lengthTicks);
    const SequencerTick start = (prevPhase + 1U) % len;
    return static_cast<uint32_t>((phaseTick + len - start) % len);
}

} // namespace

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
    sampleRateHz_ =
        (std::isfinite(config.engine.sampleRate) && config.engine.sampleRate > 1.0)
            ? config.engine.sampleRate
            : 48000.0;
    AppDiagnostics::log(AppLogLevel::Info, "engine init ok");
    if (!io_.init(config.io, error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    AppDiagnostics::log(AppLogLevel::Info, "io init ok");

    {
        std::string hudError{};
        if (!hudLayer_.loadConfigFromFile("assets/ui/hud.json", hudError)) {
            AppDiagnostics::logf(AppLogLevel::Warn, "hud config load failed: %s", hudError.c_str());
        } else {
            AppDiagnostics::log(AppLogLevel::Info, "hud config loaded");
        }
    }

    trCtl_ = bootstrap.transport;
    tracksCtl_ = bootstrap.tracks;
    trCtl_.activeTrack = clampUiTrack_(trCtl_.activeTrack);
    trCtl_.recordEnabled = false;
    recordEnabled_ = false;
    sequencerCursorInitialized_ = false;
    sequencerCursorSample_ = 0;
    sequencerByPattern_.clear();
    sequencerPatternId_ =
        (bootstrap.pattern.activeId == kInvalidPatternId) ? static_cast<PatternId>(1U) : bootstrap.pattern.activeId;
    (void)ensureSequencerPattern_(sequencerPatternId_);

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
    sequencerParamMirror_.clear();
    pendingLoopResets_.clear();
    for (std::size_t i = 0; i < tracksCtl_.size(); ++i) {
        const int16_t track = static_cast<int16_t>(i);
        SequencerParamTarget speed{};
        speed.track = track;
        speed.slot = kRtSlotTrackParams;
        speed.param = toParamIndex(TrackParamId::PlaybackInc);
        sequencerParamMirror_[makeSequencerTargetKey_(speed)] = tracksCtl_[i].stretchRatio;

        SequencerParamTarget gain{};
        gain.track = track;
        gain.slot = kRtSlotTrackParams;
        gain.param = toParamIndex(TrackParamId::Gain01);
        sequencerParamMirror_[makeSequencerTargetKey_(gain)] = tracksCtl_[i].gain01;

        SequencerParamTarget start{};
        start.track = track;
        start.slot = kRtSlotTrackParams;
        start.param = toParamIndex(TrackParamId::StartNorm);
        sequencerParamMirror_[makeSequencerTargetKey_(start)] = tracksCtl_[i].trimStart01;

        SequencerParamTarget end{};
        end.track = track;
        end.slot = kRtSlotTrackParams;
        end.param = toParamIndex(TrackParamId::EndNorm);
        sequencerParamMirror_[makeSequencerTargetKey_(end)] = tracksCtl_[i].trimEnd01;
    }

    // Публикуем начальный снапшот в UI store.
    UiState initialState{};
    initialState.transport = trCtl_;
    initialState.tracks = tracksCtl_;
    initialState.pattern = bootstrap.pattern;
    syncSequencerStateToUi_(initialState);
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
        (void)sceneHost_.registerWidget(UiScene::SampleContextMenu, widgetFactory.create(UiScene::SampleContextMenu));
        (void)sceneHost_.registerWidget(UiScene::Manager, widgetFactory.create(UiScene::Manager));
        (void)sceneHost_.registerWidget(UiScene::FxList, widgetFactory.create(UiScene::FxList));
        (void)sceneHost_.registerWidget(UiScene::FxEditor, widgetFactory.create(UiScene::FxEditor));
        (void)sceneHost_.registerWidget(UiScene::Sequencer, widgetFactory.create(UiScene::Sequencer));
        (void)sceneHost_.registerWidget(UiScene::SequencerLane, widgetFactory.create(UiScene::SequencerLane));
        (void)sceneHost_.registerWidget(UiScene::PatternEdit, widgetFactory.create(UiScene::PatternEdit));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[APP][INIT][ERROR] %s\n", ex.what());
        return 4;
    }
    AppDiagnostics::log(AppLogLevel::Info, "ui widgets init ok");
    sceneHost_.setScene(UiScene::Tracks);
    sceneHost_.nav().selectedTrack = trCtl_.activeTrack;
    sceneHost_.nav().trackPage = static_cast<uint16_t>(trCtl_.activeTrack / 2U);

    // 3) Запуск аудио.
    if (!engine_.start(error)) {
        std::printf("%s\n", error.c_str());
        return 3;
    }
    AppDiagnostics::log(AppLogLevel::Info, "engine start ok");

    // 4) Запуск control-потока (обработка input -> intents).
    stopUi_.store(false, std::memory_order_release);

    controlThread_ = std::thread([this]() {
        try {
            auto nextUiRefresh = std::chrono::steady_clock::now();
            auto drainUiGestures = [this]() -> bool {
                UiGestureEvent ev{};
                while (inputInterpreter_.poll(ev)) {
                    if (!handleGesture_(ev)) {
                        return false;
                    }
                    syncPatternStateToUi_();
                    uiDirty_.store(true, std::memory_order_release);
                }
                return true;
            };
            while (!stopUi_.load(std::memory_order_acquire)) {
                bool stateChanged = false;
                if (engine_.processPendingPatternSwitches()) {
                    stateChanged = true;
                }

                // Держим control-кэш синхронизированным с live transport/sampleTime,
                // чтобы sequencer playback работал по актуальному времени.
                (void)engine_.syncUiCache(trCtl_, tracksCtl_);
                trCtl_.recordEnabled = recordEnabled_;
                // Preview-флаг в nav — UI-подсказка для toggle-жеста.
                // Синхронизируем его с фактическим состоянием hidden preview-voice,
                // чтобы после естественного окончания one-shot не оставался "залипший" стоп-режим.
                {
                    const std::lock_guard<std::mutex> lock(sceneMutex_);
                    if (!trCtl_.previewPlaying) {
                        sceneHost_.nav().previewPlaying = false;
                    }
                }

                if (processSequencerPlayback_()) {
                    stateChanged = true;
                }
                bool forceUiRefresh = false;
                {
                    const std::lock_guard<std::mutex> lock(sceneMutex_);
                    const UiScene scene = sceneHost_.nav().scene;
                    const bool needsLiveUiRefresh =
                        (scene == UiScene::Tracks) ||
                        (scene == UiScene::SampleEdit) ||
                        (scene == UiScene::Sequencer) ||
                        (scene == UiScene::SequencerLane);
                    if (needsLiveUiRefresh) {
                        const auto now = std::chrono::steady_clock::now();
                        if (now >= nextUiRefresh) {
                            forceUiRefresh = true;
                            nextUiRefresh = now + std::chrono::milliseconds(33);
                        }
                    }
                }
                if (stateChanged || forceUiRefresh) {
                    syncPatternStateToUi_();
                    uiDirty_.store(true, std::memory_order_release);
                }

                // Tick hold-детектора: long-press должен срабатывать по таймеру,
                // а не только по KeyUp.
                inputInterpreter_.tick(steadyNowMs());
                if (!drainUiGestures()) {
                    break;
                }

                PrimitiveInputEvent primitive{};
                if (!io_.readNextInputEvent(primitive)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }

                const UiScene activeScene = [&]() {
                    const std::lock_guard<std::mutex> lock(sceneMutex_);
                    return sceneHost_.nav().scene;
                }();
                inputInterpreter_.onPrimitiveEvent(primitive, activeScene, steadyNowMs());
                if (!drainUiGestures()) {
                    break;
                }
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
    auto nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto nextRenderAt = std::chrono::steady_clock::now();
    constexpr auto kUiFrameInterval = std::chrono::milliseconds(16); // ~60 FPS cap
    while (!stopUi_.load(std::memory_order_acquire)) {
        try {
            const auto nowHeartbeat = std::chrono::steady_clock::now();
            if (nowHeartbeat >= nextHeartbeat) {
                const UiState snap = uiStore_.snapshot();
                const double rssMiB = currentRssMiB();
                uint32_t sceneRaw = 0U;
                {
                    const std::lock_guard<std::mutex> lock(sceneMutex_);
                    sceneRaw = static_cast<uint32_t>(sceneHost_.nav().scene);
                }
                AppDiagnostics::logf(
                    AppLogLevel::Info,
                    "heartbeat: sampleTime=%llu playing=%d rec=%d scene=%u rssMiB=%.1f",
                    static_cast<unsigned long long>(snap.transport.sampleTime),
                    snap.transport.playing ? 1 : 0,
                    snap.transport.recordEnabled ? 1 : 0,
                    static_cast<unsigned>(sceneRaw),
                    rssMiB);
                nextHeartbeat = nowHeartbeat + std::chrono::seconds(5);
            }
            const bool hadWindowEvents = io_.readWindowEvents();
            if (hadWindowEvents) {
                uiDirty_.store(true, std::memory_order_release);
            }

            const auto now = std::chrono::steady_clock::now();
            const bool shouldRender =
                uiDirty_.load(std::memory_order_acquire) &&
                (now >= nextRenderAt);
            if (shouldRender) {
                renderUiOnce_();
                uiDirty_.store(false, std::memory_order_release);
                nextRenderAt = now + kUiFrameInterval;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(hadWindowEvents
                                                ? std::chrono::milliseconds(2)
                                                : std::chrono::milliseconds(8));
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

bool SamplerApplication::dispatchWidgetIntent_(const UiIntent& intent) {
    switch (intent.type) {
        case UiIntentType::SequencerSetLaneFocus:
        case UiIntentType::SequencerSetActiveLane:
        case UiIntentType::SequencerSetActiveObject:
        case UiIntentType::SequencerSetScrubTick:
        case UiIntentType::SequencerNudgeObjectTime:
        case UiIntentType::SequencerAdjustObjectValue:
        case UiIntentType::SequencerSetLoopMode:
        case UiIntentType::SequencerSetPatternLengthBars:
        case UiIntentType::SequencerSetQuant:
        case UiIntentType::SequencerSetZoom:
        case UiIntentType::SequencerSetTool:
        case UiIntentType::SequencerAddObjectAtCursor:
        case UiIntentType::SequencerDeleteSelectedObject:
        case UiIntentType::SequencerDeleteSelectedLane:
            return applySequencerIntent_(intent);
        default:
            break;
    }

    if (snapshotOrchestrator_.supports(intent)) {
        const UiNavState nav = [&]() {
            std::lock_guard<std::mutex> lock(sceneMutex_);
            return sceneHost_.nav();
        }();
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav(), &hudLayer_};
        const SnapshotIntentOrchestrator::Context snapshotCtx{
            .recordEnabled = recordEnabled_,
            .transportPlaying = trCtl_.playing,
            .nav = &nav,
            .tracks = &tracksCtl_,
            .fxParamMirror = &fxParamMirror_,
            .intentApplier = &intentApplier_,
            .applierContext = &ctx,
            .snapshotRecallDispatchFlag = &snapshotRecallDispatch_,
            .onIntentApplied = [this](const UiIntent& appliedIntent) {
                updateIntentMirrors_(appliedIntent);
            }
        };
        const SnapshotIntentOrchestrator::Result snapshot =
            snapshotOrchestrator_.dispatch(intent, snapshotCtx);
        if (!snapshot.handled) {
            return false;
        }

        // Только ручной trigger в REC+PLAY пишет SnapshotRecall в event lane.
        if (intent.type == UiIntentType::SnapshotTriggerSlot &&
            snapshot.recallRequested &&
            recordEnabled_ &&
            trCtl_.playing) {
            SequencerPatternData& seq = currentSequencerPattern_();
            const SequencerTick rawTick = sampleToTick(trCtl_.sampleTime, trCtl_.bpm, seq.ppq, sampleRateHz_);
            const SequencerTick quantTick =
                quantizeForwardTick(rawTick, seq.quant, seq.ppq, trCtl_.tsNum, trCtl_.tsDen);
            const SequencerTick localTick = normalizePatternTick(quantTick, seq.lengthTicks);
            const uint64_t quantSampleTime = tickToSample(localTick, trCtl_.bpm, seq.ppq, sampleRateHz_);

            EventLaneEvent laneEvent{};
            laneEvent.sampleTime = quantSampleTime;
            laneEvent.tick = localTick;
            laneEvent.op = EventLaneOp::SnapshotRecall;
            laneEvent.snapshotId = static_cast<uint16_t>(snapshot.recallSlot + 1U);
            laneEvent.payload = EventSnapshotRecallPayload{laneEvent.snapshotId};
            laneEvent.target.track = static_cast<int16_t>(snapshot.recallTrack);
            laneEvent.target.slot = static_cast<int16_t>(snapshot.recallFxSlot);
            (void)seq.events.addEvent(laneEvent);
        }
        return snapshot.changed;
    }

    // HUD-нотификации не меняют model/runtime state и не должны участвовать в undo/redo.
    if (intent.type == UiIntentType::HudNotify ||
        intent.type == UiIntentType::SnapshotCaptured ||
        intent.type == UiIntentType::SnapshotApplied) {
        UiIntentApplier::Context hudCtx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav(), &hudLayer_};
        (void)intentApplier_.apply(intent, hudCtx);
        return false;
    }

    UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav(), &hudLayer_};

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
        return false;
    }
    updateIntentMirrors_(intent);
    if (!undoable) {
        // Новое изменение всегда обрезает redo-ветку,
        // даже если само действие не поддерживает undo.
        history_.clearRedo();
        return true;
    }
    HistoryTransactionManager::Change change{undoIntent, intent};
    if (history_.inTransaction()) {
        (void)history_.record(std::move(change));
    } else {
        (void)history_.pushAtomic(std::move(change));
    }
    return true;
}

PatternId SamplerApplication::activePatternId_() const noexcept {
    const UiPatternState p = engine_.patternUiState();
    if (p.activeId != kInvalidPatternId) {
        return p.activeId;
    }
    if (sequencerPatternId_ != kInvalidPatternId) {
        return sequencerPatternId_;
    }
    return static_cast<PatternId>(1U);
}

SamplerApplication::SequencerPatternData& SamplerApplication::ensureSequencerPattern_(PatternId id) {
    const PatternId key = (id == kInvalidPatternId) ? static_cast<PatternId>(1U) : id;
    auto it = sequencerByPattern_.find(key);
    if (it != sequencerByPattern_.end()) {
        return it->second;
    }
    SequencerPatternData data{};
    data.quant = sequencerQuantFromTransportQuant(trCtl_.quant);
    const double beatsPerBar =
        (static_cast<double>(std::max<uint8_t>(1U, trCtl_.tsNum)) * 4.0) /
        static_cast<double>(std::max<uint8_t>(1U, trCtl_.tsDen));
    const double ticks = static_cast<double>(data.ppq) * beatsPerBar * static_cast<double>(data.lengthBars);
    data.lengthTicks = static_cast<SequencerTick>(std::max<double>(1.0, std::llround(ticks)));
    auto [ins, _] = sequencerByPattern_.emplace(key, std::move(data));
    return ins->second;
}

const SamplerApplication::SequencerPatternData* SamplerApplication::findSequencerPattern_(PatternId id) const noexcept {
    const PatternId key = (id == kInvalidPatternId) ? static_cast<PatternId>(1U) : id;
    const auto it = sequencerByPattern_.find(key);
    if (it == sequencerByPattern_.end()) {
        return nullptr;
    }
    return &it->second;
}

SamplerApplication::SequencerPatternData& SamplerApplication::currentSequencerPattern_() {
    sequencerPatternId_ = activePatternId_();
    return ensureSequencerPattern_(sequencerPatternId_);
}

const SamplerApplication::SequencerPatternData& SamplerApplication::currentSequencerPattern_() const {
    const PatternId active = activePatternId_();
    if (const SequencerPatternData* data = findSequencerPattern_(active)) {
        return *data;
    }
    static SequencerPatternData fallback{};
    return fallback;
}

void SamplerApplication::syncSequencerStateToUi_(UiState& merged) const {
    struct LaneRef {
        UiSequencerLaneKind kind{UiSequencerLaneKind::Event};
        EventLaneOp eventOp{EventLaneOp::SnapshotRecall};
        SequencerParamTarget target{};
    };
    auto sameTarget = [](const SequencerParamTarget& a, const SequencerParamTarget& b) noexcept {
        return a.track == b.track && a.slot == b.slot && a.module == b.module && a.param == b.param;
    };
    auto trackTitle = [&](int16_t track) {
        char buf[64]{};
        if (track < 0 || static_cast<std::size_t>(track) >= merged.tracks.size()) {
            std::snprintf(buf, sizeof(buf), "Track ?");
            return std::string(buf);
        }
        std::snprintf(buf, sizeof(buf), "Track %d", static_cast<int>(track + 1));
        return std::string(buf);
    };
    auto trackParamLabel = [](uint16_t paramIndex) {
        switch (static_cast<TrackParamId>(paramIndex)) {
            case TrackParamId::PlaybackInc: return std::string("Speed");
            case TrackParamId::Gain01: return std::string("Gain");
            case TrackParamId::StartNorm: return std::string("Start");
            case TrackParamId::EndNorm: return std::string("End");
            case TrackParamId::MuteEnabled: return std::string("Mute");
            case TrackParamId::ArmEnabled: return std::string("Arm");
            case TrackParamId::LoopEnabled: return std::string("Loop");
            case TrackParamId::PlaybackMode: return std::string("Mode");
            case TrackParamId::PlayheadNorm: return std::string("Playhead");
            default: {
                char buf[48]{};
                std::snprintf(buf, sizeof(buf), "Param %u", static_cast<unsigned>(paramIndex));
                return std::string(buf);
            }
        }
    };
    auto fxNameFor = [&](int16_t track, int16_t slot) {
        if (track >= 0 && slot >= 0 &&
            static_cast<std::size_t>(track) < merged.tracks.size()) {
            const UiTrackStateView& tr = merged.tracks[static_cast<std::size_t>(track)];
            if (static_cast<std::size_t>(slot) < tr.fxChainIds.size()) {
                const std::string& fxId = tr.fxChainIds[static_cast<std::size_t>(slot)];
                if (const FxDescriptor* d = FxRegistry::find(fxId)) {
                    return std::string(d->displayName);
                }
                if (!fxId.empty()) {
                    return fxId;
                }
            }
        }
        char fallback[64]{};
        std::snprintf(fallback, sizeof(fallback), "FX S%d", static_cast<int>(slot + 1));
        return std::string(fallback);
    };
    auto fxParamLabelFor = [&](int16_t track, int16_t slot, uint16_t paramIndex) {
        if (track >= 0 && slot >= 0 &&
            static_cast<std::size_t>(track) < merged.tracks.size()) {
            const UiTrackStateView& tr = merged.tracks[static_cast<std::size_t>(track)];
            if (static_cast<std::size_t>(slot) < tr.fxChainIds.size()) {
                const std::string& fxId = tr.fxChainIds[static_cast<std::size_t>(slot)];
                if (const FxDescriptor* d = FxRegistry::find(fxId)) {
                    for (std::size_t i = 0; i < d->paramCount; ++i) {
                        if (d->params[i].paramIndex == paramIndex) {
                            return std::string(d->params[i].label);
                        }
                    }
                }
            }
        }
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), "Param %u", static_cast<unsigned>(paramIndex));
        return std::string(buf);
    };
    auto makeLaneLabel = [&](const LaneRef& ref) {
        if (ref.kind == UiSequencerLaneKind::Automation) {
            const std::string track = trackTitle(ref.target.track);
            if (ref.target.slot == kRtSlotTrackParams) {
                return track + " - " + trackParamLabel(ref.target.param);
            }
            const std::string fx = fxNameFor(ref.target.track, ref.target.slot);
            const std::string param = fxParamLabelFor(ref.target.track, ref.target.slot, ref.target.param);
            return track + " - " + fx + ": " + param;
        }
        switch (ref.eventOp) {
            case EventLaneOp::SnapshotRecall:
                return trackTitle(ref.target.track) + " - Snapshot";
            case EventLaneOp::TrackMuteSet:
                return trackTitle(ref.target.track) + " - Mute";
            case EventLaneOp::TrackArmSet:
                return trackTitle(ref.target.track) + " - Arm";
            case EventLaneOp::FxBypassSet:
                return trackTitle(ref.target.track) + " - " +
                       fxNameFor(ref.target.track, ref.target.slot) + " Bypass";
            case EventLaneOp::TrackPitchSet:
                return trackTitle(ref.target.track) + " - Pitch";
            case EventLaneOp::NoteOn:
                return trackTitle(ref.target.track) + " - Note On";
            case EventLaneOp::NoteOff:
                return trackTitle(ref.target.track) + " - Note Off";
            default:
                return std::string("EVENT");
        }
    };
    const UiNavState nav = sceneHost_.nav();
    const SequencerPatternData& seqData = currentSequencerPattern_();
    const SequencerTick lengthTicks = std::max<SequencerTick>(1U, seqData.lengthTicks);
    UiSequencerState seq{};
    seq.patternId = activePatternId_();
    seq.ppq = seqData.ppq;
    seq.lengthBars = seqData.lengthBars;
    seq.lengthTicks = lengthTicks;
    seq.quant = seqData.quant;
    seq.resetOnLoop = (seqData.loopMode == SequencerPatternData::LoopMode::ResetOnLoop);
    seq.playheadTick = normalizePatternTick(
        sampleToTick(merged.transport.sampleTime, merged.transport.bpm, seq.ppq, sampleRateHz_),
                                            lengthTicks);
    seq.scrubTick = std::min<SequencerTick>(seq.lengthTicks, static_cast<SequencerTick>(nav.sequencerScrubTick));
    seq.zoom = std::clamp<uint16_t>(nav.sequencerZoom, 1U, 8U);
    seq.tool = nav.sequencerTool;
    seq.laneFocused = (nav.scene == UiScene::SequencerLane) || nav.sequencerLaneFocused;

    std::vector<LaneRef> laneRefs{};
    laneRefs.reserve(seqData.events.events().size() + seqData.automation.events().size());
    for (const EventLaneEvent& ev : seqData.events.events()) {
        LaneRef ref{};
        ref.kind = UiSequencerLaneKind::Event;
        ref.eventOp = ev.op;
        ref.target = ev.target;
        const auto it = std::find_if(laneRefs.begin(), laneRefs.end(), [&](const LaneRef& x) {
            return x.kind == UiSequencerLaneKind::Event && x.eventOp == ref.eventOp && sameTarget(x.target, ref.target);
        });
        if (it == laneRefs.end()) {
            laneRefs.push_back(ref);
        }
    }
    for (const AutomationPointEvent& ev : seqData.automation.events()) {
        LaneRef ref{};
        ref.kind = UiSequencerLaneKind::Automation;
        ref.target = ev.target;
        const auto it = std::find_if(laneRefs.begin(), laneRefs.end(), [&](const LaneRef& x) {
            return x.kind == UiSequencerLaneKind::Automation && sameTarget(x.target, ref.target);
        });
        if (it == laneRefs.end()) {
            laneRefs.push_back(ref);
        }
    }

    seq.lanes.reserve(laneRefs.size());
    for (std::size_t i = 0; i < laneRefs.size(); ++i) {
        UiSequencerLaneView view{};
        view.laneId = static_cast<uint16_t>(i);
        view.kind = laneRefs[i].kind;
        view.eventOp = laneRefs[i].eventOp;
        view.target = laneRefs[i].target;
        view.label = makeLaneLabel(laneRefs[i]);
        if (laneRefs[i].kind == UiSequencerLaneKind::Event) {
            view.pointCount = static_cast<uint32_t>(std::count_if(
                seqData.events.events().begin(),
                seqData.events.events().end(),
                [&](const EventLaneEvent& ev) {
                    return ev.op == laneRefs[i].eventOp && sameTarget(ev.target, laneRefs[i].target);
                }));
        } else {
            view.pointCount = static_cast<uint32_t>(std::count_if(
                seqData.automation.events().begin(),
                seqData.automation.events().end(),
                [&](const AutomationPointEvent& ev) {
                    return sameTarget(ev.target, laneRefs[i].target);
                }));
        }
        seq.lanes.push_back(std::move(view));
    }

    if (!seq.lanes.empty()) {
        seq.activeLane = std::min<uint16_t>(nav.sequencerLane, static_cast<uint16_t>(seq.lanes.size() - 1U));
    } else {
        seq.activeLane = 0;
    }

    if (seq.activeLane < laneRefs.size()) {
        const LaneRef& lane = laneRefs[seq.activeLane];
        seq.laneTitle = makeLaneLabel(lane);
        if (lane.kind == UiSequencerLaneKind::Event) {
            for (const EventLaneEvent& ev : seqData.events.events()) {
                if (ev.op != lane.eventOp || !sameTarget(ev.target, lane.target)) {
                    continue;
                }
                UiSequencerPointView point{};
                point.objectId = ev.eventId;
                point.tick = normalizePatternTick(
                    (ev.tick != 0U) ? ev.tick : sampleToTick(ev.sampleTime, merged.transport.bpm, seq.ppq, sampleRateHz_),
                    lengthTicks);
                point.value = ev.value;
                if (ev.op == EventLaneOp::SnapshotRecall) {
                    uint16_t sid = ev.snapshotId;
                    if (std::holds_alternative<EventSnapshotRecallPayload>(ev.payload)) {
                        sid = std::get<EventSnapshotRecallPayload>(ev.payload).snapshotId;
                    }
                    point.aux = sid;
                    point.label = "S" + std::to_string(static_cast<unsigned>(sid));
                } else {
                    point.label = "EVT";
                }
                seq.points.push_back(std::move(point));
            }
        } else {
            for (const AutomationPointEvent& ev : seqData.automation.events()) {
                if (!sameTarget(ev.target, lane.target)) {
                    continue;
                }
                UiSequencerPointView point{};
                point.objectId = ev.eventId;
                point.tick = normalizePatternTick(
                    sampleToTick(ev.point.sampleTime, merged.transport.bpm, seq.ppq, sampleRateHz_),
                    lengthTicks);
                point.value = ev.point.value;
                point.label = "AUTO";
                seq.points.push_back(std::move(point));
            }
        }
    } else {
        seq.laneTitle = "-";
    }

    std::sort(seq.points.begin(), seq.points.end(), [](const UiSequencerPointView& a, const UiSequencerPointView& b) {
        if (a.tick != b.tick) {
            return a.tick < b.tick;
        }
        return a.objectId < b.objectId;
    });

    if (!seq.points.empty()) {
        seq.activePoint = std::min<uint16_t>(nav.sequencerObject, static_cast<uint16_t>(seq.points.size() - 1U));
    } else {
        seq.activePoint = 0;
    }
    merged.sequencer = std::move(seq);
}

bool SamplerApplication::applySequencerIntent_(const UiIntent& intent) {
    auto sameTarget = [](const SequencerParamTarget& a, const SequencerParamTarget& b) noexcept {
        return a.track == b.track && a.slot == b.slot && a.module == b.module && a.param == b.param;
    };
    struct LaneRef {
        UiSequencerLaneKind kind{UiSequencerLaneKind::Event};
        EventLaneOp eventOp{EventLaneOp::SnapshotRecall};
        SequencerParamTarget target{};
    };

    UiNavState& nav = sceneHost_.nav();
    SequencerPatternData& data = currentSequencerPattern_();

    auto buildLaneRefs = [&]() {
        std::vector<LaneRef> refs{};
        refs.reserve(data.events.events().size() + data.automation.events().size());
        for (const EventLaneEvent& ev : data.events.events()) {
            LaneRef ref{};
            ref.kind = UiSequencerLaneKind::Event;
            ref.eventOp = ev.op;
            ref.target = ev.target;
            const auto it = std::find_if(refs.begin(), refs.end(), [&](const LaneRef& x) {
                return x.kind == UiSequencerLaneKind::Event && x.eventOp == ref.eventOp && sameTarget(x.target, ref.target);
            });
            if (it == refs.end()) {
                refs.push_back(ref);
            }
        }
        for (const AutomationPointEvent& ev : data.automation.events()) {
            LaneRef ref{};
            ref.kind = UiSequencerLaneKind::Automation;
            ref.target = ev.target;
            const auto it = std::find_if(refs.begin(), refs.end(), [&](const LaneRef& x) {
                return x.kind == UiSequencerLaneKind::Automation && sameTarget(x.target, ref.target);
            });
            if (it == refs.end()) {
                refs.push_back(ref);
            }
        }
        return refs;
    };

    const SequencerTick maxTick = std::max<SequencerTick>(1U, data.lengthTicks);
    auto collectLaneObjectIds = [&](const LaneRef& lane) {
        struct Obj {
            uint64_t id{0};
            SequencerTick tick{0};
        };
        std::vector<Obj> objects{};
        if (lane.kind == UiSequencerLaneKind::Event) {
            for (const EventLaneEvent& ev : data.events.events()) {
                if (ev.op != lane.eventOp || !sameTarget(ev.target, lane.target)) {
                    continue;
                }
                const SequencerTick tick = normalizePatternTick(
                    (ev.tick != 0U) ? ev.tick : sampleToTick(ev.sampleTime, trCtl_.bpm, data.ppq, sampleRateHz_),
                    maxTick);
                objects.push_back(Obj{ev.eventId, tick});
            }
        } else {
            for (const AutomationPointEvent& ev : data.automation.events()) {
                if (!sameTarget(ev.target, lane.target)) {
                    continue;
                }
                const SequencerTick tick = normalizePatternTick(
                    sampleToTick(ev.point.sampleTime, trCtl_.bpm, data.ppq, sampleRateHz_),
                    maxTick);
                objects.push_back(Obj{ev.eventId, tick});
            }
        }
        std::sort(objects.begin(), objects.end(), [](const Obj& a, const Obj& b) {
            if (a.tick != b.tick) {
                return a.tick < b.tick;
            }
            return a.id < b.id;
        });
        std::vector<uint64_t> ids{};
        ids.reserve(objects.size());
        for (const Obj& obj : objects) {
            ids.push_back(obj.id);
        }
        return ids;
    };

    const std::vector<LaneRef> lanes = buildLaneRefs();
    const uint16_t laneIndex =
        lanes.empty() ? 0U : std::min<uint16_t>(nav.sequencerLane, static_cast<uint16_t>(lanes.size() - 1U));
    const SequencerTick currentScrubTickRaw =
        std::min<SequencerTick>(maxTick, static_cast<SequencerTick>(nav.sequencerScrubTick));
    const SequencerTick currentScrubTick = std::min<SequencerTick>(
        maxTick,
        quantizeForwardTick(
            currentScrubTickRaw,
            data.quant,
            data.ppq,
            trCtl_.tsNum,
            trCtl_.tsDen));

    switch (intent.type) {
        case UiIntentType::SequencerSetLaneFocus: {
            const bool next = (intent.value >= 0.5f);
            if (nav.sequencerLaneFocused == next) {
                return false;
            }
            nav.sequencerLaneFocused = next;
            return false;
        }
        case UiIntentType::SequencerSetActiveLane: {
            if (lanes.empty()) {
                if (nav.sequencerLane == 0U) {
                    return false;
                }
                nav.sequencerLane = 0U;
                nav.sequencerObject = 0U;
                return false;
            }
            const uint16_t next = std::min<uint16_t>(
                static_cast<uint16_t>(std::max<int>(0, static_cast<int>(std::lround(intent.value)))),
                static_cast<uint16_t>(lanes.size() - 1U));
            if (nav.sequencerLane == next) {
                return false;
            }
            nav.sequencerLane = next;
            nav.sequencerObject = 0U;
            return false;
        }
        case UiIntentType::SequencerSetActiveObject: {
            if (lanes.empty()) {
                nav.sequencerObject = 0U;
                return false;
            }
            const std::vector<uint64_t> ids = collectLaneObjectIds(lanes[laneIndex]);
            if (ids.empty()) {
                nav.sequencerObject = 0U;
                return false;
            }
            const uint16_t next = std::min<uint16_t>(
                static_cast<uint16_t>(std::max<int>(0, static_cast<int>(std::lround(intent.value)))),
                static_cast<uint16_t>(ids.size() - 1U));
            if (nav.sequencerObject == next) {
                return false;
            }
            nav.sequencerObject = next;
            return false;
        }
        case UiIntentType::SequencerSetScrubTick: {
            const SequencerTick next = std::min<SequencerTick>(
                maxTick,
                static_cast<SequencerTick>(std::max<int64_t>(0, static_cast<int64_t>(std::llround(intent.value)))));
            if (nav.sequencerScrubTick == next) {
                return false;
            }
            nav.sequencerScrubTick = next;
            return false;
        }
        case UiIntentType::SequencerSetZoom: {
            const uint16_t next = static_cast<uint16_t>(
                std::clamp<int>(static_cast<int>(std::lround(intent.value)), 1, 8));
            if (nav.sequencerZoom == next) {
                return false;
            }
            nav.sequencerZoom = next;
            return false;
        }
        case UiIntentType::SequencerSetQuant: {
            const SequencerQuantize next = static_cast<SequencerQuantize>(
                std::clamp<int>(static_cast<int>(std::lround(intent.value)), 0, 4));
            if (data.quant == next) {
                return false;
            }
            data.quant = next;
            return false;
        }
        case UiIntentType::SequencerSetTool: {
            const uint16_t next = static_cast<uint16_t>(
                std::clamp<int>(static_cast<int>(std::lround(intent.value)), 0, 3));
            if (nav.sequencerTool == next) {
                return false;
            }
            nav.sequencerTool = next;
            return false;
        }
        case UiIntentType::SequencerSetLoopMode: {
            const SequencerPatternData::LoopMode next =
                (intent.value >= 0.5f)
                    ? SequencerPatternData::LoopMode::ResetOnLoop
                    : SequencerPatternData::LoopMode::Continue;
            if (data.loopMode == next) {
                return false;
            }
            data.loopMode = next;
            if (next == SequencerPatternData::LoopMode::Continue) {
                pendingLoopResets_.clear();
            }
            return false;
        }
        case UiIntentType::SequencerSetPatternLengthBars: {
            constexpr uint32_t kMinBars = 2U;
            constexpr uint32_t kMaxBars = 256U;
            uint32_t next = static_cast<uint32_t>(
                std::clamp<int>(static_cast<int>(std::lround(intent.value)),
                                static_cast<int>(kMinBars),
                                static_cast<int>(kMaxBars)));
            // Секвенсорная длина фиксируется только четными значениями (кратно 2).
            if ((next & 1U) != 0U) {
                next = (next == kMaxBars) ? (next - 1U) : (next + 1U);
            }
            next = std::max<uint32_t>(kMinBars, std::min<uint32_t>(kMaxBars, next));
            if (data.lengthBars == next) {
                return false;
            }
            data.lengthBars = next;
            const double beatsPerBar =
                (static_cast<double>(std::max<uint8_t>(1U, trCtl_.tsNum)) * 4.0) /
                static_cast<double>(std::max<uint8_t>(1U, trCtl_.tsDen));
            const double ticks = static_cast<double>(data.ppq) * beatsPerBar * static_cast<double>(data.lengthBars);
            data.lengthTicks = static_cast<SequencerTick>(std::max<double>(1.0, std::llround(ticks)));
            nav.sequencerScrubTick = static_cast<uint32_t>(
                std::min<SequencerTick>(data.lengthTicks, static_cast<SequencerTick>(nav.sequencerScrubTick)));
            return false;
        }
        case UiIntentType::SequencerAddObjectAtCursor: {
            if (lanes.empty()) {
                return false;
            }
            const LaneRef& lane = lanes[laneIndex];
            const uint64_t sampleTime = tickToSample(currentScrubTick, trCtl_.bpm, data.ppq, sampleRateHz_);
            if (lane.kind == UiSequencerLaneKind::Automation) {
                const uint64_t id = data.automation.addPoint(
                    lane.target,
                    AutomationInterpolationMode::Linear,
                    sampleTime,
                    0.5f);
                const std::vector<uint64_t> ids = collectLaneObjectIds(lane);
                const auto it = std::find(ids.begin(), ids.end(), id);
                nav.sequencerObject = (it == ids.end()) ? 0U : static_cast<uint16_t>(std::distance(ids.begin(), it));
                nav.sequencerLaneFocused = true;
                return true;
            }
            EventLaneEvent ev{};
            ev.sampleTime = sampleTime;
            ev.tick = currentScrubTick;
            ev.op = lane.eventOp;
            ev.target = lane.target;
            switch (ev.op) {
                case EventLaneOp::SnapshotRecall:
                    ev.snapshotId = 1U;
                    ev.payload = EventSnapshotRecallPayload{1U};
                    ev.value = 1.0f;
                    break;
                case EventLaneOp::TrackMuteSet:
                    ev.payload = EventTrackMutePayload{true};
                    ev.value = 1.0f;
                    break;
                case EventLaneOp::TrackArmSet:
                    ev.payload = EventTrackArmPayload{true};
                    ev.value = 1.0f;
                    break;
                case EventLaneOp::FxBypassSet:
                    ev.payload = EventFxBypassPayload{true};
                    ev.value = 1.0f;
                    break;
                case EventLaneOp::TrackPitchSet:
                    ev.payload = EventTrackPitchPayload{0.0f};
                    ev.value = 0.0f;
                    break;
                case EventLaneOp::NoteOn:
                    ev.payload = EventNoteOnPayload{};
                    ev.value = 100.0f / 127.0f;
                    break;
                case EventLaneOp::NoteOff:
                    ev.payload = EventNoteOffPayload{};
                    ev.value = 0.0f;
                    break;
            }
            const uint64_t id = data.events.addEvent(ev);
            const std::vector<uint64_t> ids = collectLaneObjectIds(lane);
            const auto it = std::find(ids.begin(), ids.end(), id);
            nav.sequencerObject = (it == ids.end()) ? 0U : static_cast<uint16_t>(std::distance(ids.begin(), it));
            nav.sequencerLaneFocused = true;
            return true;
        }
        case UiIntentType::SequencerDeleteSelectedObject: {
            if (lanes.empty()) {
                return false;
            }
            const LaneRef& lane = lanes[laneIndex];
            const std::vector<uint64_t> ids = collectLaneObjectIds(lane);
            if (ids.empty()) {
                return false;
            }
            const uint16_t point = std::min<uint16_t>(nav.sequencerObject, static_cast<uint16_t>(ids.size() - 1U));
            const uint64_t objectId = ids[point];
            const bool changed = (lane.kind == UiSequencerLaneKind::Automation)
                                     ? data.automation.removeEvent(objectId)
                                     : data.events.removeEvent(objectId);
            if (!changed) {
                return false;
            }
            const std::vector<uint64_t> idsAfter = collectLaneObjectIds(lane);
            if (idsAfter.empty()) {
                nav.sequencerObject = 0U;
            } else {
                nav.sequencerObject = std::min<uint16_t>(point, static_cast<uint16_t>(idsAfter.size() - 1U));
            }
            return true;
        }
        case UiIntentType::SequencerDeleteSelectedLane: {
            if (lanes.empty()) {
                return false;
            }
            const LaneRef& lane = lanes[laneIndex];
            bool changed = false;
            if (lane.kind == UiSequencerLaneKind::Automation) {
                std::vector<uint64_t> ids{};
                for (const AutomationPointEvent& ev : data.automation.events()) {
                    if (sameTarget(ev.target, lane.target)) {
                        ids.push_back(ev.eventId);
                    }
                }
                for (const uint64_t id : ids) {
                    changed = data.automation.removeEvent(id) || changed;
                }
            } else {
                std::vector<uint64_t> ids{};
                for (const EventLaneEvent& ev : data.events.events()) {
                    if (ev.op == lane.eventOp && sameTarget(ev.target, lane.target)) {
                        ids.push_back(ev.eventId);
                    }
                }
                for (const uint64_t id : ids) {
                    changed = data.events.removeEvent(id) || changed;
                }
            }
            if (!changed) {
                return false;
            }
            const std::vector<LaneRef> lanesAfter = buildLaneRefs();
            if (lanesAfter.empty()) {
                nav.sequencerLane = 0U;
                nav.sequencerObject = 0U;
            } else {
                nav.sequencerLane =
                    std::min<uint16_t>(laneIndex, static_cast<uint16_t>(lanesAfter.size() - 1U));
                nav.sequencerObject = 0U;
            }
            return true;
        }
        case UiIntentType::SequencerNudgeObjectTime: {
            if (lanes.empty()) {
                return false;
            }
            const LaneRef& lane = lanes[laneIndex];
            const std::vector<uint64_t> ids = collectLaneObjectIds(lane);
            if (ids.empty()) {
                return false;
            }
            const uint16_t point = std::min<uint16_t>(nav.sequencerObject, static_cast<uint16_t>(ids.size() - 1U));
            const uint64_t objectId = ids[point];
            const SequencerTick deltaTick =
                static_cast<SequencerTick>(std::max<int64_t>(1, std::llround(std::fabs(intent.value))));
            const int64_t sign = (intent.value >= 0.0f) ? 1 : -1;
            const int64_t deltaSamples =
                static_cast<int64_t>(tickToSample(deltaTick, trCtl_.bpm, data.ppq, sampleRateHz_)) * sign;
            if (lane.kind == UiSequencerLaneKind::Automation) {
                return data.automation.nudgeEventTime(objectId, deltaSamples);
            }
            return data.events.nudgeEventTime(objectId, deltaSamples);
        }
        case UiIntentType::SequencerAdjustObjectValue: {
            if (lanes.empty()) {
                return false;
            }
            const LaneRef& lane = lanes[laneIndex];
            const std::vector<uint64_t> ids = collectLaneObjectIds(lane);
            if (ids.empty()) {
                return false;
            }
            const uint16_t point = std::min<uint16_t>(nav.sequencerObject, static_cast<uint16_t>(ids.size() - 1U));
            const uint64_t objectId = ids[point];
            if (lane.kind == UiSequencerLaneKind::Automation) {
                const auto& events = data.automation.events();
                const auto it = std::find_if(events.begin(), events.end(), [&](const AutomationPointEvent& ev) {
                    return ev.eventId == objectId;
                });
                if (it == events.end()) {
                    return false;
                }
                const float next = std::clamp(it->point.value + intent.value, 0.0f, 1.0f);
                return data.automation.setEventValue(objectId, next);
            }

            const auto& events = data.events.events();
            const auto it = std::find_if(events.begin(), events.end(), [&](const EventLaneEvent& ev) {
                return ev.eventId == objectId;
            });
            if (it == events.end()) {
                return false;
            }

            EventLaneEvent next = *it;
            switch (next.op) {
                case EventLaneOp::SnapshotRecall: {
                    int sid = (next.snapshotId == 0U) ? 1 : static_cast<int>(next.snapshotId);
                    sid += (intent.value >= 0.0f) ? 1 : -1;
                    sid = std::clamp(sid, 1, 4);
                    next.snapshotId = static_cast<uint16_t>(sid);
                    next.value = static_cast<float>(sid);
                    next.payload = EventSnapshotRecallPayload{static_cast<uint16_t>(sid)};
                } break;
                case EventLaneOp::TrackMuteSet: {
                    bool muted = next.value >= 0.5f;
                    muted = !muted;
                    next.value = muted ? 1.0f : 0.0f;
                    next.payload = EventTrackMutePayload{muted};
                } break;
                case EventLaneOp::TrackArmSet: {
                    bool armed = next.value >= 0.5f;
                    armed = !armed;
                    next.value = armed ? 1.0f : 0.0f;
                    next.payload = EventTrackArmPayload{armed};
                } break;
                case EventLaneOp::FxBypassSet: {
                    bool enabled = next.value >= 0.5f;
                    enabled = !enabled;
                    next.value = enabled ? 1.0f : 0.0f;
                    next.payload = EventFxBypassPayload{enabled};
                } break;
                case EventLaneOp::TrackPitchSet: {
                    float semitones = next.value + intent.value * 12.0f;
                    semitones = std::clamp(semitones, -24.0f, 24.0f);
                    next.value = semitones;
                    next.payload = EventTrackPitchPayload{semitones};
                } break;
                case EventLaneOp::NoteOn: {
                    int vel = static_cast<int>(std::lround(next.value * 127.0f));
                    vel += (intent.value >= 0.0f) ? 1 : -1;
                    vel = std::clamp(vel, 1, 127);
                    next.value = static_cast<float>(vel) / 127.0f;
                    EventNoteOnPayload payload{};
                    if (std::holds_alternative<EventNoteOnPayload>(next.payload)) {
                        payload = std::get<EventNoteOnPayload>(next.payload);
                    }
                    payload.velocity = static_cast<uint8_t>(vel);
                    next.payload = payload;
                } break;
                case EventLaneOp::NoteOff:
                    return false;
            }
            return data.events.updateEvent(objectId, next);
        }
        default:
            return false;
    }
}

uint64_t SamplerApplication::makeSequencerTargetKey_(const SequencerParamTarget& target) noexcept {
    const uint64_t track = static_cast<uint64_t>(static_cast<uint16_t>(target.track));
    const uint64_t slot = static_cast<uint64_t>(static_cast<uint16_t>(target.slot));
    const uint64_t module = static_cast<uint64_t>(target.module);
    const uint64_t param = static_cast<uint64_t>(target.param);
    return (track << 48U) | (slot << 32U) | (module << 16U) | param;
}

bool SamplerApplication::applySequencerParamTarget_(const SequencerParamTarget& target, float value) {
    if (target.track < 0) {
        return false;
    }
    const uint8_t track = clampUiTrack_(static_cast<uint8_t>(target.track));
    const bool ok = (target.slot < 0)
                        ? engine_.setTrackParam(track, target.param, value)
                        : engine_.setFxParam(track, static_cast<uint8_t>(target.slot), target.param, value);
    if (ok) {
        sequencerParamMirror_[makeSequencerTargetKey_(target)] = value;
    }
    return ok;
}

void SamplerApplication::schedulePatternLoopReset_(const SequencerPatternData& seq, uint64_t nowSample) {
    pendingLoopResets_.clear();
    if (seq.loopMode != SequencerPatternData::LoopMode::ResetOnLoop) {
        return;
    }

    // 40ms сглаживание reset-перехода (под будущий scene-slider/transition слой).
    constexpr uint64_t kResetSmoothingSamples = static_cast<uint64_t>(48000.0 * 0.04);

    // Lazy apply: работаем только с реально задетыми automation-target'ами.
    std::unordered_map<uint64_t, SequencerParamTarget> affected{};
    for (const AutomationPointEvent& ev : seq.automation.events()) {
        const uint64_t key = makeSequencerTargetKey_(ev.target);
        affected.emplace(key, ev.target);
    }

    pendingLoopResets_.reserve(affected.size());
    for (const auto& [key, target] : affected) {
        const auto baseIt = seq.baseSnapshotValues.find(key);
        if (baseIt == seq.baseSnapshotValues.end()) {
            continue;
        }
        const float toValue = baseIt->second;
        const auto curIt = sequencerParamMirror_.find(key);
        const float fromValue = (curIt == sequencerParamMirror_.end()) ? toValue : curIt->second;
        if (std::fabs(fromValue - toValue) < 0.0005f) {
            continue;
        }

        PendingLoopReset reset{};
        reset.target = target;
        reset.targetKey = key;
        reset.smoother.snap(fromValue);
        reset.smoother.startRamp(toValue, nowSample, kResetSmoothingSamples);
        reset.lastSentValue = fromValue;
        pendingLoopResets_.push_back(std::move(reset));
    }
}

bool SamplerApplication::processPendingLoopResets_(uint64_t nowSample) {
    if (pendingLoopResets_.empty()) {
        return false;
    }
    // Chunking + degrade:
    // - ограничиваем число апдейтов за проход,
    // - пропускаем микрошаги (не слышны, но нагружают queue).
    constexpr std::size_t kMaxUpdatesPerPass = 8U;
    constexpr float kMinAudibleDelta = 0.002f;

    bool changed = false;
    std::size_t sent = 0U;
    for (auto it = pendingLoopResets_.begin();
         it != pendingLoopResets_.end() && sent < kMaxUpdatesPerPass;) {
        const float value = it->smoother.valueAt(nowSample);
        const bool finalStep = !it->smoother.isActive();
        const bool shouldSend = finalStep || (std::fabs(value - it->lastSentValue) >= kMinAudibleDelta);
        if (shouldSend) {
            if (applySequencerParamTarget_(it->target, value)) {
                it->lastSentValue = value;
                changed = true;
            }
            ++sent;
        }
        if (finalStep) {
            it = pendingLoopResets_.erase(it);
        } else {
            ++it;
        }
    }
    return changed;
}

uint64_t SamplerApplication::makeFxParamMirrorKey_(uint8_t track,
                                                   uint8_t fxSlot,
                                                   uint16_t paramIndex) noexcept {
    return (static_cast<uint64_t>(track) << 24U) |
           (static_cast<uint64_t>(fxSlot) << 16U) |
           static_cast<uint64_t>(paramIndex);
}

void SamplerApplication::clearFxParamMirrorTrack_(uint8_t track) {
    for (auto it = fxParamMirror_.begin(); it != fxParamMirror_.end();) {
        const uint8_t keyTrack = static_cast<uint8_t>((it->first >> 24U) & 0xFFU);
        if (keyTrack == track) {
            it = fxParamMirror_.erase(it);
            continue;
        }
        ++it;
    }
}

void SamplerApplication::updateIntentMirrors_(const UiIntent& intent) {
    switch (intent.type) {
        case UiIntentType::SetFxParam: {
            const uint64_t key = makeFxParamMirrorKey_(intent.track, intent.fxSlot, intent.paramIndex);
            fxParamMirror_[key] = intent.value;
            SequencerParamTarget t{};
            t.track = static_cast<int16_t>(intent.track);
            t.slot = static_cast<int16_t>(intent.fxSlot);
            t.param = intent.paramIndex;
            sequencerParamMirror_[makeSequencerTargetKey_(t)] = intent.value;
        } break;
        case UiIntentType::SetTrackSpeed: {
            SequencerParamTarget t{};
            t.track = static_cast<int16_t>(intent.track);
            t.slot = kRtSlotTrackParams;
            t.param = toParamIndex(TrackParamId::PlaybackInc);
            sequencerParamMirror_[makeSequencerTargetKey_(t)] = intent.value;
        } break;
        case UiIntentType::SetTrackGain: {
            SequencerParamTarget t{};
            t.track = static_cast<int16_t>(intent.track);
            t.slot = kRtSlotTrackParams;
            t.param = toParamIndex(TrackParamId::Gain01);
            sequencerParamMirror_[makeSequencerTargetKey_(t)] = intent.value;
        } break;
        case UiIntentType::SetTrackTrimStart: {
            SequencerParamTarget t{};
            t.track = static_cast<int16_t>(intent.track);
            t.slot = kRtSlotTrackParams;
            t.param = toParamIndex(TrackParamId::StartNorm);
            sequencerParamMirror_[makeSequencerTargetKey_(t)] = intent.value;
        } break;
        case UiIntentType::SetTrackTrimEnd: {
            SequencerParamTarget t{};
            t.track = static_cast<int16_t>(intent.track);
            t.slot = kRtSlotTrackParams;
            t.param = toParamIndex(TrackParamId::EndNorm);
            sequencerParamMirror_[makeSequencerTargetKey_(t)] = intent.value;
        } break;
        case UiIntentType::RemoveFxFromTrack:
        case UiIntentType::ClearTrackSample:
            clearFxParamMirrorTrack_(intent.track);
            break;
        default:
            break;
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
            if (hasSceneFrame) {
                const uint64_t nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
                prepared.hud = hudLayer_.view(nowMs);
            }
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

bool SamplerApplication::applySequencerItem_(const SequencerDispatchItem& item) {
    if (item.source == SequencerDispatchItem::Source::Event) {
        const EventLaneEvent& ev = item.event;
        const int16_t trackRaw = ev.target.track;
        const uint8_t track = clampUiTrack_(trackRaw < 0 ? 0U : static_cast<uint8_t>(trackRaw));
        switch (ev.op) {
            case EventLaneOp::TrackMuteSet: {
                bool muted = ev.value >= 0.5f;
                if (std::holds_alternative<EventTrackMutePayload>(ev.payload)) {
                    muted = std::get<EventTrackMutePayload>(ev.payload).muted;
                }
                return engine_.setTrackMuted(track, muted);
            }
            case EventLaneOp::TrackArmSet: {
                bool armed = ev.value >= 0.5f;
                if (std::holds_alternative<EventTrackArmPayload>(ev.payload)) {
                    armed = std::get<EventTrackArmPayload>(ev.payload).armed;
                }
                return engine_.setTrackArmed(track, armed);
            }
            case EventLaneOp::FxBypassSet: {
                if (ev.target.slot < 0) {
                    return false;
                }
                bool enabled = ev.value >= 0.5f;
                if (std::holds_alternative<EventFxBypassPayload>(ev.payload)) {
                    enabled = std::get<EventFxBypassPayload>(ev.payload).bypass;
                }
                return engine_.setFxEnabled(
                    track,
                    static_cast<uint8_t>(ev.target.slot),
                    enabled);
            }
            case EventLaneOp::TrackPitchSet:
                return applySequencerParamTarget_(ev.target, ev.value);
            case EventLaneOp::SnapshotRecall:
                if (ev.snapshotId == 0U && !std::holds_alternative<EventSnapshotRecallPayload>(ev.payload)) {
                    return false;
                }
                {
                    uint16_t id = ev.snapshotId;
                    if (std::holds_alternative<EventSnapshotRecallPayload>(ev.payload)) {
                        id = std::get<EventSnapshotRecallPayload>(ev.payload).snapshotId;
                    }
                    if (id == 0U) {
                        return false;
                    }
                    UiIntent recall{};
                    recall.type = UiIntentType::SnapshotRecallSlot;
                    recall.snapshotSlot = static_cast<uint8_t>(id - 1U);
                    return dispatchWidgetIntent_(recall);
                }
            case EventLaneOp::NoteOn:
                {
                    uint8_t note = 60U;
                    uint8_t velocity = 100U;
                    if (std::holds_alternative<EventNoteOnPayload>(ev.payload)) {
                        const EventNoteOnPayload p = std::get<EventNoteOnPayload>(ev.payload);
                        note = p.note;
                        velocity = p.velocity;
                    }
                    const float velocity01 = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
                    return engine_.triggerTrackNoteOn(track, note, velocity01);
                }
            case EventLaneOp::NoteOff:
                {
                    uint8_t note = 60U;
                    if (std::holds_alternative<EventNoteOffPayload>(ev.payload)) {
                        note = std::get<EventNoteOffPayload>(ev.payload).note;
                    }
                    return engine_.triggerTrackNoteOff(track, note);
                }
        }
        return false;
    }

    const AutomationPointEvent& ev = item.automation;
    if (ev.target.track < 0) {
        // Глобальные automation-targets (transport/domain) добавим отдельным
        // маппером после финализации target namespaces.
        return false;
    }
    return applySequencerParamTarget_(ev.target, ev.point.value);
}

void SamplerApplication::recordIntentToSequencer_(const UiIntent& intent, uint64_t sampleTime) {
    if (!recordEnabled_ || sequencerPlaybackDispatch_ || snapshotRecallDispatch_ || !trCtl_.playing) {
        return;
    }
    SequencerPatternData& seq = currentSequencerPattern_();
    const SequencerTick rawTick = sampleToTick(sampleTime, trCtl_.bpm, seq.ppq, sampleRateHz_);
    const SequencerTick quantTick = quantizeForwardTick(rawTick, seq.quant, seq.ppq, trCtl_.tsNum, trCtl_.tsDen);
    const SequencerTick localTick = normalizePatternTick(quantTick, seq.lengthTicks);
    const uint64_t quantSampleTime = tickToSample(localTick, trCtl_.bpm, seq.ppq, sampleRateHz_);

    SequencerParamTarget target{};
    if (mapIntentToAutomationTarget(intent, target)) {
        const uint64_t targetKey = makeSequencerTargetKey_(target);
        if (seq.baseSnapshotValues.find(targetKey) == seq.baseSnapshotValues.end()) {
            const auto it = sequencerParamMirror_.find(targetKey);
            const float baseValue = (it == sequencerParamMirror_.end()) ? intent.value : it->second;
            seq.baseSnapshotValues.emplace(targetKey, baseValue);
        }
        const TransportRtSnapshot snap = makeTransportSnapshot(trCtl_, quantSampleTime);
        // Один UI-gesture -> один gesture-batch в automation lane.
        if (!seq.automation.beginGesture(target, AutomationInterpolationMode::Linear)) {
            // Если что-то осталось незакоммиченным (нештатный путь), сбрасываем и переоткрываем.
            seq.automation.cancelGesture();
            if (!seq.automation.beginGesture(target, AutomationInterpolationMode::Linear)) {
                return;
            }
        }
        (void)seq.automation.pushGesturePoint(quantSampleTime, intent.value);
        AutomationGestureCommitResult commit{};
        (void)seq.automation.commitGesture(snap, QuantizeMode::None, commit);
        return;
    }

    EventLaneEvent ev{};
    if (mapIntentToEvent(intent, quantSampleTime, ev)) {
        ev.tick = localTick;
        (void)seq.events.addEvent(ev);
    }
}

bool SamplerApplication::processSequencerPlayback_() {
    if (!trCtl_.playing) {
        // При stop курсор сбрасываем: при следующем play не отстреливаем старый диапазон.
        sequencerCursorInitialized_ = false;
        pendingLoopResets_.clear();
        return false;
    }

    const uint64_t now = trCtl_.sampleTime;
    if (!sequencerCursorInitialized_) {
        sequencerCursorInitialized_ = true;
        sequencerCursorSample_ = now;
        return false;
    }
    if (now <= sequencerCursorSample_) {
        return false;
    }
    bool changed = processPendingLoopResets_(now);
    const SequencerPatternData& seq = currentSequencerPattern_();
    const SequencerTick lengthTicks = std::max<SequencerTick>(1U, seq.lengthTicks);
    const SequencerTick prevTick = sampleToTick(sequencerCursorSample_, trCtl_.bpm, seq.ppq, sampleRateHz_);
    const SequencerTick nowTick = sampleToTick(now, trCtl_.bpm, seq.ppq, sampleRateHz_);
    if (nowTick <= prevTick) {
        sequencerCursorSample_ = now;
        return false;
    }

    const SequencerTick prevPhase = normalizePatternTick(prevTick, lengthTicks);
    const SequencerTick nowPhase = normalizePatternTick(nowTick, lengthTicks);
    const bool wrapped = (nowTick / lengthTicks) != (prevTick / lengthTicks);
    const bool fullCycle = (nowTick - prevTick) >= lengthTicks;

    std::vector<SequencerDispatchItem> plan{};
    plan.reserve(seq.events.events().size() + seq.automation.events().size());

    for (const EventLaneEvent& ev : seq.events.events()) {
        const SequencerTick eventTick =
            (ev.tick != 0U) ? ev.tick : sampleToTick(ev.sampleTime, trCtl_.bpm, seq.ppq, sampleRateHz_);
        const SequencerTick phaseTick = normalizePatternTick(eventTick, lengthTicks);
        if (!isTickDueInWindow(phaseTick, prevPhase, nowPhase, wrapped, fullCycle)) {
            continue;
        }
        SequencerDispatchItem item{};
        item.sampleTime = relativePhaseOrder(phaseTick, prevPhase, lengthTicks);
        item.source = SequencerDispatchItem::Source::Event;
        item.event = ev;
        plan.push_back(item);
    }

    for (const AutomationPointEvent& av : seq.automation.events()) {
        const SequencerTick eventTick = sampleToTick(av.point.sampleTime, trCtl_.bpm, seq.ppq, sampleRateHz_);
        const SequencerTick phaseTick = normalizePatternTick(eventTick, lengthTicks);
        if (!isTickDueInWindow(phaseTick, prevPhase, nowPhase, wrapped, fullCycle)) {
            continue;
        }
        SequencerDispatchItem item{};
        item.sampleTime = relativePhaseOrder(phaseTick, prevPhase, lengthTicks);
        item.source = SequencerDispatchItem::Source::Automation;
        item.automation = av;
        plan.push_back(item);
    }

    std::sort(plan.begin(), plan.end(), [](const SequencerDispatchItem& a, const SequencerDispatchItem& b) {
        if (a.sampleTime != b.sampleTime) {
            return a.sampleTime < b.sampleTime;
        }
        // Сохраняем правило порядка: event раньше automation.
        if (a.source != b.source) {
            return a.source == SequencerDispatchItem::Source::Event;
        }
        const uint64_t aId = (a.source == SequencerDispatchItem::Source::Event)
                                 ? a.event.eventId
                                 : a.automation.eventId;
        const uint64_t bId = (b.source == SequencerDispatchItem::Source::Event)
                                 ? b.event.eventId
                                 : b.automation.eventId;
        return aId < bId;
    });

    // Для automation в одном tick-слоте и одинаковом target оставляем только
    // последнюю точку. Это снижает burst-нагрузку на control->RT очередь.
    if (!plan.empty()) {
        using AutoKey = std::tuple<uint64_t, int16_t, int16_t, uint16_t, uint16_t>;
        std::map<AutoKey, std::size_t> lastAutomationIndex{};
        std::vector<SequencerDispatchItem> compact{};
        compact.reserve(plan.size());

        for (const SequencerDispatchItem& item : plan) {
            if (item.source != SequencerDispatchItem::Source::Automation) {
                compact.push_back(item);
                continue;
            }
            const SequencerParamTarget& t = item.automation.target;
            const AutoKey key{
                item.sampleTime,
                t.track,
                t.slot,
                t.module,
                t.param
            };
            const auto it = lastAutomationIndex.find(key);
            if (it == lastAutomationIndex.end()) {
                lastAutomationIndex.emplace(key, compact.size());
                compact.push_back(item);
            } else {
                compact[it->second] = item;
            }
        }
        plan.swap(compact);
    }

    sequencerPlaybackDispatch_ = true;
    const bool useLoopReset = wrapped && (seq.loopMode == SequencerPatternData::LoopMode::ResetOnLoop);
    if (!useLoopReset) {
        for (const SequencerDispatchItem& item : plan) {
            changed = applySequencerItem_(item) || changed;
        }
    } else {
        const uint64_t boundaryOrder = static_cast<uint64_t>(relativePhaseOrder(0U, prevPhase, lengthTicks));
        std::size_t split = 0U;
        while (split < plan.size() && plan[split].sampleTime < boundaryOrder) {
            changed = applySequencerItem_(plan[split]) || changed;
            ++split;
        }

        // На boundary делаем lazy reset только затронутых automation-параметров.
        schedulePatternLoopReset_(seq, now);
        changed = processPendingLoopResets_(now) || changed;

        for (std::size_t i = split; i < plan.size(); ++i) {
            changed = applySequencerItem_(plan[i]) || changed;
        }
    }
    sequencerPlaybackDispatch_ = false;
    sequencerCursorSample_ = now;
    return changed;
}

void SamplerApplication::setRecordEnabled_(bool enabled) {
    recordEnabled_ = enabled;
    trCtl_.recordEnabled = enabled;
    UiState merged = uiStore_.snapshot();
    merged.transport = trCtl_;
    uiStore_.setState(merged);
}

bool SamplerApplication::handleGesture_(const UiGestureEvent& ev) {
    const UiGesture action = ev.action;

    if (action == UiGesture::Quit) {
        stopUi_.store(true, std::memory_order_release);
        return false;
    }
    if (action == UiGesture::Record) {
        setRecordEnabled_(!recordEnabled_);
        return true;
    }
    if (action == UiGesture::SnapshotSlotDirect) {
        const int raw = static_cast<int>(ev.value) - 1;
        const uint8_t slot = static_cast<uint8_t>(std::clamp(raw, 0, 3));
        UiIntent snapshot{};
        snapshot.type = UiIntentType::SnapshotTriggerSlot;
        snapshot.snapshotSlot = slot;
        (void)dispatchWidgetIntent_(snapshot);
        return true;
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
        const bool changed = dispatchWidgetIntent_(it);
        if (changed) {
            recordIntentToSequencer_(it, trCtl_.sampleTime);
        }
        return true;
    }
    if (action == UiGesture::SelectPatternDirect) {
        UiIntent it{};
        it.type = UiIntentType::SwitchPatternSet;
        it.value = static_cast<float>(std::max<int16_t>(1, ev.value));
        const bool changed = dispatchWidgetIntent_(it);
        if (changed) {
            recordIntentToSequencer_(it, trCtl_.sampleTime);
        }
        syncPatternStateToUi_();
        return true;
    }

    // Глобальные hotkey undo/redo (доступны в любой сцене).
    // F2/F9 резервируются под аппаратные кнопки, ActionUndo/ActionRedo —
    // под универсальный слой pointer-команд.
    if (action == UiGesture::ActionUndo || action == UiGesture::F2) {
        if (recordEnabled_ && currentSequencerPattern_().automation.undoLastGesture()) {
            return true;
        }
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav(), &hudLayer_};
        (void)history_.undo([this, &ctx](const UiIntent& intent) {
            return intentApplier_.apply(intent, ctx);
        });
        return true;
    }
    if (action == UiGesture::ActionRedo || action == UiGesture::F9) {
        if (recordEnabled_ && currentSequencerPattern_().automation.redoLastGesture()) {
            return true;
        }
        UiIntentApplier::Context ctx{engine_, uiStore_, trCtl_, tracksCtl_, &sceneHost_.nav(), &hudLayer_};
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
    const uint64_t gestureSample = trCtl_.sampleTime;
    for (const UiIntent& intent : widgetOut.intents) {
        const bool changed = dispatchWidgetIntent_(intent);
        if (changed &&
            intent.type != UiIntentType::HudNotify &&
            intent.type != UiIntentType::SnapshotTriggerSlot &&
            intent.type != UiIntentType::SnapshotCaptureSlot &&
            intent.type != UiIntentType::SnapshotRecallSlot &&
            intent.type != UiIntentType::SnapshotCaptured &&
            intent.type != UiIntentType::SnapshotApplied) {
            recordIntentToSequencer_(intent, gestureSample);
        }
    }
    if (txOpened) {
        (void)history_.commit();
    }
    return true;
}

void SamplerApplication::syncPatternStateToUi_() {
    (void)engine_.syncUiCache(trCtl_, tracksCtl_);
    trCtl_.recordEnabled = recordEnabled_;
    UiState merged = uiStore_.snapshot();
    merged.transport = trCtl_;
    merged.tracks = tracksCtl_;
    merged.pattern = engine_.patternUiState();
    sequencerPatternId_ =
        (merged.pattern.activeId == kInvalidPatternId) ? sequencerPatternId_ : merged.pattern.activeId;
    (void)ensureSequencerPattern_(sequencerPatternId_);
    syncSequencerStateToUi_(merged);
    uiStore_.setState(merged);
}

} // namespace avantgarde
