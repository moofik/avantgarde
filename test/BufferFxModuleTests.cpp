#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "module/BufferFxModule.h"

using namespace avantgarde;

namespace {

constexpr double kPi = 3.14159265358979323846;

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        m = std::max(m, std::fabs(a[i] - b[i]));
    }
    return m;
}

} // namespace

TEST_CASE("BufferFx: mix=0 is transparent passthrough") {
    BufferFxModule fx{};
    fx.init(48000.0, 512);
    fx.reset();
    fx.setParam(BufferFxModule::P_MIX, 0.0f);
    fx.beginBlock();

    std::vector<float> in(512, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 64.0);
    }
    std::vector<float> out(512, 0.0f);
    const float* inA[2] = {in.data(), in.data()};
    float* outA[2] = {out.data(), out.data()};
    AudioProcessContext ctx{};
    ctx.in = inA;
    ctx.out = outA;
    ctx.nframes = in.size();
    ctx.transportValid = true;
    ctx.transportPlaying = true;
    ctx.transportBpm = 120.0f;
    ctx.transportTsNum = 4;
    ctx.transportTsDen = 4;
    fx.process(ctx);

    REQUIRE(maxAbsDiff(in, out) < 1e-6f);
}

TEST_CASE("BufferFx: with retrig off still transforms signal when wet path is enabled") {
    BufferFxModule fx{};
    fx.init(48000.0, 512);
    fx.reset();
    fx.setParam(BufferFxModule::P_MIX, 1.0f);
    fx.setParam(BufferFxModule::P_RETRIG, 0.0f);
    fx.setParam(BufferFxModule::P_REPEAT, 0.6f);
    fx.beginBlock();

    std::vector<float> in(512, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 96.0);
    }
    std::vector<float> out(512, 0.0f);
    const float* inA[2] = {in.data(), in.data()};
    float* outA[2] = {out.data(), out.data()};
    AudioProcessContext ctx{};
    ctx.in = inA;
    ctx.out = outA;
    ctx.nframes = in.size();
    ctx.transportValid = true;
    ctx.transportPlaying = true;
    ctx.transportBpm = 120.0f;
    ctx.transportTsNum = 4;
    ctx.transportTsDen = 4;
    fx.process(ctx);

    REQUIRE(maxAbsDiff(in, out) > 1e-4f);
}

TEST_CASE("BufferFx: retrig on produces wet transformation") {
    BufferFxModule fx{};
    fx.init(48000.0, 512);
    fx.reset();
    fx.setParam(BufferFxModule::P_MIX, 1.0f);
    fx.setParam(BufferFxModule::P_SLICE_SIZE, 0.7f);
    fx.setParam(BufferFxModule::P_REPEAT, 0.8f);
    fx.setParam(BufferFxModule::P_SPEED, 0.3f);
    fx.setParam(BufferFxModule::P_RETRIG, 0.5f);
    fx.beginBlock();

    std::vector<float> in(24000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 48.0);
    }
    std::vector<float> out(24000, 0.0f);
    const float* inA[2] = {in.data(), in.data()};
    float* outA[2] = {out.data(), out.data()};
    AudioProcessContext ctx{};
    ctx.in = inA;
    ctx.out = outA;
    ctx.nframes = in.size();
    ctx.transportValid = true;
    ctx.transportPlaying = true;
    ctx.transportBpm = 120.0f;
    ctx.transportTsNum = 4;
    ctx.transportTsDen = 4;
    fx.process(ctx);

    // Не ожидаем точного значения, только факт трансформации.
    REQUIRE(maxAbsDiff(in, out) > 1e-4f);
}

TEST_CASE("BufferFx: jitter param is ignored in DSP (v2 policy)") {
    BufferFxModule fxA{};
    BufferFxModule fxB{};
    fxA.init(48000.0, 512);
    fxB.init(48000.0, 512);
    fxA.reset();
    fxB.reset();

    // Одинаковые параметры кроме JITTER.
    for (BufferFxModule* fx : {&fxA, &fxB}) {
        fx->setParam(BufferFxModule::P_MIX, 1.0f);
        fx->setParam(BufferFxModule::P_SLICE_SIZE, 0.7f);
        fx->setParam(BufferFxModule::P_REPEAT, 0.8f);
        fx->setParam(BufferFxModule::P_SPEED, 0.3f);
        fx->setParam(BufferFxModule::P_RETRIG, 0.5f);
    }
    fxA.setParam(BufferFxModule::P_JITTER, 0.0f);
    fxB.setParam(BufferFxModule::P_JITTER, 1.0f);
    fxA.beginBlock();
    fxB.beginBlock();

    std::vector<float> in(24000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 57.0);
    }
    std::vector<float> outA(in.size(), 0.0f);
    std::vector<float> outB(in.size(), 0.0f);
    const float* inPtr[2] = {in.data(), in.data()};
    float* outPtrA[2] = {outA.data(), outA.data()};
    float* outPtrB[2] = {outB.data(), outB.data()};

    AudioProcessContext ctxA{};
    ctxA.in = inPtr;
    ctxA.out = outPtrA;
    ctxA.nframes = in.size();
    ctxA.transportValid = true;
    ctxA.transportPlaying = true;
    ctxA.transportBpm = 120.0f;
    ctxA.transportTsNum = 4;
    ctxA.transportTsDen = 4;
    ctxA.transportSampleTime = 0;

    AudioProcessContext ctxB = ctxA;
    ctxB.out = outPtrB;

    fxA.process(ctxA);
    fxB.process(ctxB);

    REQUIRE(maxAbsDiff(outA, outB) < 1e-6f);
}

TEST_CASE("BufferFx: phrase params are latched and applied on boundary") {
    BufferFxModule fxA{};
    BufferFxModule fxB{};
    fxA.init(48000.0, 512);
    fxB.init(48000.0, 512);
    fxA.reset();
    fxB.reset();

    for (BufferFxModule* fx : {&fxA, &fxB}) {
        fx->setParam(BufferFxModule::P_MIX, 1.0f);
        fx->setParam(BufferFxModule::P_RETRIG, 0.0f);     // free-running phrase mode
        fx->setParam(BufferFxModule::P_REPEAT, 0.0f);     // repeat=1
        fx->setParam(BufferFxModule::P_SLICE_SIZE, 1.0f); // long slice to avoid boundary inside short test block
        fx->setParam(BufferFxModule::P_BUFFER_SIZE, 0.0f);
        fx->setParam(BufferFxModule::P_SPEED, 0.5f);      // neutral
    }

    std::vector<float> in(512, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(i) / 73.0);
    }
    const float* inPtr[2] = {in.data(), in.data()};

    std::vector<float> outA1(in.size(), 0.0f);
    std::vector<float> outB1(in.size(), 0.0f);
    float* outPtrA1[2] = {outA1.data(), outA1.data()};
    float* outPtrB1[2] = {outB1.data(), outB1.data()};

    AudioProcessContext ctx1{};
    ctx1.in = inPtr;
    ctx1.out = outPtrA1;
    ctx1.nframes = in.size();
    ctx1.transportValid = true;
    ctx1.transportPlaying = true;
    ctx1.transportBpm = 120.0f;
    ctx1.transportTsNum = 4;
    ctx1.transportTsDen = 4;
    ctx1.transportSampleTime = 0;

    AudioProcessContext ctx1b = ctx1;
    ctx1b.out = outPtrB1;

    fxA.beginBlock();
    fxB.beginBlock();
    fxA.process(ctx1);
    fxB.process(ctx1b);

    // Меняем phrase-boundary параметры только у B.
    fxB.setParam(BufferFxModule::P_BUFFER_SIZE, 1.0f);
    fxB.setParam(BufferFxModule::P_SLICE_SIZE, 0.0f);
    fxA.beginBlock();
    fxB.beginBlock();

    std::vector<float> outA2(in.size(), 0.0f);
    std::vector<float> outB2(in.size(), 0.0f);
    float* outPtrA2[2] = {outA2.data(), outA2.data()};
    float* outPtrB2[2] = {outB2.data(), outB2.data()};

    AudioProcessContext ctx2 = ctx1;
    ctx2.transportSampleTime = static_cast<uint64_t>(in.size());
    ctx2.out = outPtrA2;
    AudioProcessContext ctx2b = ctx2;
    ctx2b.out = outPtrB2;

    fxA.process(ctx2);
    fxB.process(ctx2b);

    // Поскольку граница фразы за этот короткий блок еще не наступила,
    // разницы быть не должно: новые значения только pending.
    REQUIRE(maxAbsDiff(outA2, outB2) < 1e-6f);
}
