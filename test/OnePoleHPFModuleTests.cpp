#include <catch2/catch_test_macros.hpp>
#include "module/OnePoleHPFModule.cpp"
#include <vector>
#include <cmath>

using namespace avantgarde;

static void makeSine(float freq, float sr, std::vector<float>& buf) {
    float ph = 0.0f, d = 2.0f * float(M_PI) * freq / sr;
    for (auto& s : buf) { s = std::sin(ph); ph += d; if (ph > 2*M_PI) ph -= 2*M_PI; }
}

TEST_CASE("OnePoleHPF: init/reset/process stable") {
    OnePoleHPFModule hpf;
    hpf.init(48000.0, 1024);
    hpf.reset();

    std::vector<float> in(256, 0.0f), out(256, 0.0f);
    const float* inA[1]  = { in.data() };
    float*       outA[1] = { out.data() };
    AudioProcessContext ctx{ inA, outA, in.size() };

    hpf.process(ctx);
    for (auto v : out) {
        REQUIRE(std::isfinite(v));
        REQUIRE(v == 0.0f);
    }
}

TEST_CASE("OnePoleHPF: low frequency attenuates stronger with higher cutoff") {
    OnePoleHPFModule hpf;
    hpf.init(48000.0, 2048);

    std::vector<float> in(1024), out(1024);
    makeSine(100.0f, 48000.0f, in);

    const float* inA[1]  = { in.data() };
    float*       outA[1] = { out.data() };
    AudioProcessContext ctx{ inA, outA, in.size() };

    hpf.setCutoff01(0.1f); hpf.reset(); hpf.process(ctx);
    float rmsLow = 0.0f; for (auto v: out) rmsLow += v*v; rmsLow = std::sqrt(rmsLow/out.size());

    hpf.setCutoff01(0.9f); hpf.reset(); hpf.process(ctx);
    float rmsHigh = 0.0f; for (auto v: out) rmsHigh += v*v; rmsHigh = std::sqrt(rmsHigh/out.size());

    REQUIRE(rmsHigh < rmsLow * 0.75f);
}
