#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "module/StutterModule.h"

using namespace avantgarde;

namespace {

AudioProcessContext makeCtx(const float* in0,
                            const float* in1,
                            float* out0,
                            float* out1,
                            std::size_t nframes,
                            float bpm,
                            uint64_t sampleTime,
                            bool playing = true) {
    static std::array<const float*, 2> inPtrs{};
    static std::array<float*, 2> outPtrs{};
    inPtrs[0] = in0;
    inPtrs[1] = in1 ? in1 : in0;
    outPtrs[0] = out0;
    outPtrs[1] = out1 ? out1 : out0;

    AudioProcessContext ctx{};
    ctx.in = inPtrs.data();
    ctx.out = outPtrs.data();
    ctx.nframes = nframes;
    ctx.transportValid = true;
    ctx.transportPlaying = playing;
    ctx.transportBpm = bpm;
    ctx.transportSampleTime = sampleTime;
    return ctx;
}

} // namespace

TEST_CASE("Stutter: wet=0 is transparent passthrough") {
    StutterModule m{};
    m.init(1000.0, 256);
    m.reset();
    m.setParam(StutterModule::P_WET, 0.0f);
    m.beginBlock();

    std::vector<float> in(128, 0.0f);
    std::vector<float> out(128, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(static_cast<float>(i) * 0.07f);
    }

    AudioProcessContext ctx = makeCtx(in.data(), nullptr, out.data(), nullptr, in.size(), 120.0f, 0);
    m.process(ctx);

    for (std::size_t i = 0; i < in.size(); ++i) {
        REQUIRE(out[i] == Catch::Approx(in[i]).margin(1e-4f));
    }
}

TEST_CASE("Stutter: retrigger off works as gate (gate=0 mutes wet path)") {
    StutterModule m{};
    m.init(1000.0, 256);
    m.reset();
    m.setParam(StutterModule::P_WET, 1.0f);
    m.setParam(StutterModule::P_RATE, 0.5f);
    m.setParam(StutterModule::P_RETRIGGER, 0.0f); // Off
    m.setParam(StutterModule::P_GATE, 0.0f);      // Fully closed
    m.beginBlock();

    std::vector<float> in(240, 1.0f);
    std::vector<float> out(240, 0.0f);

    AudioProcessContext ctx = makeCtx(in.data(), nullptr, out.data(), nullptr, in.size(), 120.0f, 0);
    m.process(ctx);

    float maxAbs = 0.0f;
    for (float s : out) {
        maxAbs = std::max(maxAbs, std::fabs(s));
    }
    REQUIRE(maxAbs < 1e-3f);
}

TEST_CASE("Stutter: retrigger on produces audible difference from dry signal") {
    StutterModule m{};
    m.init(2000.0, 512);
    m.reset();

    std::vector<float> inA(256, 0.0f);
    std::vector<float> outA(256, 0.0f);
    std::vector<float> inB(256, 0.0f);
    std::vector<float> outB(256, 0.0f);

    for (std::size_t i = 0; i < inA.size(); ++i) {
        inA[i] = static_cast<float>(i) / static_cast<float>(inA.size());
        inB[i] = static_cast<float>(i + inA.size()) / static_cast<float>(inA.size() * 2U);
    }

    // Блок A: прогреваем буфер.
    m.setParam(StutterModule::P_WET, 0.0f);
    m.setParam(StutterModule::P_RATE, 0.65f);
    m.setParam(StutterModule::P_GATE, 1.0f);
    m.setParam(StutterModule::P_RETRIGGER, 1.0f); // 16x
    m.beginBlock();
    AudioProcessContext ctxA = makeCtx(inA.data(), nullptr, outA.data(), nullptr, inA.size(), 120.0f, 0);
    m.process(ctxA);

    // Блок B: включаем stutter.
    m.setParam(StutterModule::P_WET, 1.0f);
    m.beginBlock();
    AudioProcessContext ctxB = makeCtx(inB.data(), nullptr, outB.data(), nullptr, inB.size(), 120.0f, inA.size());
    m.process(ctxB);

    float meanAbsDiff = 0.0f;
    for (std::size_t i = 0; i < inB.size(); ++i) {
        meanAbsDiff += std::fabs(outB[i] - inB[i]);
    }
    meanAbsDiff /= static_cast<float>(inB.size());
    REQUIRE(meanAbsDiff > 0.02f);
}

TEST_CASE("Stutter: retrigger loops same chunk inside retrigger period") {
    StutterModule m{};
    m.init(2000.0, 2048);
    m.reset();

    // При BPM=240 и SR=2000:
    // 1 beat = 500 сэмплов, bar=2000.
    // retrigger=2 => retrig-period=1000.
    // rate=0.35 -> gateStep ~= 0.5 beat = 250.
    // Ожидаем, что внутри retrig-period кусок 250 сэмплов лупится несколько раз.
    std::vector<float> in(4000, 0.0f);
    std::vector<float> out(4000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(i);
    }

    m.setParam(StutterModule::P_WET, 1.0f);
    m.setParam(StutterModule::P_RATE, 0.35f);
    m.setParam(StutterModule::P_GATE, 1.0f);
    m.setParam(StutterModule::P_RETRIGGER, 0.40f); // 2x
    m.beginBlock();

    AudioProcessContext ctx = makeCtx(in.data(), nullptr, out.data(), nullptr, in.size(), 240.0f, 0);
    m.process(ctx);

    // Внутри retrig-периода (1000..1999) луп 250 должен повторяться.
    float meanDiffLoop = 0.0f;
    for (std::size_t i = 0; i < 250; ++i) {
        meanDiffLoop += std::fabs(out[1000 + i] - out[1250 + i]);
        meanDiffLoop += std::fabs(out[1000 + i] - out[1500 + i]);
    }
    meanDiffLoop /= 500.0f;
    REQUIRE(meanDiffLoop < 1e-3f);

    // Но это не должно сваливаться в экстремально короткий "робо" период (например 32).
    float meanDiffShort = 0.0f;
    for (std::size_t i = 0; i < 218; ++i) {
        meanDiffShort += std::fabs(out[1000 + i] - out[1032 + i]);
    }
    meanDiffShort /= 218.0f;
    REQUIRE(meanDiffShort > 0.1f);
}
