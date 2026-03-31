#include <catch2/catch_all.hpp>

#include <vector>

#include "contracts/IRtCommandQueue.h"
#include "contracts/ids.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/TransportBridgeDualBuffer.h"

using namespace avantgarde;

namespace {

struct MockRtQueue final : IRtCommandQueue {
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
    std::size_t capacity() const noexcept override { return 4096; }
    std::size_t size() const noexcept override { return q.size(); }
    bool overflowFlagAndReset() noexcept override { return false; }
};

AudioProcessContext makeCtx(std::size_t nframes) {
    static std::vector<float> out0(1024, 0.0f);
    static float* outPtrs[1] = { out0.data() };
    AudioProcessContext ctx{};
    ctx.in = nullptr;
    ctx.out = outPtrs;
    ctx.nframes = nframes;
    return ctx;
}

RtCommand makeCmd(CmdId id, int16_t track, float value = 0.0f) {
    RtCommand cmd{};
    cmd.id = toWireCmdId(id);
    cmd.track = track;
    if (id == CmdId::QuantizeMode || id == CmdId::SetTempoBpm || id == CmdId::SetTimeSig) {
        cmd.slot = kRtSlotTrackParams;
    } else {
        cmd.slot = kRtClipSlot0;
    }
    cmd.index = kRtIndexUnused;
    cmd.value = value;
    return cmd;
}

} // namespace

TEST_CASE("QuantizedScheduler: passes through non-quantized commands") {
    MockRtQueue inQ;
    MockRtQueue outQ;
    TransportBridgeDualBuffer tr;
    tr.swapBuffers();

    QuantizedSchedulerRtExtension scheduler(&inQ, &outQ, &tr, 48000.0);

    inQ.push(makeCmd(CmdId::Play, 0));

    const auto ctx = makeCtx(256);
    scheduler.onBlockBegin(ctx);

    REQUIRE(outQ.size() == 1);
    CHECK(outQ.q[0].id == toWireCmdId(CmdId::Play));
}

TEST_CASE("QuantizedScheduler: QuantizeMode command sets internal mode") {
    MockRtQueue inQ;
    MockRtQueue outQ;
    TransportBridgeDualBuffer tr;
    tr.setTempo(120.0f);
    tr.setTimeSignature(4, 4);
    tr.swapBuffers();
    tr.advanceSampleTime(1000);

    QuantizedSchedulerRtExtension scheduler(&inQ, &outQ, &tr, 48000.0);

    inQ.push(makeCmd(CmdId::QuantizeMode, -1, 1.0f)); // Beat
    inQ.push(makeCmd(CmdId::Play, 0, 1.0f));

    const auto ctx = makeCtx(256);

    int firedAt = -1;
    for (int block = 0; block < 200; ++block) {
        tr.swapBuffers();
        scheduler.onBlockBegin(ctx);
        if (outQ.size() == 1) {
            firedAt = block;
            break;
        }
        tr.advanceSampleTime(256);
    }

    REQUIRE(firedAt == 89);
}

TEST_CASE("QuantizedScheduler: Quantize Bar aligns to next bar") {
    MockRtQueue inQ;
    MockRtQueue outQ;
    TransportBridgeDualBuffer tr;
    tr.setTempo(120.0f);
    tr.setTimeSignature(4, 4);
    tr.swapBuffers();
    tr.advanceSampleTime(50000);

    QuantizedSchedulerRtExtension scheduler(&inQ, &outQ, &tr, 48000.0);

    inQ.push(makeCmd(CmdId::QuantizeMode, -1, 2.0f)); // Bar
    inQ.push(makeCmd(CmdId::Play, 0, 1.0f));

    const auto ctx = makeCtx(512);

    int firedAt = -1;
    for (int block = 0; block < 300; ++block) {
        tr.swapBuffers();
        scheduler.onBlockBegin(ctx);
        if (outQ.size() == 1) {
            firedAt = block;
            break;
        }
        tr.advanceSampleTime(512);
    }

    REQUIRE(firedAt == 89);
}

TEST_CASE("QuantizedScheduler: Stop is immediate even when quantization mode is active") {
    MockRtQueue inQ;
    MockRtQueue outQ;
    TransportBridgeDualBuffer tr;
    tr.setTempo(120.0f);
    tr.setTimeSignature(4, 4);
    tr.swapBuffers();
    tr.advanceSampleTime(1000);

    QuantizedSchedulerRtExtension scheduler(&inQ, &outQ, &tr, 48000.0);

    inQ.push(makeCmd(CmdId::QuantizeMode, -1, 2.0f)); // Bar
    inQ.push(makeCmd(CmdId::Stop, 0, 0.0f));          // must bypass quantization

    const auto ctx = makeCtx(256);
    scheduler.onBlockBegin(ctx);

    REQUIRE(outQ.size() == 1);
    CHECK(outQ.q[0].id == toWireCmdId(CmdId::Stop));
}
