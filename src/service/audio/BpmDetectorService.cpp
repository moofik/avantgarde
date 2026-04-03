#include "service/audio/BpmDetectorService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace avantgarde {
namespace {

struct DecodedWavMono {
    int sampleRate{0};
    std::vector<float> mono{};
};

uint16_t readU16Le(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32Le(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0] |
                                 (p[1] << 8) |
                                 (p[2] << 16) |
                                 (p[3] << 24));
}

bool readExact(std::ifstream& in, void* dst, std::size_t n) {
    in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    return in.good();
}

bool decodeWavMono(const std::string& path, DecodedWavMono& out, std::string& error) {
    out = DecodedWavMono{};
    error.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "cannot open file";
        return false;
    }

    uint8_t riff[12]{};
    if (!readExact(in, riff, sizeof(riff))) {
        error = "cannot read RIFF header";
        return false;
    }
    if (std::memcmp(riff + 0, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }

    struct Fmt {
        uint16_t audioFormat{0};   // 1=PCM, 3=float
        uint16_t numChannels{0};   // 1..2
        uint32_t sampleRate{0};
        uint16_t bitsPerSample{0}; // 16/24/32
        uint16_t blockAlign{0};
    } fmt{};

    bool haveFmt = false;
    bool haveData = false;
    std::vector<uint8_t> data{};

    while (in.good() && !(haveFmt && haveData)) {
        uint8_t chdr[8]{};
        if (!readExact(in, chdr, sizeof(chdr))) {
            break;
        }
        const uint32_t csize = readU32Le(chdr + 4);
        const bool hasPad = (csize & 1U) != 0U;

        if (std::memcmp(chdr, "fmt ", 4) == 0) {
            if (csize < 16U) {
                error = "invalid fmt chunk";
                return false;
            }
            std::vector<uint8_t> buf(csize);
            if (!readExact(in, buf.data(), csize)) {
                error = "cannot read fmt chunk";
                return false;
            }
            fmt.audioFormat = readU16Le(buf.data() + 0);
            fmt.numChannels = readU16Le(buf.data() + 2);
            fmt.sampleRate = readU32Le(buf.data() + 4);
            fmt.blockAlign = readU16Le(buf.data() + 12);
            fmt.bitsPerSample = readU16Le(buf.data() + 14);
            haveFmt = true;
        } else if (std::memcmp(chdr, "data", 4) == 0) {
            data.resize(csize);
            if (csize > 0U && !readExact(in, data.data(), csize)) {
                error = "cannot read data chunk";
                return false;
            }
            haveData = true;
        } else {
            in.seekg(static_cast<std::streamoff>(csize), std::ios::cur);
            if (!in.good()) {
                error = "cannot skip unknown chunk";
                return false;
            }
        }

        if (hasPad) {
            in.seekg(1, std::ios::cur);
            if (!in.good()) {
                error = "cannot skip chunk padding";
                return false;
            }
        }
    }

    if (!haveFmt || !haveData) {
        error = "wav missing fmt/data chunk";
        return false;
    }
    if (!(fmt.audioFormat == 1U || fmt.audioFormat == 3U)) {
        error = "unsupported wav format (expect PCM/float)";
        return false;
    }
    if (!(fmt.numChannels == 1U || fmt.numChannels == 2U)) {
        error = "unsupported channel count";
        return false;
    }
    if (!(fmt.bitsPerSample == 16U || fmt.bitsPerSample == 24U || fmt.bitsPerSample == 32U)) {
        error = "unsupported bits per sample";
        return false;
    }
    if (fmt.sampleRate == 0U || fmt.blockAlign == 0U) {
        error = "invalid fmt values";
        return false;
    }

    const uint32_t frames = static_cast<uint32_t>(data.size() / fmt.blockAlign);
    if (frames == 0U) {
        error = "empty audio data";
        return false;
    }
    out.sampleRate = static_cast<int>(fmt.sampleRate);
    out.mono.assign(frames, 0.0f);

    auto clamp1 = [](float x) noexcept {
        return std::clamp(x, -1.0f, 1.0f);
    };

    const uint8_t* src = data.data();
    if (fmt.audioFormat == 3U && fmt.bitsPerSample == 32U) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float* fr = reinterpret_cast<const float*>(src + i * fmt.blockAlign);
            const float l = clamp1(fr[0]);
            const float r = (fmt.numChannels == 2U) ? clamp1(fr[1]) : l;
            out.mono[i] = 0.5f * (l + r);
        }
        return true;
    }

    if (fmt.audioFormat == 1U && fmt.bitsPerSample == 16U) {
        for (uint32_t i = 0; i < frames; ++i) {
            const uint8_t* fr = src + i * fmt.blockAlign;
            const int16_t s0 = static_cast<int16_t>(readU16Le(fr + 0));
            const float l = static_cast<float>(s0) / 32768.0f;
            float r = l;
            if (fmt.numChannels == 2U) {
                const int16_t s1 = static_cast<int16_t>(readU16Le(fr + 2));
                r = static_cast<float>(s1) / 32768.0f;
            }
            out.mono[i] = 0.5f * (l + r);
        }
        return true;
    }

    if (fmt.audioFormat == 1U && fmt.bitsPerSample == 24U) {
        auto readS24 = [](const uint8_t* p) noexcept {
            int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
            if ((v & 0x00800000) != 0) {
                v |= 0xFF000000;
            }
            return v;
        };

        for (uint32_t i = 0; i < frames; ++i) {
            const uint8_t* fr = src + i * fmt.blockAlign;
            const float l = static_cast<float>(readS24(fr + 0)) / 8388608.0f;
            float r = l;
            if (fmt.numChannels == 2U) {
                r = static_cast<float>(readS24(fr + 3)) / 8388608.0f;
            }
            out.mono[i] = 0.5f * (l + r);
        }
        return true;
    }

    error = "unsupported wav pcm mode";
    return false;
}

float bpmFromLag(int sampleRate, int hop, int lag) {
    if (sampleRate <= 0 || hop <= 0 || lag <= 0) {
        return 0.0f;
    }
    return (60.0f * static_cast<float>(sampleRate)) /
           (static_cast<float>(hop) * static_cast<float>(lag));
}

float normalizeToMusicalRange(float bpm) {
    if (!(bpm > 0.0f)) {
        return bpm;
    }
    while (bpm < 80.0f) {
        bpm *= 2.0f;
    }
    while (bpm > 170.0f) {
        bpm *= 0.5f;
    }
    return bpm;
}

float gaussianTempoPrior(float bpm) {
    // Мягкий музыкальный prior:
    // - не запрещает быстрые темпы,
    // - но слегка предпочитает «основной» диапазон around ~120.
    const float x = (bpm - 120.0f) / 34.0f;
    const float g = std::exp(-0.5f * x * x);
    return 0.55f + 0.45f * g;
}

float onePoleAlpha(float sampleRate, float cutoffHz) {
    if (!(sampleRate > 1.0f) || !(cutoffHz > 0.0f)) {
        return 0.0f;
    }
    const float x = (2.0f * 3.14159265358979323846f * cutoffHz) / sampleRate;
    return std::clamp(x, 0.0f, 1.0f);
}

void normalizeVec(std::vector<float>& v) {
    float mx = 0.0f;
    for (float x : v) {
        mx = std::max(mx, std::fabs(x));
    }
    if (mx <= 1e-12f) {
        return;
    }
    const float k = 1.0f / mx;
    for (float& x : v) {
        x *= k;
    }
}

} // namespace

BpmDetectionResult BpmDetectorService::detectFromFile(const std::string& path,
                                                      float trackSpeed) const {
    BpmDetectionResult out{};
    DecodedWavMono wav{};
    if (!decodeWavMono(path, wav, out.error)) {
        return out;
    }

    if (wav.sampleRate < 1000 || wav.mono.size() < static_cast<std::size_t>(wav.sampleRate / 2)) {
        out.error = "audio too short for bpm detection";
        return out;
    }

    constexpr int kHop = 256;
    const std::size_t envFrames = wav.mono.size() / static_cast<std::size_t>(kHop);
    if (envFrames < 32U) {
        out.error = "not enough envelope frames";
        return out;
    }

    std::vector<float> energyLow(envFrames, 0.0f);
    std::vector<float> energyHigh(envFrames, 0.0f);
    std::vector<float> energyFull(envFrames, 0.0f);
    const float alphaLow = onePoleAlpha(static_cast<float>(wav.sampleRate), 220.0f);
    float lpState = 0.0f;
    for (std::size_t i = 0; i < envFrames; ++i) {
        const std::size_t base = i * static_cast<std::size_t>(kHop);
        float sumLow = 0.0f;
        float sumHigh = 0.0f;
        float sumFull = 0.0f;
        for (int j = 0; j < kHop; ++j) {
            const float x = wav.mono[base + static_cast<std::size_t>(j)];
            lpState += alphaLow * (x - lpState);
            const float low = lpState;
            const float high = x - low;
            sumLow += std::fabs(low);
            sumHigh += std::fabs(high);
            sumFull += std::fabs(x);
        }
        energyLow[i] = std::log1p(sumLow / static_cast<float>(kHop));
        energyHigh[i] = std::log1p(sumHigh / static_cast<float>(kHop));
        energyFull[i] = std::log1p(sumFull / static_cast<float>(kHop));
    }

    std::vector<float> onset(envFrames, 0.0f);
    constexpr std::size_t kAdaptiveWin = 16U;
    float running = 0.0f;
    for (std::size_t i = 1; i < envFrames; ++i) {
        const float dLow = std::max(0.0f, energyLow[i] - energyLow[i - 1U]);
        const float dHigh = std::max(0.0f, energyHigh[i] - energyHigh[i - 1U]);
        const float dFull = std::max(0.0f, energyFull[i] - energyFull[i - 1U]);
        const float raw = 0.9f * dLow + 1.25f * dHigh + 0.55f * dFull;

        running += raw;
        if (i > kAdaptiveWin) {
            const float oldDLow = std::max(0.0f, energyLow[i - kAdaptiveWin] - energyLow[i - kAdaptiveWin - 1U]);
            const float oldDHigh = std::max(0.0f, energyHigh[i - kAdaptiveWin] - energyHigh[i - kAdaptiveWin - 1U]);
            const float oldDFull = std::max(0.0f, energyFull[i - kAdaptiveWin] - energyFull[i - kAdaptiveWin - 1U]);
            running -= (0.9f * oldDLow + 1.25f * oldDHigh + 0.55f * oldDFull);
        }
        const std::size_t denom = std::min<std::size_t>(i, kAdaptiveWin);
        const float localMean = (denom > 0) ? (running / static_cast<float>(denom)) : 0.0f;
        onset[i] = std::max(0.0f, raw - 0.9f * localMean);
    }
    normalizeVec(onset);

    constexpr float kMinBpm = 60.0f;
    constexpr float kMaxBpm = 200.0f;
    const int lagMin = std::max(1, static_cast<int>(std::lround((60.0 * wav.sampleRate) / (kHop * kMaxBpm))));
    int lagMax = std::max(lagMin + 1, static_cast<int>(std::lround((60.0 * wav.sampleRate) / (kHop * kMinBpm))));
    lagMax = std::min(lagMax, static_cast<int>(envFrames) - 2);
    if (lagMax <= lagMin) {
        out.error = "invalid lag range for bpm detection";
        return out;
    }

    auto corrAtLag = [&](int lag) noexcept {
        if (lag <= 0 || static_cast<std::size_t>(lag) >= onset.size()) {
            return 0.0f;
        }
        double num = 0.0;
        double denA = 0.0;
        double denB = 0.0;
        for (std::size_t i = static_cast<std::size_t>(lag); i < onset.size(); ++i) {
            const double a = onset[i];
            const double b = onset[i - static_cast<std::size_t>(lag)];
            num += a * b;
            denA += a * a;
            denB += b * b;
        }
        const double den = std::sqrt(denA * denB);
        if (den <= std::numeric_limits<double>::epsilon()) {
            return 0.0f;
        }
        return static_cast<float>(num / den);
    };

    std::vector<float> autoScore(static_cast<std::size_t>(lagMax + 1), 0.0f);
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        autoScore[static_cast<std::size_t>(lag)] = corrAtLag(lag);
    }

    std::vector<float> histScore(static_cast<std::size_t>(lagMax + 1), 0.0f);
    {
        std::vector<std::size_t> peaks{};
        float onsetMax = 0.0f;
        for (float x : onset) {
            onsetMax = std::max(onsetMax, x);
        }
        const float thr = std::max(0.02f, onsetMax * 0.28f);
        constexpr std::size_t kMinPeakGap = 8U;
        std::size_t lastPeak = 0;
        bool havePeak = false;
        for (std::size_t i = 1; i + 1 < onset.size(); ++i) {
            if (onset[i] < thr) {
                continue;
            }
            if (!(onset[i] >= onset[i - 1U] && onset[i] >= onset[i + 1U])) {
                continue;
            }
            if (havePeak && (i - lastPeak) < kMinPeakGap) {
                if (onset[i] > onset[lastPeak]) {
                    peaks.back() = i;
                    lastPeak = i;
                }
                continue;
            }
            peaks.push_back(i);
            lastPeak = i;
            havePeak = true;
        }

        for (std::size_t a = 0; a < peaks.size(); ++a) {
            const std::size_t ia = peaks[a];
            for (std::size_t b = a + 1; b < peaks.size(); ++b) {
                const std::size_t ib = peaks[b];
                const int lag = static_cast<int>(ib - ia);
                if (lag < lagMin) {
                    continue;
                }
                if (lag > lagMax) {
                    break;
                }
                const float w = onset[ia] * onset[ib];
                histScore[static_cast<std::size_t>(lag)] += w;
            }
        }
    }
    normalizeVec(histScore);

    int bestLag = lagMin;
    float bestScore = -1.0f;
    float secondScore = -1.0f;

    auto combinedLagScore = [&](int lag) noexcept {
        if (lag < lagMin || lag > lagMax) {
            return 0.0f;
        }
        float score = 0.72f * autoScore[static_cast<std::size_t>(lag)] +
                      0.95f * histScore[static_cast<std::size_t>(lag)];
        if (lag * 2 <= lagMax) {
            score += 0.30f * autoScore[static_cast<std::size_t>(lag * 2)];
            score += 0.22f * histScore[static_cast<std::size_t>(lag * 2)];
        }
        if (lag * 3 <= lagMax) {
            score += 0.10f * autoScore[static_cast<std::size_t>(lag * 3)];
        }
        if (lag / 2 >= lagMin) {
            score += 0.18f * autoScore[static_cast<std::size_t>(lag / 2)];
            score += 0.10f * histScore[static_cast<std::size_t>(lag / 2)];
        }
        return score;
    };

    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const float score = combinedLagScore(lag);

        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            bestLag = lag;
        } else if (score > secondScore) {
            secondScore = score;
        }
    }

    if (!(bestScore > 0.01f)) {
        out.error = "low confidence bpm result";
        return out;
    }

    const float rawBpm = bpmFromLag(wav.sampleRate, kHop, bestLag);
    float sourceBpm = normalizeToMusicalRange(rawBpm);

    // До-выбор между ритмически эквивалентными кандидатами:
    // иногда автокорреляция выбирает дробный/дотированный пульс (например 4/3).
    {
        struct Candidate {
            float bpm{0.0f};
            float lagScore{0.0f};
            float weighted{0.0f};
        };

        const float base = sourceBpm;
        const std::initializer_list<float> ratios = {
            1.0f, 0.5f, 2.0f, 2.0f / 3.0f, 3.0f / 2.0f, 3.0f / 4.0f, 4.0f / 3.0f
        };
        std::vector<Candidate> cands{};
        std::set<int> seenBpms{};
        cands.reserve(ratios.size());
        for (float r : ratios) {
            const float candBpm = normalizeToMusicalRange(base * r);
            const int bucket = static_cast<int>(std::lround(candBpm * 10.0f));
            if (seenBpms.find(bucket) != seenBpms.end()) {
                continue;
            }
            seenBpms.insert(bucket);
            if (!(candBpm >= kMinBpm && candBpm <= kMaxBpm)) {
                continue;
            }
            const int lag = static_cast<int>(std::lround((60.0f * static_cast<float>(wav.sampleRate)) /
                                                         (static_cast<float>(kHop) * candBpm)));
            const float lagScore = combinedLagScore(lag);
            cands.push_back(Candidate{
                .bpm = candBpm,
                .lagScore = lagScore,
                .weighted = lagScore * gaussianTempoPrior(candBpm),
            });
        }

        if (!cands.empty()) {
            std::size_t bestIdx = 0U;
            for (std::size_t i = 1; i < cands.size(); ++i) {
                if (cands[i].weighted > cands[bestIdx].weighted) {
                    bestIdx = i;
                }
            }

            // Если более медленный «базовый» темп почти не уступает по ритмической
            // согласованности, предпочитаем его (практический фикс против 4/3 overshoot).
            constexpr float kNearRatio = 0.86f;
            const float bestLagScore = std::max(1e-6f, cands[bestIdx].lagScore);
            for (std::size_t i = 0; i < cands.size(); ++i) {
                if (!(cands[i].bpm < cands[bestIdx].bpm)) {
                    continue;
                }
                if (cands[i].lagScore < (bestLagScore * kNearRatio)) {
                    continue;
                }
                if (gaussianTempoPrior(cands[i].bpm) < gaussianTempoPrior(cands[bestIdx].bpm)) {
                    continue;
                }
                bestIdx = i;
                break;
            }
            sourceBpm = cands[bestIdx].bpm;
        }
    }
    const float speed = std::clamp(trackSpeed, 0.25f, 4.0f);
    const float effectiveBpm = sourceBpm * speed;

    const float conf = std::clamp((bestScore - std::max(0.0f, secondScore)) /
                                      std::max(bestScore, 1e-6f),
                                  0.0f,
                                  1.0f);

    out.ok = true;
    out.sourceBpm = sourceBpm;
    out.effectiveBpm = effectiveBpm;
    out.confidence = conf;
    out.error.clear();
    return out;
}

} // namespace avantgarde
