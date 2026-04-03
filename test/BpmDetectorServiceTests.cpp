#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "service/audio/BpmDetectorService.h"

namespace fs = std::filesystem;

namespace {

void writeU16Le(std::ofstream& f, uint16_t v) {
    const uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xFFU),
        static_cast<uint8_t>((v >> 8) & 0xFFU),
    };
    f.write(reinterpret_cast<const char*>(b), 2);
}

void writeU32Le(std::ofstream& f, uint32_t v) {
    const uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFFU),
        static_cast<uint8_t>((v >> 8) & 0xFFU),
        static_cast<uint8_t>((v >> 16) & 0xFFU),
        static_cast<uint8_t>((v >> 24) & 0xFFU),
    };
    f.write(reinterpret_cast<const char*>(b), 4);
}

fs::path writeClickWav(const fs::path& path, int sampleRate, float bpm, float seconds) {
    const int totalFrames = static_cast<int>(seconds * static_cast<float>(sampleRate));
    std::vector<int16_t> mono(totalFrames, 0);
    const int beatFrames = static_cast<int>((60.0f / bpm) * static_cast<float>(sampleRate));
    for (int i = 0; i < totalFrames; i += beatFrames) {
        const int clickLen = std::min(64, totalFrames - i);
        for (int k = 0; k < clickLen; ++k) {
            // Короткий затухающий импульс.
            const float g = 1.0f - static_cast<float>(k) / static_cast<float>(clickLen);
            mono[i + k] = static_cast<int16_t>(std::lround(28000.0f * g));
        }
    }

    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(mono.size() * sizeof(int16_t));
    const uint32_t riffSize = 4U + (8U + 16U) + (8U + dataSize);

    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f.write("RIFF", 4);
    writeU32Le(f, riffSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32Le(f, 16);
    writeU16Le(f, 1);
    writeU16Le(f, channels);
    writeU32Le(f, static_cast<uint32_t>(sampleRate));
    writeU32Le(f, byteRate);
    writeU16Le(f, blockAlign);
    writeU16Le(f, bits);

    f.write("data", 4);
    writeU32Le(f, dataSize);
    f.write(reinterpret_cast<const char*>(mono.data()), static_cast<std::streamsize>(dataSize));
    f.flush();
    REQUIRE(f.good());
    return path;
}

fs::path writeBeatAndTripletWav(const fs::path& path, int sampleRate, float bpm, float seconds) {
    const int totalFrames = static_cast<int>(seconds * static_cast<float>(sampleRate));
    std::vector<int16_t> mono(totalFrames, 0);
    const int beatFrames = static_cast<int>((60.0f / bpm) * static_cast<float>(sampleRate));
    const int tripletFrames = static_cast<int>(std::lround((60.0f / (bpm * 4.0f / 3.0f)) * static_cast<float>(sampleRate)));

    auto addClick = [&](int pos, int len, float amp) {
        if (pos < 0 || pos >= totalFrames) {
            return;
        }
        const int cl = std::min(len, totalFrames - pos);
        for (int k = 0; k < cl; ++k) {
            const float g = 1.0f - static_cast<float>(k) / static_cast<float>(cl);
            const float s = amp * g;
            const int v = static_cast<int>(mono[pos + k]) + static_cast<int>(std::lround(s));
            mono[pos + k] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
        }
    };

    // Базовый метроном (истинный BPM).
    for (int i = 0; i < totalFrames; i += beatFrames) {
        addClick(i, 64, 20000.0f);
    }
    // Более тихий триольный слой: проверяем, что детект не уходит в 4/3 tempo,
    // когда базовый пульс все еще доминирует.
    for (int i = 0; i < totalFrames; i += tripletFrames) {
        addClick(i, 20, 12000.0f);
    }

    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(mono.size() * sizeof(int16_t));
    const uint32_t riffSize = 4U + (8U + 16U) + (8U + dataSize);

    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f.write("RIFF", 4);
    writeU32Le(f, riffSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32Le(f, 16);
    writeU16Le(f, 1);
    writeU16Le(f, channels);
    writeU32Le(f, static_cast<uint32_t>(sampleRate));
    writeU32Le(f, byteRate);
    writeU16Le(f, blockAlign);
    writeU16Le(f, bits);

    f.write("data", 4);
    writeU32Le(f, dataSize);
    f.write(reinterpret_cast<const char*>(mono.data()), static_cast<std::streamsize>(dataSize));
    f.flush();
    REQUIRE(f.good());
    return path;
}

} // namespace

TEST_CASE("BpmDetectorService: detects BPM from click loop and applies track speed") {
    const fs::path p = fs::temp_directory_path() / "avantgarde_bpm_detect_click.wav";
    writeClickWav(p, /*sampleRate=*/48000, /*bpm=*/120.0f, /*seconds=*/10.0f);

    avantgarde::BpmDetectorService detector{};
    const avantgarde::BpmDetectionResult r = detector.detectFromFile(p.string(), /*trackSpeed=*/1.5f);

    REQUIRE(r.ok);
    REQUIRE(r.sourceBpm > 115.0f);
    REQUIRE(r.sourceBpm < 125.0f);
    REQUIRE(r.effectiveBpm > 170.0f);
    REQUIRE(r.effectiveBpm < 190.0f);
    REQUIRE(r.confidence > 0.01f);
}

TEST_CASE("BpmDetectorService: returns error for missing file") {
    avantgarde::BpmDetectorService detector{};
    const avantgarde::BpmDetectionResult r =
        detector.detectFromFile("/tmp/avantgarde_missing_file_for_bpm.wav", 1.0f);
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
}

TEST_CASE("BpmDetectorService: prefers base tempo over triplet feel candidate") {
    const fs::path p = fs::temp_directory_path() / "avantgarde_bpm_detect_triplet_bias.wav";
    writeBeatAndTripletWav(p, /*sampleRate=*/48000, /*bpm=*/114.0f, /*seconds=*/12.0f);

    avantgarde::BpmDetectorService detector{};
    const avantgarde::BpmDetectionResult r = detector.detectFromFile(p.string(), /*trackSpeed=*/1.0f);

    REQUIRE(r.ok);
    REQUIRE(r.sourceBpm > 109.0f);
    REQUIRE(r.sourceBpm < 119.0f);
}
