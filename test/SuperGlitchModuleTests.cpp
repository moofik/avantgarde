#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "module/SuperGlitchModule.h"

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

float rms(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0f;
    }
    double e = 0.0;
    for (float x : v) {
        e += static_cast<double>(x) * static_cast<double>(x);
    }
    return static_cast<float>(std::sqrt(e / static_cast<double>(v.size())));
}

void fillInput(std::vector<float>& in, std::size_t seed) {
    for (std::size_t i = 0; i < in.size(); ++i) {
        const double t = static_cast<double>(i + seed);
        in[i] = static_cast<float>(0.6 * std::sin(2.0 * kPi * t / 51.0) +
                                   0.4 * std::sin(2.0 * kPi * t / 113.0));
    }
}

} // namespace

TEST_CASE("SuperGlitch: speed changes affect output immediately in running state") {
    SuperGlitchModule fxA{};
    SuperGlitchModule fxB{};
    fxA.init(48000.0, 512);
    fxB.init(48000.0, 512);
    fxA.reset();
    fxB.reset();

    for (SuperGlitchModule* fx : {&fxA, &fxB}) {
        fx->setParam(SuperGlitchModule::P_MIX, 1.0f);
        fx->setParam(SuperGlitchModule::P_PHRASE, 0.0f);
        fx->setParam(SuperGlitchModule::P_SUBSLICE, 0.0f);
        fx->setParam(SuperGlitchModule::P_RETRIG, 0.0f);
        fx->setParam(SuperGlitchModule::P_MODE, 0.0f);
        fx->setParam(SuperGlitchModule::P_HOLD, 1.0f);
        fx->setParam(SuperGlitchModule::P_SPEED, 0.5f); // neutral
        fx->beginBlock();
    }

    std::vector<float> in(4096, 0.0f);
    fillInput(in, 0);

    auto runBlock = [&](SuperGlitchModule& fx, std::vector<float>& out, uint64_t sampleTime) {
        const float* inA[2] = {in.data(), in.data()};
        float* outA[2] = {out.data(), out.data()};
        AudioProcessContext ctx{};
        ctx.in = inA;
        ctx.out = outA;
        ctx.nframes = in.size();
        ctx.transportValid = true;
        ctx.transportPlaying = true;
        ctx.transportBpm = 300.0f;
        ctx.transportTsNum = 4;
        ctx.transportTsDen = 4;
        ctx.transportSampleTime = sampleTime;
        fx.process(ctx);
    };

    std::vector<float> warmA(in.size(), 0.0f);
    std::vector<float> warmB(in.size(), 0.0f);
    uint64_t warmSample = 0;
    for (int i = 0; i < 4; ++i) {
        fillInput(in, static_cast<std::size_t>(i * 31));
        runBlock(fxA, warmA, warmSample);
        runBlock(fxB, warmB, warmSample);
        warmSample += static_cast<uint64_t>(in.size());
    }

    fxA.setParam(SuperGlitchModule::P_SPEED, 0.0f); // 0.25x
    fxB.setParam(SuperGlitchModule::P_SPEED, 1.0f); // 2.0x
    fxA.beginBlock();
    fxB.beginBlock();
    REQUIRE(fxA.getParam(SuperGlitchModule::P_SPEED) != fxB.getParam(SuperGlitchModule::P_SPEED));

    std::vector<float> outA(in.size(), 0.0f);
    std::vector<float> outB(in.size(), 0.0f);
    fillInput(in, 777);
    runBlock(fxA, outA, warmSample);
    runBlock(fxB, outB, warmSample);

    const float rmsA = rms(outA);
    const float rmsB = rms(outB);
    CAPTURE(rmsA, rmsB, outA[0], outB[0], outA[1], outB[1], outA[2], outB[2]);
    REQUIRE(rmsA > 1e-6f);
    REQUIRE(rmsB > 1e-6f);
    REQUIRE(maxAbsDiff(outA, outB) > 1e-4f);
}

TEST_CASE("SuperGlitch: transport rewind does not stall control reaction") {
    SuperGlitchModule fxA{};
    SuperGlitchModule fxB{};
    fxA.init(48000.0, 512);
    fxB.init(48000.0, 512);
    fxA.reset();
    fxB.reset();

    for (SuperGlitchModule* fx : {&fxA, &fxB}) {
        fx->setParam(SuperGlitchModule::P_MIX, 1.0f);
        fx->setParam(SuperGlitchModule::P_PHRASE, 0.0f);
        fx->setParam(SuperGlitchModule::P_SUBSLICE, 0.0f);
        fx->setParam(SuperGlitchModule::P_RETRIG, 0.9f); // retrig on
        fx->setParam(SuperGlitchModule::P_MODE, 0.0f);   // smooth (без gate-тишины)
        fx->setParam(SuperGlitchModule::P_HOLD, 1.0f);
        fx->setParam(SuperGlitchModule::P_SPEED, 0.5f);
        fx->beginBlock();
    }

    constexpr std::size_t kFrames = 512;
    std::vector<float> in(kFrames, 0.0f);
    std::vector<float> outA(kFrames, 0.0f);
    std::vector<float> outB(kFrames, 0.0f);

    auto runBlock = [&](SuperGlitchModule& fx, std::vector<float>& out, uint64_t sampleTime) {
        const float* inA[2] = {in.data(), in.data()};
        float* outA[2] = {out.data(), out.data()};
        AudioProcessContext ctx{};
        ctx.in = inA;
        ctx.out = outA;
        ctx.nframes = kFrames;
        ctx.transportValid = true;
        ctx.transportPlaying = true;
        ctx.transportBpm = 300.0f;
        ctx.transportTsNum = 4;
        ctx.transportTsDen = 4;
        ctx.transportSampleTime = sampleTime;
        fx.process(ctx);
    };

    uint64_t sampleTime = 0;
    for (int block = 0; block < 24; ++block) {
        fillInput(in, static_cast<std::size_t>(block * 17));
        runBlock(fxA, outA, sampleTime);
        runBlock(fxB, outB, sampleTime);
        sampleTime += kFrames;
    }

    // Эмулируем stop/start: transport sampleTime прыгает назад.
    fillInput(in, 999);
    runBlock(fxA, outA, 0);
    runBlock(fxB, outB, 0);

    fxA.setParam(SuperGlitchModule::P_SPEED, 0.0f); // slow
    fxB.setParam(SuperGlitchModule::P_SPEED, 1.0f); // fast
    fxA.beginBlock();
    fxB.beginBlock();
    REQUIRE(fxA.getParam(SuperGlitchModule::P_SPEED) != fxB.getParam(SuperGlitchModule::P_SPEED));

    sampleTime = kFrames;
    for (int block = 0; block < 4; ++block) {
        fillInput(in, static_cast<std::size_t>(200 + block * 13));
        runBlock(fxA, outA, sampleTime);
        runBlock(fxB, outB, sampleTime);
        sampleTime += kFrames;
    }

    REQUIRE(std::isfinite(outA[0]));
    REQUIRE(std::isfinite(outB[0]));
    REQUIRE(rms(outA) > 1e-6f);
    REQUIRE(maxAbsDiff(outA, outB) > 1e-4f);
}
