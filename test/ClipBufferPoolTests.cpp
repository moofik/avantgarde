#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "contracts/ids.h"
#include "contracts/types.h"
#include "runtime/ClipTrack.cpp"
#include "service/pattern/ClipBufferPool.h"

namespace fs = std::filesystem;
using namespace avantgarde;

namespace {

static inline void write_u16_le(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu)};
    f.write(reinterpret_cast<const char*>(b), 2);
}

static inline void write_u32_le(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFFu),
        static_cast<uint8_t>((v >> 8) & 0xFFu),
        static_cast<uint8_t>((v >> 16) & 0xFFu),
        static_cast<uint8_t>((v >> 24) & 0xFFu)
    };
    f.write(reinterpret_cast<const char*>(b), 4);
}

static fs::path write_wav_pcm16(const fs::path& path,
                                int sampleRate,
                                int channels,
                                const std::vector<int16_t>& interleaved) {
    REQUIRE((channels == 1 || channels == 2));
    REQUIRE(sampleRate > 0);
    REQUIRE((int)interleaved.size() % channels == 0);

    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
    const uint32_t fmtChunkSize = 16;
    const uint32_t riffChunkSize = 4 + 8 + fmtChunkSize + 8 + dataSize;

    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f.write("RIFF", 4);
    write_u32_le(f, riffChunkSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    write_u32_le(f, fmtChunkSize);
    write_u16_le(f, 1);
    write_u16_le(f, static_cast<uint16_t>(channels));
    write_u32_le(f, static_cast<uint32_t>(sampleRate));
    write_u32_le(f, byteRate);
    write_u16_le(f, blockAlign);
    write_u16_le(f, bitsPerSample);

    f.write("data", 4);
    write_u32_le(f, dataSize);
    f.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataSize));
    f.flush();
    REQUIRE(f.good());
    return path;
}

struct TestCtx {
    std::vector<float> out0{};
    std::vector<float> out1{};
    float* outPtrs[2]{};
    AudioProcessContext ctx{};
};

TestCtx make_ctx(std::size_t nframes) {
    TestCtx t{};
    t.out0.assign(nframes, 0.0f);
    t.out1.assign(nframes, 0.0f);
    t.outPtrs[0] = t.out0.data();
    t.outPtrs[1] = t.out1.data();
    t.ctx.in = nullptr;
    t.ctx.out = t.outPtrs;
    t.ctx.nframes = nframes;
    return t;
}

void send_play(ClipTrackImpl& tr) {
    RtCommand c{};
    c.id = toWireCmdId(CmdId::Play);
    c.track = 0;
    c.slot = 0;
    c.index = 0;
    c.value = 1.0f;
    tr.onRtCommand(c);
}

int count_non_zero(const std::vector<float>& v, float eps = 1e-5f) {
    int n = 0;
    for (float x : v) {
        if (x > eps || x < -eps) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("ClipBufferPool: loadFromFile + bindClipToTrack plays without extra file IO on bind") {
    const fs::path tmp = fs::temp_directory_path() / "avantgarde_pool_bind.wav";

    // Простая импульсная моно-волна.
    const std::vector<int16_t> mono = {32767, 0, 0, 0, 0, 0, 0, 0};
    write_wav_pcm16(tmp, 48000, 1, mono);

    ClipBufferPool pool{};
    std::string err{};
    REQUIRE(pool.loadFromFile(42, tmp.string(), &err));
    CHECK(pool.contains(42));

    ClipTrackImpl tr{48000.0};
    REQUIRE(pool.bindClipToTrack(tr, 0, 42));
    REQUIRE(tr.setSlotLooping(0, false));

    auto tc = make_ctx(16);
    send_play(tr);
    tr.process(tc.ctx);

    CHECK(count_non_zero(tc.out0) > 0);
}

TEST_CASE("ClipBufferPool: bind fails for unknown ref") {
    ClipBufferPool pool{};
    ClipTrackImpl tr{48000.0};
    REQUIRE_FALSE(pool.bindClipToTrack(tr, 0, 9999));
}

