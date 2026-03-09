// main.cpp
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

#include "contracts/types.h"
#include "contracts/IPlatform.h"
#include "contracts/IAudioEngine.h"
#include "contracts/IRtExtension.h"
#include "contracts/IClipTrack.h"
#include "contracts/IUi.h"
#include "contracts/ids.h"   // где у тебя CmdId enum (или include из contracts)

#include "control/ControlCommandDispatcher.h"
#include "control/TerminalUiInput.h"
#include "platform/macos/MacAudioHost.mm"   // твой
#include "platform/terminal/AnsiUiRenderer.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/AudioEngine.cpp"       // твой
#include "runtime/ClipTrack.cpp"  // твой
#include "runtime/ParamBridgeDualBuffer.cpp"  // твой
#include "runtime/RtCommandQueueSPSC.cpp"  // твой
#include "runtime/TransportBridgeDualBuffer.h"
#include "service/UiStateComposer.h"
#include "service/UiStateStore.h"

using namespace avantgarde;

static void RenderThunk(AudioProcessContext& ctx, void* user) noexcept {
    auto* engine = static_cast<IAudioEngine*>(user);
    engine->processBlock(ctx);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: %s /path/to/track1.wav [/path/to/track2.wav]\n", argv[0]);
        return 1;
    }
    const char* wavPath0 = argv[1];
    const char* wavPath1 = (argc >= 3) ? argv[2] : nullptr;

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

    // UI state (service-side)
    UiStateStore uiStore;
    UiStateComposer uiComposer;
    AnsiUiRenderer uiRenderer;

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

    // 5) Register track (non-RT)
    engine.registerTrack(std::move(clip0));
    engine.registerTrack(std::move(clip1));

    // 6) Transport + quantized scheduler
    engine.setTransportBridge(&transport);
    QuantizedSchedulerRtExtension scheduler(&qUi, &qRt, &transport, 48000.0);
    engine.addRtExtension(&scheduler);
    ControlCommandDispatcher controlDispatcher(&qUi);

    // Set initial quantization mode via UI command path.
    (void)controlDispatcher.setQuantizeMode(QuantizeMode::Bar);

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
    std::thread controlThread([&]() {
        TerminalUiInput input;
        std::array<UiTrackStateView, 2> tracksCtl{track0, track1};
        UiTransportState trCtl = trUi;
        const auto recomputePlaying = [&]() noexcept {
            return tracksCtl[0].state == UiTrackState::Playing || tracksCtl[1].state == UiTrackState::Playing;
        };

        while (!stopUi.load(std::memory_order_acquire)) {
            UiInputEvent ev{};
            if (!input.poll(ev)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            switch (ev.action) {
                case UiInputAction::Quit:
                    stopUi.store(true, std::memory_order_release);
                    break;
                case UiInputAction::SelectTrack0:
                    trCtl.activeTrack = 0;
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::SelectTrack1:
                    trCtl.activeTrack = 1;
                    uiStore.setTransport(trCtl);
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
                    (void)controlDispatcher.sendParamSet(static_cast<int16_t>(t), 0, 2, tracksCtl[t].stretchRatio);
                    uiStore.setTrack(t, tracksCtl[t]);
                } break;
                case UiInputAction::TrackSpeedDown: {
                    const uint8_t t = trCtl.activeTrack > 1 ? 1 : trCtl.activeTrack;
                    tracksCtl[t].stretchRatio = std::clamp(tracksCtl[t].stretchRatio - 0.05f, 0.25f, 4.0f);
                    (void)controlDispatcher.sendParamSet(static_cast<int16_t>(t), 0, 2, tracksCtl[t].stretchRatio);
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
                    transport.setTempo(trCtl.bpm);
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::BpmDown:
                    trCtl.bpm = std::clamp(trCtl.bpm - 1.0f, 20.0f, 300.0f);
                    transport.setTempo(trCtl.bpm);
                    uiStore.setTransport(trCtl);
                    break;
                case UiInputAction::None:
                default:
                    break;
            }
        }
    });

    std::thread uiThread([&]() {
        while (!stopUi.load(std::memory_order_acquire)) {
            UiRuntimeTelemetryView telemetry{};
            telemetry.totalCallbacks = stream->totalCallbacks();
            telemetry.xruns = stream->xruns();
            telemetry.rtQueueOverflow =
                    qUi.overflowFlagAndReset() || qRt.overflowFlagAndReset() || scheduler.overflowFlagAndReset();
            telemetry.blockFrames = static_cast<uint32_t>(stream->blockFrames());

            const UiState state = uiComposer.compose(uiStore.snapshot(), telemetry);
            uiRenderer.render(state);
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
    });

    while (!stopUi.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stopUi.store(true, std::memory_order_release);
    if (controlThread.joinable()) {
        controlThread.join();
    }
    if (uiThread.joinable()) {
        uiThread.join();
    }

    stream->stop();
    stream->close();
    return 0;
}
