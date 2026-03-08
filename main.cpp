// main.cpp
#include <cstdio>
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

#include "platform/macos/MacAudioHost.mm"   // твой
#include "platform/terminal/AnsiUiRenderer.h"
#include "runtime/AudioEngine.cpp"       // твой
#include "runtime/ClipTrack.cpp"  // твой
#include "runtime/ParamBridgeDualBuffer.cpp"  // твой
#include "runtime/RtCommandQueueSPSC.cpp"  // твой
#include "service/UiStateStore.h"

using namespace avantgarde;

static void RenderThunk(AudioProcessContext& ctx, void* user) noexcept {
    auto* engine = static_cast<IAudioEngine*>(user);
    engine->processBlock(ctx);
}

// RT extension: дергаем Play один раз на первом блоке
struct StartOncePlay final : IRtExtension {
    ITrack* track = nullptr;
    bool fired = false;

    void onBlockBegin(const AudioProcessContext& /*ctx*/) noexcept override {
        if (fired || !track) return;
        fired = true;

        RtCommand cmd{};
        cmd.id    = static_cast<uint16_t>(CmdId::Play);
        cmd.track = 0;   // трек #0 (первый зарегистрированный)
        cmd.slot  = 0;   // clip-slot 0 (MVP договорённость)
        cmd.index = 0;
        cmd.value = 0.0f;

        track->onRtCommand(cmd);
    }

    void onBlockEnd(const AudioProcessContext& /*ctx*/) noexcept override {}
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: %s /path/to/sample.wav\n", argv[0]);
        return 1;
    }
    const char* wavPath = argv[1];
    std::string clipName = wavPath;
    if (const std::size_t pos = clipName.find_last_of("/\\"); pos != std::string::npos) {
        clipName = clipName.substr(pos + 1);
    }

    // 1) Host
    MacAudioHost host;
    RtCommandQueueSPSC q;
    ParamBridgeDualBuffer pb(10);

    // 2) Engine
    AudioEngine engine(&q, &pb);
    engine.setSampleRate(48000.0);

    // 3) Clip track
    auto clip = std::make_unique<ClipTrackImpl>();
    IClipTrack* clipPtr = clip.get(); // сохраним указатель до move()

    // 4) Load sample (non-RT!)
    if (!clipPtr->loadSlotFromFile(0, wavPath)) {
        std::printf("loadSlotFromFile failed: %s\n", wavPath);
        return 2;
    }
    clipPtr->setSlotLooping(0, true); // чтоб не кончилось через 1 секунду

    // UI state (service-side)
    UiStateStore uiStore;
    AnsiUiRenderer uiRenderer;

    UiTransportState trUi{};
    trUi.playing = true;
    trUi.bpm = 120.0f;
    trUi.tsNum = 4;
    trUi.tsDen = 4;
    trUi.quant = QuantizeMode::Bar;
    uiStore.setTransport(trUi);

    UiTrackStateView track0{};
    track0.id = 0;
    track0.state = UiTrackState::Playing;
    track0.bars = 4;
    track0.stretchRatio = 1.0f;
    track0.gain01 = 1.0f;
    track0.loop = true;
    track0.fxCount = 0;
    track0.clipName = clipName;
    uiStore.setTrack(0, track0);

    UiTrackStateView track1{};
    track1.id = 1;
    track1.state = UiTrackState::Empty;
    track1.bars = 4;
    track1.stretchRatio = 1.0f;
    track1.gain01 = 1.0f;
    track1.loop = false;
    track1.fxCount = 0;
    track1.clipName.clear();
    uiStore.setTrack(1, track1);

    // 5) Register track (non-RT)
    engine.registerTrack(std::move(clip));

    // 6) RT hook to fire Play on first block
    StartOncePlay start{};
    start.track = clipPtr;
    engine.addRtExtension(&start);

    // 7) Run audio
    StreamConfig cfg{ .sampleRate=48000, .blockFrames=256, .numInput=0, .numOutput=2 };
    auto stream = host.openStream(cfg, "default", "default");

    engine.setNumOutput((uint32_t)stream->numOutput());

    if (!stream) {
        std::printf("openStream failed\n");
        return 3;
    }
    if (!stream->start(&RenderThunk, &engine)) {
        std::printf("stream->start failed\n");
        return 4;
    }

    std::atomic<bool> stopUi{false};
    std::thread uiThread([&]() {
        while (!stopUi.load(std::memory_order_acquire)) {
            UiState state = uiStore.snapshot();
            state.telemetry.totalCallbacks = stream->totalCallbacks();
            state.telemetry.xruns = stream->xruns();
            state.telemetry.rtQueueOverflow = q.overflowFlagAndReset();
            state.transport.sampleTime = state.telemetry.totalCallbacks * static_cast<uint64_t>(stream->blockFrames());
            uiRenderer.render(state);
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
    });

    (void)std::getchar();

    stopUi.store(true, std::memory_order_release);
    if (uiThread.joinable()) {
        uiThread.join();
    }

    stream->stop();
    stream->close();
    return 0;
}
