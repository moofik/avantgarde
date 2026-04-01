#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "module/SchroederReverbModule.h"

using namespace avantgarde;

namespace {

struct StereoBlock {
    std::vector<float> inL;
    std::vector<float> inR;
    std::vector<float> outL;
    std::vector<float> outR;
    const float* inPtr[2]{};
    float* outPtr[2]{};
    AudioProcessContext ctx{};

    explicit StereoBlock(std::size_t n)
        : inL(n, 0.0f), inR(n, 0.0f), outL(n, 0.0f), outR(n, 0.0f) {
        inPtr[0] = inL.data();
        inPtr[1] = inR.data();
        outPtr[0] = outL.data();
        outPtr[1] = outR.data();
        ctx.in = inPtr;
        ctx.out = outPtr;
        ctx.nframes = n;
    }
};

float sumAbs(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) {
        s += std::abs(static_cast<double>(x));
    }
    return static_cast<float>(s);
}

} // namespace

TEST_CASE("SchroederReverb: wet=0 is dry passthrough") {
    SchroederReverbModule rev;
    rev.init(48000.0, 512);
    rev.setParam(SchroederReverbModule::P_WET, 0.0f);
    rev.setParam(SchroederReverbModule::P_ROOM, 0.7f);
    rev.setParam(SchroederReverbModule::P_DAMP, 0.4f);
    rev.beginBlock();

    StereoBlock b(512);
    for (std::size_t i = 0; i < b.inL.size(); ++i) {
        b.inL[i] = std::sin(static_cast<float>(i) * 0.01f);
        b.inR[i] = std::cos(static_cast<float>(i) * 0.015f);
    }

    rev.process(b.ctx);
    for (std::size_t i = 0; i < b.inL.size(); ++i) {
        REQUIRE(std::abs(b.outL[i] - b.inL[i]) < 1e-6f);
        REQUIRE(std::abs(b.outR[i] - b.inR[i]) < 1e-6f);
    }
}

TEST_CASE("SchroederReverb: impulse creates non-zero tail") {
    SchroederReverbModule rev;
    rev.init(48000.0, 512);
    rev.setParam(SchroederReverbModule::P_WET, 1.0f);
    rev.setParam(SchroederReverbModule::P_ROOM, 0.75f);
    rev.setParam(SchroederReverbModule::P_DAMP, 0.25f);
    rev.beginBlock();

    StereoBlock first(512);
    first.inL[0] = 1.0f;
    first.inR[0] = 1.0f;
    rev.process(first.ctx);

    // В первом блоке хвост может еще не прийти из-за длины delay.
    StereoBlock tail(4096);
    rev.beginBlock();
    rev.process(tail.ctx);

    const float tailEnergy = sumAbs(tail.outL) + sumAbs(tail.outR);
    REQUIRE(tailEnergy > 1e-3f);
}

TEST_CASE("SchroederReverb: output remains finite on long run") {
    SchroederReverbModule rev;
    rev.init(48000.0, 512);
    rev.setParam(SchroederReverbModule::P_WET, 0.6f);
    rev.setParam(SchroederReverbModule::P_ROOM, 0.9f);
    rev.setParam(SchroederReverbModule::P_DAMP, 0.35f);
    rev.beginBlock();

    StereoBlock b(256);
    std::fill(b.inL.begin(), b.inL.end(), 0.2f);
    std::fill(b.inR.begin(), b.inR.end(), 0.2f);

    for (int iter = 0; iter < 200; ++iter) {
        std::fill(b.outL.begin(), b.outL.end(), 0.0f);
        std::fill(b.outR.begin(), b.outR.end(), 0.0f);
        rev.beginBlock();
        rev.process(b.ctx);
        for (std::size_t i = 0; i < b.outL.size(); ++i) {
            REQUIRE(std::isfinite(b.outL[i]));
            REQUIRE(std::isfinite(b.outR[i]));
            REQUIRE(std::abs(b.outL[i]) < 8.0f);
            REQUIRE(std::abs(b.outR[i]) < 8.0f);
        }
    }
}

