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

float rms(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0f;
    }
    double s2 = 0.0;
    for (float x : v) {
        const double xd = static_cast<double>(x);
        s2 += xd * xd;
    }
    return static_cast<float>(std::sqrt(s2 / static_cast<double>(v.size())));
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

TEST_CASE("SchroederReverb: wet has audible growth in mid range") {
    auto tailEnergyForWet = [](float wet) {
        SchroederReverbModule rev;
        rev.init(48000.0, 512);
        rev.setParam(SchroederReverbModule::P_WET, wet);
        rev.setParam(SchroederReverbModule::P_ROOM, 0.75f);
        rev.setParam(SchroederReverbModule::P_DAMP, 0.25f);
        rev.setParam(SchroederReverbModule::P_WIDTH, 0.9f);
        rev.beginBlock();
        REQUIRE(std::abs(rev.getParam(SchroederReverbModule::P_WET) - wet) < 1e-6f);

        StereoBlock first(512);
        first.inL[0] = 1.0f;
        first.inR[0] = 1.0f;
        rev.process(first.ctx);

        StereoBlock tail(4096);
        rev.beginBlock();
        rev.process(tail.ctx);
        return sumAbs(tail.outL) + sumAbs(tail.outR);
    };

    const float e03 = tailEnergyForWet(0.3f);
    const float e07 = tailEnergyForWet(0.7f);
    REQUIRE(e03 > 1e-4f);
    REQUIRE(e07 > e03 * 1.6f);
}

TEST_CASE("SchroederReverb: high wet keeps reasonable loudness") {
    auto steadyRmsForWet = [](float wet) {
        SchroederReverbModule rev;
        rev.init(48000.0, 256);
        rev.setParam(SchroederReverbModule::P_WET, wet);
        rev.setParam(SchroederReverbModule::P_ROOM, 0.8f);
        rev.setParam(SchroederReverbModule::P_DAMP, 0.3f);
        rev.setParam(SchroederReverbModule::P_WIDTH, 1.0f);

        StereoBlock b(256);
        float rmsAcc = 0.0f;
        int rmsBlocks = 0;
        for (int iter = 0; iter < 40; ++iter) {
            for (std::size_t i = 0; i < b.inL.size(); ++i) {
                const float x = std::sin((static_cast<float>(iter * 256 + static_cast<int>(i)) * 0.013f)) * 0.25f;
                b.inL[i] = x;
                b.inR[i] = x;
                b.outL[i] = 0.0f;
                b.outR[i] = 0.0f;
            }
            rev.beginBlock();
            rev.process(b.ctx);
            if (iter >= 25) {
                rmsAcc += 0.5f * (rms(b.outL) + rms(b.outR));
                ++rmsBlocks;
            }
        }
        return (rmsBlocks > 0) ? (rmsAcc / static_cast<float>(rmsBlocks)) : 0.0f;
    };

    const float dryRms = steadyRmsForWet(0.0f);
    const float wetRms = steadyRmsForWet(1.0f);
    REQUIRE(dryRms > 1e-4f);
    REQUIRE(wetRms > dryRms * 0.65f);
}
