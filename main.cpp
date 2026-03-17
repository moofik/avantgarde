// main.cpp
#include <cstdio>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <string_view>
#include <deque>
#include <mutex>

#include "contracts/types.h"
#include "contracts/IDisplay.h"
#include "contracts/IPlatform.h"
#include "contracts/IAudioEngine.h"
#include "contracts/IRtExtension.h"
#include "contracts/IClipTrack.h"
#include "contracts/IParameterized.h"
#include "contracts/IUi.h"
#include "contracts/UiTheme.h"
#include "contracts/ids.h"   // где у тебя CmdId enum (или include из contracts)

#include "control/ControlCommandDispatcher.h"
#include "control/TerminalUiInput.h"
#include "platform/macos/MacAudioHost.mm"   // твой
#include "platform/macos/MacGbWindowRenderer.h"
#include "platform/lowres/LowResUiRenderer.h"
#include "platform/terminal/AnsiUiRenderer.h"
#include "platform/terminal/GothicGbUiRenderer.h"
#include "platform/terminal/TerminalCharDisplay.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/AudioEngine.cpp"       // твой
#include "runtime/ClipTrack.cpp"  // твой
#include "runtime/ParamBridgeDualBuffer.cpp"  // твой
#include "runtime/RtCommandQueueSPSC.cpp"  // твой
#include "runtime/TransportBridgeDualBuffer.h"
#include "service/UiStateComposer.h"
#include "service/UiStateStore.h"
#include "service/ui/ManagerWidget.h"
#include "service/ui/UiSceneHost.h"

using namespace avantgarde;

namespace {
std::array<ITrack*, 2> gParamTracks{};
enum class UiMode {
    Ansi,
    LowRes,
    Gb,
    GbWindow
};

bool parseUiMode(std::string_view raw, UiMode& out) noexcept {
    if (raw == "ansi") {
        out = UiMode::Ansi;
        return true;
    }
    if (raw == "lowres") {
        out = UiMode::LowRes;
        return true;
    }
    if (raw == "gb") {
        out = UiMode::Gb;
        return true;
    }
    if (raw == "gb-window") {
        out = UiMode::GbWindow;
        return true;
    }
    return false;
}

IParameterized* ResolveParamTarget(Target target) noexcept {
    if (target.trackId < 0 || static_cast<std::size_t>(target.trackId) >= gParamTracks.size()) {
        return nullptr;
    }
    if (target.slotId < 0) {
        return nullptr;
    }
    ITrack* tr = gParamTracks[static_cast<std::size_t>(target.trackId)];
    if (!tr) {
        return nullptr;
    }
    return tr->getModule(static_cast<std::size_t>(target.slotId));
}

// Minimal input queue for cross-thread event handoff.
// Kept local to main.cpp to avoid extra global UI surface area.
struct InputEventQueue final {
    void push(const UiInputEvent& ev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= 1024U) {
            queue_.pop_front();
        }
        queue_.push_back(ev);
    }

    bool tryPop(UiInputEvent& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            out.action = UiInputAction::None;
            return false;
        }
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

private:
    std::mutex mutex_{};
    std::deque<UiInputEvent> queue_{};
};
} // namespace

static void RenderThunk(AudioProcessContext& ctx, void* user) noexcept {
    auto* engine = static_cast<IAudioEngine*>(user);
    engine->processBlock(ctx);
}

int main(int argc, char** argv) {
    UiMode uiMode = UiMode::Ansi;
    UiTheme uiTheme = UiTheme::Default;
    bool uiThemeProvided = false;
    int argi = 1;
    while (argi < argc) {
        const std::string arg = argv[argi];
        if (arg.rfind("--ui=", 0) == 0) {
            if (!parseUiMode(std::string_view(arg).substr(5), uiMode)) {
                std::printf("Unsupported UI mode: %s\n", arg.c_str());
                return 1;
            }
            ++argi;
            continue;
        }
        if (arg == "--ui" && (argi + 1) < argc) {
            if (!parseUiMode(argv[argi + 1], uiMode)) {
                std::printf("Unsupported UI mode: %s\n", argv[argi + 1]);
                return 1;
            }
            argi += 2;
            continue;
        }
        if (arg.rfind("--theme=", 0) == 0) {
            if (!parseUiTheme(std::string_view(arg).substr(8), uiTheme)) {
                std::printf("Unsupported theme: %s\n", arg.c_str());
                return 1;
            }
            uiThemeProvided = true;
            ++argi;
            continue;
        }
        if (arg == "--theme" && (argi + 1) < argc) {
            if (!parseUiTheme(argv[argi + 1], uiTheme)) {
                std::printf("Unsupported theme: %s\n", argv[argi + 1]);
                return 1;
            }
            uiThemeProvided = true;
            argi += 2;
            continue;
        }
        if (arg == "--ui") {
            std::printf("Missing value for --ui (expected: ansi|lowres|gb|gb-window)\n");
            return 1;
        }
        if (arg == "--theme") {
            std::printf("Missing value for --theme (expected: gothic)\n");
            return 1;
        }
        if (arg.rfind("--", 0) == 0) {
            std::printf("Unknown option: %s\n", arg.c_str());
            return 1;
        }
        break;
    }

    if (argi >= argc) {
        std::printf("Usage: %s [--ui=ansi|lowres|gb|gb-window] [--theme=gothic] /path/to/track1.wav [/path/to/track2.wav]\n", argv[0]);
        return 1;
    }
    const char* wavPath0 = argv[argi];
    const char* wavPath1 = (argi + 1 < argc) ? argv[argi + 1] : nullptr;

    std::string clipName0 = wavPath0;
    if (const std::size_t pos = clipName0.find_last_of("/\\"); pos != std::string::npos) {
        clipName0 = clipName0.substr(pos + 1);
    }

    std::string clipName1;
    if (wavPath1) {
        clipName1 = wavPath1;
        if (const std::size_t pos = clipName1.find_last_of("/\\"); pos != std::string::npos) {
            clipName1 = clipName1.substr(pos + 1);
        }
    }

    // 1) Host
    MacAudioHost host;
    RtCommandQueueSPSC qUi;
    RtCommandQueueSPSC qRt;
    ParamBridgeDualBuffer pb(10);

    // 2) Engine
    AudioEngine engine(&qRt, &pb);
    engine.setSampleRate(48000.0);
    TransportBridgeDualBuffer transport;

    // 3) Clip tracks (2 tracks runtime)
    auto clip0 = std::make_unique<ClipTrackImpl>();
    IClipTrack* clipPtr0 = clip0.get();
    auto clip1 = std::make_unique<ClipTrackImpl>();
    IClipTrack* clipPtr1 = clip1.get();
    auto previewClip = std::make_unique<ClipTrackImpl>();
    IClipTrack* previewClipPtr = previewClip.get();

    // 4) Load sample (non-RT!)
    if (!clipPtr0->loadSlotFromFile(0, wavPath0)) {
        std::printf("loadSlotFromFile failed: %s\n", wavPath0);
        return 2;
    }
    (void)clipPtr0->setSlotLooping(0, true);

    bool track1Loaded = false;
    if (wavPath1) {
        track1Loaded = clipPtr1->loadSlotFromFile(0, wavPath1);
        if (!track1Loaded) {
            std::printf("warning: loadSlotFromFile failed for track2: %s\n", wavPath1);
        } else {
            (void)clipPtr1->setSlotLooping(0, true);
        }
    }

    gParamTracks[0] = clipPtr0;
    gParamTracks[1] = clipPtr1;
    pb.setResolver(&ResolveParamTarget);

    // UI state (service-side)
    UiStateStore uiStore;
    UiStateComposer uiComposer;
    std::unique_ptr<IDisplay> uiDisplay;
    std::unique_ptr<IUiRenderer> uiRenderer;
    const UiTheme effectiveGbTheme = uiThemeProvided ? uiTheme : UiTheme::Gothic;
    // Single tuning knob for the GB frame width in both terminal and window UI modes.
    static constexpr uint16_t kGbTextWidth = 60;
    if (uiMode == UiMode::LowRes) {
        uiDisplay = std::make_unique<TerminalCharDisplay>(64, 16);
        uiRenderer = std::make_unique<LowResUiRenderer>(*uiDisplay);
    } else if (uiMode == UiMode::GbWindow) {
        uiRenderer = std::make_unique<MacGbWindowRenderer>(effectiveGbTheme, kGbTextWidth);
    } else if (uiMode == UiMode::Gb) {
        uiRenderer = std::make_unique<GothicGbUiRenderer>(effectiveGbTheme, kGbTextWidth);
    } else {
        uiRenderer = std::make_unique<AnsiUiRenderer>();
    }

    UiTransportState trUi{};
    trUi.playing = false;
    trUi.bpm = 120.0f;
    trUi.tsNum = 4;
    trUi.tsDen = 4;
    trUi.quant = QuantizeMode::Bar;
    trUi.activeTrack = 0;
    transport.setTempo(trUi.bpm);
    transport.setTimeSignature(trUi.tsNum, trUi.tsDen);
    transport.setQuantize(trUi.quant);
    transport.setPlaying(trUi.playing);
    uiStore.setTransport(trUi);

    UiTrackStateView track0{};
    track0.id = 0;
    track0.state = UiTrackState::Stopped;
    track0.bars = 4;
    track0.stretchRatio = 1.0f;
    track0.gain01 = 1.0f;
    track0.loop = true;
    track0.fxCount = 0;
    track0.clipName = clipName0;
    uiStore.setTrack(0, track0);

    UiTrackStateView track1{};
    track1.id = 1;
    track1.state = track1Loaded ? UiTrackState::Stopped : UiTrackState::Empty;
    track1.bars = 4;
    track1.stretchRatio = 1.0f;
    track1.gain01 = 1.0f;
    track1.loop = track1Loaded;
    track1.fxCount = 0;
    track1.clipName = track1Loaded ? clipName1 : std::string{};
    uiStore.setTrack(1, track1);

    std::array<IClipTrack*, 2> clipCtl{clipPtr0, clipPtr1};

    // 5) Register track (non-RT)
    engine.registerTrack(std::move(clip0));
    engine.registerTrack(std::move(clip1));
    engine.registerTrack(std::move(previewClip)); // hidden preview voice (track index 2)

    // 6) Transport + quantized scheduler
    engine.setTransportBridge(&transport);
    QuantizedSchedulerRtExtension scheduler(&qUi, &qRt, &transport, 48000.0);
    engine.addRtExtension(&scheduler);
    ControlCommandDispatcher controlDispatcher(&qUi);
    ControlCommandDispatcher immediateDispatcher(&qRt); // bypass quantized scheduler (preview path)

    // Set initial quantization mode via UI command path.
    (void)controlDispatcher.setQuantizeMode(QuantizeMode::Bar);
    (void)controlDispatcher.setTempoBpm(trUi.bpm);
    (void)controlDispatcher.setTimeSignature(trUi.tsNum, trUi.tsDen);
    (void)immediateDispatcher.sendParamSet(/*track=*/2, /*slot=*/-1, /*gain index=*/0, /*-12dB*/0.25f);
    (void)immediateDispatcher.sendParamSet(/*track=*/2, /*slot=*/-1, /*loop index=*/1, /*off*/0.0f);

    UiSceneHost sceneHost;
    (void)sceneHost.registerWidget(UiScene::Manager, std::make_unique<ManagerWidget>(kGbTextWidth));
    sceneHost.setScene(UiScene::Tracks);
    sceneHost.nav().selectedTrack = trUi.activeTrack;

    std::atomic<UiScene> activeUiScene{UiScene::Tracks};
    std::mutex customFrameMutex;
    std::string customFrame;
    auto* windowUiInput = dynamic_cast<MacGbWindowRenderer*>(uiRenderer.get());
    InputEventQueue inputQueue;

    // 7) Run audio
    StreamConfig cfg{ .sampleRate=48000, .blockFrames=256, .numInput=0, .numOutput=2 };
    auto stream = host.openStream(cfg, "default", "default");

    if (!stream) {
        std::printf("openStream failed\n");
        return 3;
    }
    engine.setNumOutput((uint32_t)stream->numOutput());
    if (!stream->start(&RenderThunk, &engine)) {
        std::printf("stream->start failed\n");
        return 4;
    }

    std::atomic<bool> stopUi{false};
    std::thread terminalInputThread([&]() {
        TerminalUiInput input;
        while (!stopUi.load(std::memory_order_acquire)) {
            UiInputEvent ev{};
            if (input.poll(ev)) {
                inputQueue.push(ev);
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::thread controlThread([&]() {
        std::array<UiTrackStateView, 2> tracksCtl{track0, track1};
        UiTransportState trCtl = trUi;
        auto refreshManagerFrame = [&]() {
            UiTextBuffer buf{};
            if (!sceneHost.renderActive(buf, uiStore.snapshot())) {
                return;
            }
            std::string frame;
            for (const std::string& line : buf.lines) {
                frame += line;
                frame.push_back('\n');
            }
            std::lock_guard<std::mutex> lock(customFrameMutex);
            customFrame = std::move(frame);
        };
        auto handleIntent = [&](const UiIntent& intent) {
            switch (intent.type) {
                case UiIntentType::LoadSampleToTrack: {
                    const uint8_t t = std::min<uint8_t>(intent.track, 1);
                    if (intent.path.empty() || !clipCtl[t]) {
                        break;
                    }
                    if (!clipCtl[t]->loadSlotFromFile(0, intent.path.c_str())) {
                        break;
                    }
                    (void)clipCtl[t]->setSlotLooping(0, true);
                    tracksCtl[t].clipName = intent.path;
                    if (const std::size_t pos = tracksCtl[t].clipName.find_last_of("/\\"); pos != std::string::npos) {
                        tracksCtl[t].clipName = tracksCtl[t].clipName.substr(pos + 1);
                    }
                    if (tracksCtl[t].state == UiTrackState::Empty) {
                        tracksCtl[t].state = UiTrackState::Stopped;
                    }
                    tracksCtl[t].loop = true;
                    uiStore.setTrack(t, tracksCtl[t]);
                } break;
                case UiIntentType::PreviewRequest:
                    (void)immediateDispatcher.sendStop(2, 0);
                    if (!intent.path.empty() && previewClipPtr->loadSlotFromFile(0, intent.path.c_str())) {
                        (void)previewClipPtr->setSlotLooping(0, false);
                        (void)immediateDispatcher.sendPlay(2, 0);
                    }
                    break;
                case UiIntentType::PreviewStop:
                    (void)immediateDispatcher.sendStop(2, 0);
                    break;
                case UiIntentType::OpenScene:
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
                    break;
            }
        };
        const auto recomputePlaying = [&]() noexcept {
            return tracksCtl[0].state == UiTrackState::Playing || tracksCtl[1].state == UiTrackState::Playing;
        };

        while (!stopUi.load(std::memory_order_acquire)) {
            UiInputEvent ev{};
            if (!inputQueue.tryPop(ev)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (ev.action == UiInputAction::Quit) {
                stopUi.store(true, std::memory_order_release);
                break;
            }

            if (ev.action == UiInputAction::SelectTrack0 || ev.action == UiInputAction::SelectTrack1) {
                trCtl.activeTrack = (ev.action == UiInputAction::SelectTrack0) ? 0 : 1;
                sceneHost.nav().selectedTrack = trCtl.activeTrack;
                uiStore.setTransport(trCtl);
            }

            const WidgetOutput widgetOut = sceneHost.handleInput(ev.action, uiStore.snapshot());
            for (const UiIntent& intent : widgetOut.intents) {
                handleIntent(intent);
            }

            if (sceneHost.scene() == UiScene::Manager) {
                activeUiScene.store(UiScene::Manager, std::memory_order_release);
                refreshManagerFrame();
                continue;
            }

            activeUiScene.store(UiScene::Tracks, std::memory_order_release);

            switch (ev.action) {
                case UiInputAction::SelectTrack0:
                case UiInputAction::SelectTrack1:
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
                    const uint8_t t = trCtl.activeTrack > 1 ? 1 : trCtl.activeTrack;
                    (void)controlDispatcher.sendPlay(static_cast<int16_t>(t), 0);
                    if (tracksCtl[t].state != UiTrackState::Empty) {
                        tracksCtl[t].state = UiTrackState::Playing;
                        uiStore.setTrack(t, tracksCtl[t]);
                    }
                    trCtl.playing = recomputePlaying();
                    transport.setPlaying(trCtl.playing);
                    uiStore.setTransport(trCtl);
                } break;
                case UiInputAction::StopActiveTrack: {
                    const uint8_t t = trCtl.activeTrack > 1 ? 1 : trCtl.activeTrack;
                    (void)controlDispatcher.sendStop(static_cast<int16_t>(t), 0);
                    if (tracksCtl[t].state != UiTrackState::Empty) {
                        tracksCtl[t].state = UiTrackState::Stopped;
                        uiStore.setTrack(t, tracksCtl[t]);
                    }
                    trCtl.playing = recomputePlaying();
                    transport.setPlaying(trCtl.playing);
                    uiStore.setTransport(trCtl);
                } break;
                case UiInputAction::TrackSpeedUp: {
                    const uint8_t t = trCtl.activeTrack > 1 ? 1 : trCtl.activeTrack;
                    tracksCtl[t].stretchRatio = std::clamp(tracksCtl[t].stretchRatio + 0.05f, 0.25f, 4.0f);
                    (void)controlDispatcher.sendParamSet(static_cast<int16_t>(t), -1, 2, tracksCtl[t].stretchRatio);
                    uiStore.setTrack(t, tracksCtl[t]);
                } break;
                case UiInputAction::TrackSpeedDown: {
                    const uint8_t t = trCtl.activeTrack > 1 ? 1 : trCtl.activeTrack;
                    tracksCtl[t].stretchRatio = std::clamp(tracksCtl[t].stretchRatio - 0.05f, 0.25f, 4.0f);
                    (void)controlDispatcher.sendParamSet(static_cast<int16_t>(t), -1, 2, tracksCtl[t].stretchRatio);
                    uiStore.setTrack(t, tracksCtl[t]);
                } break;
                case UiInputAction::QuantNone:
                    (void)controlDispatcher.setQuantizeMode(QuantizeMode::None);
                    trCtl.quant = QuantizeMode::None;
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::QuantBeat:
                    (void)controlDispatcher.setQuantizeMode(QuantizeMode::Beat);
                    trCtl.quant = QuantizeMode::Beat;
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::QuantBar:
                    (void)controlDispatcher.setQuantizeMode(QuantizeMode::Bar);
                    trCtl.quant = QuantizeMode::Bar;
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::BpmUp:
                    trCtl.bpm = std::clamp(trCtl.bpm + 1.0f, 20.0f, 300.0f);
                    (void)controlDispatcher.setTempoBpm(trCtl.bpm);
                    transport.setTempo(trCtl.bpm);
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::BpmDown:
                    trCtl.bpm = std::clamp(trCtl.bpm - 1.0f, 20.0f, 300.0f);
                    (void)controlDispatcher.setTempoBpm(trCtl.bpm);
                    transport.setTempo(trCtl.bpm);
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::None:
                default:
                    break;
            }
        }
        (void)immediateDispatcher.sendStop(2, 0);
    });

    const auto renderUiOnce = [&]() {
        UiRuntimeTelemetryView telemetry{};
        telemetry.totalCallbacks = stream->totalCallbacks();
        telemetry.xruns = stream->xruns();
        telemetry.rtQueueOverflow =
                qUi.overflowFlagAndReset() || qRt.overflowFlagAndReset() || scheduler.overflowFlagAndReset();
        telemetry.blockFrames = static_cast<uint32_t>(stream->blockFrames());

        const UiState state = uiComposer.compose(uiStore.snapshot(), telemetry);
        const UiScene scene = activeUiScene.load(std::memory_order_acquire);
        if (scene == UiScene::Manager) {
            std::string frame;
            {
                std::lock_guard<std::mutex> lock(customFrameMutex);
                frame = customFrame;
            }
            if (!frame.empty()) {
                if (auto* windowRenderer = dynamic_cast<MacGbWindowRenderer*>(uiRenderer.get())) {
                    windowRenderer->renderCustomFrame(frame, /*showHeaderOverlay=*/false);
                } else if (auto* gbRenderer = dynamic_cast<GothicGbUiRenderer*>(uiRenderer.get())) {
                    gbRenderer->renderCustomFrame(frame);
                } else {
                    uiRenderer->render(state);
                }
            } else {
                uiRenderer->render(state);
            }
        } else {
            uiRenderer->render(state);
        }
    };

    std::thread uiThread;
    const bool renderOnMainThread = (windowUiInput != nullptr);
    if (!renderOnMainThread) {
        uiThread = std::thread([&]() {
            while (!stopUi.load(std::memory_order_acquire)) {
                renderUiOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
        });
    }

    while (!stopUi.load(std::memory_order_acquire)) {
        if (windowUiInput) {
            windowUiInput->pumpEvents();
            UiInputEvent ev{};
            while (windowUiInput->pollInput(ev)) {
                inputQueue.push(ev);
            }
            // macOS AppKit render on main thread for deterministic input latency.
            renderUiOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    stopUi.store(true, std::memory_order_release);
    if (controlThread.joinable()) {
        controlThread.join();
    }
    if (terminalInputThread.joinable()) {
        terminalInputThread.join();
    }
    if (uiThread.joinable()) {
        uiThread.join();
    }

    stream->stop();
    stream->close();
    return 0;
}
