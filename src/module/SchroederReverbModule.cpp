#include "module/SchroederReverbModule.h"

#include <algorithm>
#include <cmath>

namespace avantgarde {
namespace {

// Базовые длины delay-линий (как в классических реализациях 44.1k).
constexpr std::array<int, 4> kCombBaseL = {1116, 1188, 1277, 1356};
constexpr std::array<int, 4> kCombBaseR = {1139, 1211, 1300, 1379}; // L + 23
constexpr std::array<int, 2> kApBaseL = {556, 441};
constexpr std::array<int, 2> kApBaseR = {579, 464}; // L + 23

constexpr float kAllpassFeedback = 0.5f;
// Входной драйв ревера.
// Повышен относительно классического freeverb-уровня,
// чтобы wet ощущался равномернее в диапазоне 0..1.
constexpr float kInputGain = 0.04f;

} // namespace

SchroederReverbModule::SchroederReverbModule() noexcept {
    meta_[P_WET] = {"Wet", 0.0f, 1.0f, false, "norm"};
    meta_[P_ROOM] = {"Room", 0.0f, 1.0f, false, "norm"};
    meta_[P_DAMP] = {"Damp", 0.0f, 1.0f, false, "norm"};
    meta_[P_WIDTH] = {"Width", 0.0f, 1.0f, false, "norm"};
}

void SchroederReverbModule::init(double sampleRate, std::size_t /*maxFrames*/) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    configureDelayLines_();
    read_ = write_.load(std::memory_order_relaxed);
    recalcFromParams_(read_);
    reset();
}

void SchroederReverbModule::beginBlock() noexcept {
    read_ = write_.load(std::memory_order_relaxed);
    recalcFromParams_(read_);
}

void SchroederReverbModule::process(const AudioProcessContext& ctx) noexcept {
    if (!ctx.out || !ctx.in) {
        return;
    }
    const float* inL = ctx.in[0];
    const float* inR = ctx.in[1] ? ctx.in[1] : ctx.in[0];
    float* outL = ctx.out[0];
    float* outR = ctx.out[1] ? ctx.out[1] : ctx.out[0];
    if (!inL || !outL) {
        return;
    }

    for (std::size_t i = 0; i < ctx.nframes; ++i) {
        const float dryL = inL[i];
        const float dryR = inR ? inR[i] : dryL;
        const float monoIn = (dryL + dryR) * 0.5f * kInputGain;

        float accL = 0.0f;
        float accR = 0.0f;
        for (std::size_t c = 0; c < combL_.size(); ++c) {
            accL += processComb_(combL_[c], monoIn, feedback_, dampA_, dampB_);
            accR += processComb_(combR_[c], monoIn, feedback_, dampA_, dampB_);
        }

        float revL = accL;
        float revR = accR;
        for (std::size_t a = 0; a < apL_.size(); ++a) {
            revL = processAllpass_(apL_[a], revL, kAllpassFeedback);
            revR = processAllpass_(apR_[a], revR, kAllpassFeedback);
        }

        // Stereo-mix с cross-компонентом (width управляет "разъездом" каналов).
        outL[i] = dry_ * dryL + wet1_ * revL + wet2_ * revR;
        outR[i] = dry_ * dryR + wet1_ * revR + wet2_ * revL;
    }
}

void SchroederReverbModule::reset() {
    for (Comb& c : combL_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.idx = 0;
        c.lpState = 0.0f;
    }
    for (Comb& c : combR_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.idx = 0;
        c.lpState = 0.0f;
    }
    for (Allpass& a : apL_) {
        std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        a.idx = 0;
    }
    for (Allpass& a : apR_) {
        std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        a.idx = 0;
    }
}

std::size_t SchroederReverbModule::getParamCount() const {
    return NUM_PARAMS;
}

float SchroederReverbModule::getParam(std::size_t index) const {
    switch (index) {
        case P_WET: return read_.wet;
        case P_ROOM: return read_.room;
        case P_DAMP: return read_.damp;
        case P_WIDTH: return read_.width;
        default: return 0.0f;
    }
}

void SchroederReverbModule::setParam(std::size_t index, float value) {
    Params p = write_.load(std::memory_order_relaxed);
    switch (index) {
        case P_WET:
            p.wet = clamp01_(value);
            break;
        case P_ROOM:
            p.room = clamp01_(value);
            break;
        case P_DAMP:
            p.damp = clamp01_(value);
            break;
        case P_WIDTH:
            p.width = clamp01_(value);
            break;
        default:
            break;
    }
    write_.store(p, std::memory_order_relaxed);
}

const ParamMeta& SchroederReverbModule::getParamMeta(std::size_t index) const {
    return meta_[index];
}

float SchroederReverbModule::processComb_(Comb& c, float in, float feedback, float dampA, float dampB) noexcept {
    if (c.buf.empty()) {
        return 0.0f;
    }
    const float out = c.buf[c.idx];
    c.lpState = out * dampB + c.lpState * dampA;
    c.buf[c.idx] = in + c.lpState * feedback;
    c.idx = (c.idx + 1U >= c.buf.size()) ? 0U : c.idx + 1U;
    return out;
}

float SchroederReverbModule::processAllpass_(Allpass& a, float in, float feedback) noexcept {
    if (a.buf.empty()) {
        return in;
    }
    const float bufOut = a.buf[a.idx];
    const float out = -in + bufOut;
    a.buf[a.idx] = in + bufOut * feedback;
    a.idx = (a.idx + 1U >= a.buf.size()) ? 0U : a.idx + 1U;
    return out;
}

void SchroederReverbModule::recalcFromParams_(const Params& p) noexcept {
    const float wetCtrl = clamp01_(p.wet);
    const float room = clamp01_(p.room);
    const float damp = clamp01_(p.damp);
    const float width = clamp01_(p.width);

    // Room -> RT60 (сек). Параметр room растет линейно по времени хвоста,
    // а feedback вычисляется из RT60 по экспоненциальной формуле затухания.
    constexpr float kRt60MinSec = 0.35f;
    constexpr float kRt60MaxSec = 8.0f;
    constexpr float kRefDelaySec = 0.03f; // референс для усредненного comb-delay
    const float rt60Sec = kRt60MinSec + room * (kRt60MaxSec - kRt60MinSec);
    const float fb = std::pow(10.0f, (-3.0f * kRefDelaySec) / std::max(0.05f, rt60Sec));
    feedback_ = std::clamp(fb, 0.30f, 0.985f);

    // Damp: 0 -> ярко/долго по ВЧ, 1 -> сильнее затухают ВЧ.
    dampA_ = 0.08f + damp * 0.88f;
    dampB_ = 1.0f - dampA_;

    // Wet-модель:
    // - dry линейно уменьшается с ростом wet;
    // - wet-path получает квадратичную makeup-компенсацию, чтобы high-wet
    //   не "проваливался" по громкости и был заметен уже в среднем диапазоне.
    const float wet = wetCtrl;
    const float dryGain = 1.0f - wet;
    const float wetGain = wet * (0.4f + 7.6f * wet);

    const float w1 = wetGain * (0.5f + 0.5f * width);
    const float w2 = wetGain * (0.5f * (1.0f - width));
    wet1_ = w1;
    wet2_ = w2;
    dry_ = dryGain;
}

void SchroederReverbModule::configureDelayLines_() {
    const double ratio = sampleRate_ / 44100.0;
    auto scaled = [ratio](int baseSamples) -> std::size_t {
        const int d = std::max(1, static_cast<int>(std::lround(static_cast<double>(baseSamples) * ratio)));
        return static_cast<std::size_t>(d);
    };

    for (std::size_t i = 0; i < combL_.size(); ++i) {
        combL_[i].buf.assign(scaled(kCombBaseL[i]), 0.0f);
        combL_[i].idx = 0;
        combL_[i].lpState = 0.0f;
        combR_[i].buf.assign(scaled(kCombBaseR[i]), 0.0f);
        combR_[i].idx = 0;
        combR_[i].lpState = 0.0f;
    }
    for (std::size_t i = 0; i < apL_.size(); ++i) {
        apL_[i].buf.assign(scaled(kApBaseL[i]), 0.0f);
        apL_[i].idx = 0;
        apR_[i].buf.assign(scaled(kApBaseR[i]), 0.0f);
        apR_[i].idx = 0;
    }
}

float SchroederReverbModule::clamp01_(float v) noexcept {
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace avantgarde
