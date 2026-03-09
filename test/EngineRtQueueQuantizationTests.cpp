#include <catch2/catch_all.hpp>

#include <memory>
#include <vector>

#include "contracts/IAudioEngine.h"
#include "contracts/IParamBridge.h"
#include "contracts/ITrack.h"
#include "contracts/IRtCommandQueue.h"
#include "contracts/ids.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/TransportBridgeDualBuffer.h"

using namespace avantgarde;

namespace avantgarde {
std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue*, IParamBridge*);
}

namespace {

struct MockParamBridge final : IParamBridge {
    void pushParam(Target, std::size_t, float) override {}
    void swapBuffers() override {}
};

struct MockQueue final : IRtCommandQueue {
    std::vector<RtCommand> q;

    bool push(const RtCommand& cmd) noexcept override {
        q.push_back(cmd);
        return true;
    }

    bool pop(RtCommand& out) noexcept override {
        if (q.empty()) return false;
        out = q.front();
        q.erase(q.begin());
        return true;
    }

    void clear() noexcept override { q.clear(); }
    std::size_t capacity() const noexcept override { return 1024; }
    std::size_t size() const noexcept override { return q.size(); }
    bool overflowFlagAndReset() noexcept override { return false; }
};

struct ProbeTrack final : ITrack {
    int* currentBlock = nullptr;
    int commandBlock = -1;
    int seenPlay = 0;

    void addModule(std::unique_ptr<IAudioModule>) override {}
    IAudioModule* getModule(std::size_t) override { return nullptr; }
    void process(const AudioProcessContext&) override {}

    void onRtCommand(const RtCommand& cmd) noexcept override {
        if (cmd.id == static_cast<uint16_t>(CmdId::Play)) {
            ++seenPlay;
            if (currentBlock) {
                commandBlock = *currentBlock;
            }
        }
    }
};

AudioProcessContext makeCtx(std::size_t nframes) {
    static std::vector<float> out0(1024, 0.0f);
    static std::vector<float> out1(1024, 0.0f);
    static float* outPtrs[2] = { out0.data(), out1.data() };

    AudioProcessContext ctx{};
    ctx.in = nullptr;
    ctx.out = outPtrs;
    ctx.nframes = nframes;
    return ctx;
}

RtCommand makeCmd(CmdId id, int16_t track, float value = 0.0f) {
    RtCommand cmd{};
    cmd.id = static_cast<uint16_t>(id);
    cmd.track = track;
    cmd.slot = 0;
    cmd.index = 0;
    cmd.value = value;
    return cmd;
}

} // namespace

TEST_CASE("Engine: quantize mode and play from rtQueue schedule on bar boundary") {
    MockQueue qUi;
    MockQueue qRt;
    MockParamBridge pb;
    auto engine = MakeAudioEngine(&qRt, &pb);

    auto track = std::make_unique<ProbeTrack>();
    ProbeTrack* t = track.get();

    int blockIndex = 0;
    t->currentBlock = &blockIndex;
    engine->registerTrack(std::move(track));

    TransportBridgeDualBuffer transport;
    transport.setTempo(120.0f);
    transport.setTimeSignature(4, 4);
    transport.swapBuffers();
    transport.advanceSampleTime(1000);
    engine->setTransportBridge(&transport);
    QuantizedSchedulerRtExtension scheduler(&qUi, &qRt, &transport, 48000.0);
    engine->addRtExtension(&scheduler);

    // UI path: all commands go via rtQueue.
    qUi.push(makeCmd(CmdId::QuantizeMode, /*track=*/-1, /*value=*/2.0f)); // Bar
    qUi.push(makeCmd(CmdId::Play, /*track=*/0, /*value=*/1.0f));

    const auto ctx = makeCtx(256);

    for (blockIndex = 0; blockIndex < 500; ++blockIndex) {
        engine->processBlock(ctx);
        if (t->seenPlay > 0) {
            break;
        }
    }

    REQUIRE(t->seenPlay == 1);
    REQUIRE(t->commandBlock == 371);
}
