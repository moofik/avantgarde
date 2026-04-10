// test/test_cliptrack.cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cmath>

#include "contracts/types.h"      // RtCommand, AudioProcessContext, Target, etc. :contentReference[oaicite:4]{index=4}
#include "contracts/ids.h"        // CmdId :contentReference[oaicite:5]{index=5}
#include "contracts/IClipTrack.h" // IClipTrack

#include "runtime/ClipTrack.cpp"

namespace fs = std::filesystem;

namespace {

// -------------------------
// Helpers: little-endian IO
// -------------------------
    static inline void write_u16_le(std::ofstream& f, uint16_t v) {
        uint8_t b[2] = { (uint8_t)(v & 0xFFu), (uint8_t)((v >> 8) & 0xFFu) };
        f.write((const char*)b, 2);
    }
    static inline void write_u32_le(std::ofstream& f, uint32_t v) {
        uint8_t b[4] = {
                (uint8_t)(v & 0xFFu),
                (uint8_t)((v >> 8) & 0xFFu),
                (uint8_t)((v >> 16) & 0xFFu),
                (uint8_t)((v >> 24) & 0xFFu)
        };
        f.write((const char*)b, 4);
    }

// --------------------------------------
// Make minimal PCM16 WAV (mono or stereo)
// --------------------------------------
    static fs::path write_wav_pcm16(
            const fs::path& path,
            int sampleRate,
            int channels,
            const std::vector<int16_t>& interleaved // frames*channels
    ) {
        REQUIRE((channels == 1 || channels == 2));
        REQUIRE(sampleRate > 0);
        REQUIRE((int)interleaved.size() % channels == 0);

        const uint32_t frames = (uint32_t)(interleaved.size() / (size_t)channels);
        const uint16_t bitsPerSample = 16;
        const uint16_t blockAlign = (uint16_t)(channels * (bitsPerSample / 8));
        const uint32_t byteRate = (uint32_t)(sampleRate * blockAlign);
        const uint32_t dataSize = (uint32_t)(interleaved.size() * sizeof(int16_t));

        // RIFF sizes
        const uint32_t fmtChunkSize = 16;
        const uint32_t riffChunkSize = 4 /*WAVE*/ + 8 + fmtChunkSize + 8 + dataSize;

        std::ofstream f(path, std::ios::binary);
        REQUIRE(f.is_open());

        // RIFF header
        f.write("RIFF", 4);
        write_u32_le(f, riffChunkSize);
        f.write("WAVE", 4);

        // fmt chunk
        f.write("fmt ", 4);
        write_u32_le(f, fmtChunkSize);
        write_u16_le(f, 1); // PCM
        write_u16_le(f, (uint16_t)channels);
        write_u32_le(f, (uint32_t)sampleRate);
        write_u32_le(f, byteRate);
        write_u16_le(f, blockAlign);
        write_u16_le(f, bitsPerSample);

        // data chunk
        f.write("data", 4);
        write_u32_le(f, dataSize);
        f.write((const char*)interleaved.data(), (std::streamsize)dataSize);

        f.flush();
        REQUIRE(f.good());

        return path;
    }

// --------------------------
// AudioProcessContext builder
// --------------------------
    struct TestCtx {
        std::vector<float> out0;
        std::vector<float> out1;
        float* outPtrs[2]{};
        avantgarde::AudioProcessContext ctx{};
    };

    static TestCtx make_ctx(std::size_t nframes) {
        TestCtx t;
        t.out0.assign(nframes, 0.0f);
        t.out1.assign(nframes, 0.0f);
        t.outPtrs[0] = t.out0.data();
        t.outPtrs[1] = t.out1.data();

        t.ctx.in = nullptr;
        t.ctx.out = t.outPtrs;
        t.ctx.nframes = nframes;
        return t;
    }

    static void clear_out(TestCtx& t) {
        std::memset(t.out0.data(), 0, t.out0.size() * sizeof(float));
        std::memset(t.out1.data(), 0, t.out1.size() * sizeof(float));
    }

// ----------------------
// RtCommand send shortcut
// ----------------------
    static void send_cmd(avantgarde::ClipTrackImpl& tr,
                         avantgarde::CmdId id,
                         int16_t slot = 0,
                         uint16_t index = 0,
                         float value = 0.0f) {
        avantgarde::RtCommand c{};
        c.id = (uint16_t)id;
        c.track = 0;
        c.slot = slot;     // по контракту slot = FX-slot или -1 :contentReference[oaicite:7]{index=7}
        c.index = index;   // ParamSet index :contentReference[oaicite:8]{index=8}
        c.value = value;   // payload :contentReference[oaicite:9]{index=9}
        tr.onRtCommand(c);
    }

    static float absf(float x) { return x < 0 ? -x : x; }

    static int count_non_zero(const std::vector<float>& v, float eps = 1e-4f) {
        int n = 0;
        for (float x : v) {
            if (absf(x) > eps) {
                ++n;
            }
        }
        return n;
    }

    struct CaptureTransportFx final : avantgarde::IAudioModule {
        bool seenValid{false};
        bool seenPlaying{false};
        uint8_t seenTsNum{0};
        uint8_t seenTsDen{0};
        float seenBpm{0.0f};
        uint8_t seenQuant{0};
        uint64_t seenSampleTime{0};
        avantgarde::ParamMeta meta{"noop", 0.0f, 1.0f, false, ""};

        void init(double, std::size_t) override {}
        void reset() override {}
        std::size_t getParamCount() const override { return 0; }
        float getParam(std::size_t) const override { return 0.0f; }
        void setParam(std::size_t, float) override {}
        const avantgarde::ParamMeta& getParamMeta(std::size_t) const override { return meta; }

        void process(const avantgarde::AudioProcessContext& ctx) override {
            seenValid = ctx.transportValid;
            seenPlaying = ctx.transportPlaying;
            seenTsNum = ctx.transportTsNum;
            seenTsDen = ctx.transportTsDen;
            seenBpm = ctx.transportBpm;
            seenQuant = ctx.transportQuant;
            seenSampleTime = ctx.transportSampleTime;
            for (std::size_t i = 0; i < ctx.nframes; ++i) {
                ctx.out[0][i] = ctx.in[0][i];
                if (ctx.out[1] && ctx.in[1]) {
                    ctx.out[1][i] = ctx.in[1][i];
                } else if (ctx.out[1]) {
                    ctx.out[1][i] = ctx.in[0][i];
                }
            }
        }
    };

} // namespace

// -------------------------
// Tests
// -------------------------

TEST_CASE("ClipTrack: numSlots fixed and slot bounds") {
    avantgarde::ClipTrackImpl tr;

    REQUIRE(tr.numSlots() == 1);

    // setSlotLooping is only meaningful for existing slots (contract) :contentReference[oaicite:10]{index=10}
    REQUIRE(tr.setSlotLooping(0, true) == true);
    REQUIRE(tr.setSlotLooping(1, true) == false);

    // clearSlot (contract) :contentReference[oaicite:11]{index=11}
    REQUIRE(tr.clearSlot(0) == true);
    REQUIRE(tr.clearSlot(1) == false);

    // armRecordSlot (contract) :contentReference[oaicite:12]{index=12}
    REQUIRE(tr.armRecordSlot(0, true) == true);
    REQUIRE(tr.armRecordSlot(1, true) == false);
}

TEST_CASE("ClipTrack: loadSlotFromFile + Play writes audio into ctx.out") {
    avantgarde::ClipTrackImpl tr;

    // WAV: mono, 8 frames, constant +0.5 amplitude in PCM16
    // int16 16384 ~ 0.5
    const int sr = 48000;
    const int ch = 1;
    std::vector<int16_t> pcm = {
            16384,16384,16384,16384,16384,16384,16384,16384
    };

    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_mono.wav";
    write_wav_pcm16(tmp, sr, ch, pcm);

    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);

    // loop off to simplify
    REQUIRE(tr.setSlotLooping(0, false) == true);

    // Start
    send_cmd(tr, avantgarde::CmdId::Play, /*slot*/0);

    auto t = make_ctx(4);

    // Block 1
    clear_out(t);
    tr.process(t.ctx);

    // Expect some non-zero signal in out0 (and out1 should be dual-mono by impl)
    float sum0 = 0.0f, sum1 = 0.0f;
    for (int i=0;i<4;i++) { sum0 += absf(t.out0[(size_t)i]); sum1 += absf(t.out1[(size_t)i]); }
    REQUIRE(sum0 > 0.1f);
    REQUIRE(sum1 > 0.1f);
}

TEST_CASE("ClipTrack: Stop stops adding audio") {
    avantgarde::ClipTrackImpl tr;

    // mono 4 frames, amplitude 1.0
    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_stop.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, true) == true);

    auto t = make_ctx(4);

    // Play, process once -> audio should appear
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);

    float sumPlay = 0.0f;
    for (float v: t.out0) sumPlay += absf(v);
    REQUIRE(sumPlay > 0.1f);

    // Stop, process again -> should be ~0
    send_cmd(tr, avantgarde::CmdId::Stop, 0);
    clear_out(t);
    tr.process(t.ctx);

    float sumStop = 0.0f;
    for (float v: t.out0) sumStop += absf(v);
    REQUIRE(sumStop < 1e-4f);
}

TEST_CASE("ClipTrack: loop=false one-shot stops at end; loop=true wraps") {
    avantgarde::ClipTrackImpl tr;

    // mono 3 frames: [1,2,3] normalized roughly via PCM16
    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767, 16384, 8192 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_loop.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);

    auto t = make_ctx(8);

    SECTION("one-shot (loop=false) stops after 3 frames") {
        REQUIRE(tr.setSlotLooping(0, false) == true);
        send_cmd(tr, avantgarde::CmdId::Play, 0);

        clear_out(t);
        tr.process(t.ctx);

        // We expect: first 3 samples non-zero, remaining should stay zero (no wrap)
        REQUIRE(absf(t.out0[0]) > 0.1f);
        REQUIRE(absf(t.out0[1]) > 0.05f);
        REQUIRE(absf(t.out0[2]) > 0.01f);

        for (int i=3;i<8;i++) {
            REQUIRE(absf(t.out0[(size_t)i]) < 1e-4f);
        }

        // Next block should be silent too (since playing turned off)
        clear_out(t);
        tr.process(t.ctx);
        float sum = 0.0f;
        for (float v: t.out0) sum += absf(v);
        REQUIRE(sum < 1e-4f);
    }

    SECTION("loop=true wraps and continues after end") {
        REQUIRE(tr.setSlotLooping(0, true) == true);
        send_cmd(tr, avantgarde::CmdId::Play, 0);

        clear_out(t);
        tr.process(t.ctx);

        // Expect non-zero beyond index 2 due to wrap
        REQUIRE(absf(t.out0[0]) > 0.1f);
        REQUIRE(absf(t.out0[1]) > 0.05f);
        REQUIRE(absf(t.out0[2]) > 0.01f);

        // Wrapped samples should also be non-zero
        REQUIRE(absf(t.out0[3]) > 0.1f);
        REQUIRE(absf(t.out0[4]) > 0.05f);
    }
}

TEST_CASE("ClipTrack: ParamSet controls gain (index=0) and loop (index=1)") {
    avantgarde::ClipTrackImpl tr;

    // mono 4 frames const 1.0
    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_paramset.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);

    auto t = make_ctx(4);

    // gain = 1.0
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/0, /*value*/1.0f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);

    clear_out(t);
    tr.process(t.ctx);

    float sum1 = 0.0f;
    for (float v: t.out0) sum1 += absf(v);
    REQUIRE(sum1 > 0.5f);

    // gain = 0.25
    send_cmd(tr, avantgarde::CmdId::Stop, 0);
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/0, /*value*/0.25f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);

    clear_out(t);
    tr.process(t.ctx);

    float sum025 = 0.0f;
    for (float v: t.out0) sum025 += absf(v);

    // should be about 4x smaller than sum1 (roughly)
    REQUIRE(sum025 < sum1 * 0.35f);

    // loop via ParamSet index=1
    send_cmd(tr, avantgarde::CmdId::Stop, 0);
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/1, /*value*/1.0f); // loop on
    send_cmd(tr, avantgarde::CmdId::Play, 0);

    // clip length 4, ask 8 frames; if loop is on, beyond 4 should be non-zero
    auto t8 = make_ctx(8);
    clear_out(t8);
    tr.process(t8.ctx);

    REQUIRE(absf(t8.out0[0]) > 0.01f);
    REQUIRE(absf(t8.out0[5]) > 0.01f); // wrapped region
}

TEST_CASE("ClipTrack: ParamSet index=2 controls playback speed") {
    avantgarde::ClipTrackImpl tr;

    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767,32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_speed.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, false) == true);

    auto t = make_ctx(8);

    // normal speed
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/2, /*value*/1.0f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);
    float sumNormal = 0.0f;
    for (float v : t.out0) sumNormal += absf(v);
    REQUIRE(sumNormal > 4.0f);

    // faster speed: clip is consumed sooner, so output energy in same block is lower
    send_cmd(tr, avantgarde::CmdId::Stop, 0);
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/2, /*value*/2.0f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);
    float sumFast = 0.0f;
    for (float v : t.out0) sumFast += absf(v);

    REQUIRE(sumFast < sumNormal * 0.7f);
    REQUIRE(sumFast > sumNormal * 0.3f);
}

TEST_CASE("ClipTrack: forwards transport fields into FX module context") {
    avantgarde::ClipTrackImpl tr;

    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_fx_transport.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, false) == true);

    auto fx = std::make_unique<CaptureTransportFx>();
    auto* fxPtr = fx.get();
    tr.addModule(std::move(fx));

    auto t = make_ctx(4);
    t.ctx.transportValid = true;
    t.ctx.transportPlaying = true;
    t.ctx.transportTsNum = 7;
    t.ctx.transportTsDen = 8;
    t.ctx.transportBpm = 133.0f;
    t.ctx.transportQuant = 1; // Beat
    t.ctx.transportSampleTime = 987654;

    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);

    REQUIRE(fxPtr->seenValid == true);
    REQUIRE(fxPtr->seenPlaying == true);
    REQUIRE(fxPtr->seenTsNum == 7);
    REQUIRE(fxPtr->seenTsDen == 8);
    REQUIRE(absf(fxPtr->seenBpm - 133.0f) < 1e-6f);
    REQUIRE(fxPtr->seenQuant == 1);
    REQUIRE(fxPtr->seenSampleTime == 987654);
}

TEST_CASE("ClipTrack: speed=1.0 keeps real-time duration for clip sampleRate != host sampleRate") {
    // Host работает на 480 Гц, клип закодирован в 441 Гц.
    // При корректной компенсации sample rate one-shot длительностью 1 сек
    // должен занять около 480 output-фреймов, а не 441.
    avantgarde::ClipTrackImpl tr(/*outputSampleRate*/480.0);

    const int clipSr = 441;
    std::vector<int16_t> pcm(441, 32767); // 1 second source clip at 441 Hz
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_samplerate_comp.wav";
    write_wav_pcm16(tmp, clipSr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, false) == true);

    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/0, /*index*/2, /*value*/1.0f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);

    auto t = make_ctx(480);
    clear_out(t);
    tr.process(t.ctx);

    const int nz = count_non_zero(t.out0);
    REQUIRE(nz >= 475);
}

TEST_CASE("ClipTrack: mute does not stop playback phase progression") {
    avantgarde::ClipTrackImpl tr;

    // Упорядоченная амплитуда, чтобы можно было увидеть сдвиг playhead после mute.
    const int sr = 48000;
    std::vector<int16_t> pcm = { 8192, 16384, 24576, 32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_mute_phase.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, true) == true);

    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1, avantgarde::toParamIndex(avantgarde::TrackParamId::PlaybackInc), 1.0f);
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1, avantgarde::toParamIndex(avantgarde::TrackParamId::MuteEnabled), 0.0f);
    send_cmd(tr, avantgarde::CmdId::Play, 0);

    auto one = make_ctx(1);
    clear_out(one);
    tr.process(one.ctx);
    const float firstAudible = absf(one.out0[0]);
    REQUIRE(firstAudible > 0.1f);

    // Включаем mute и прогоняем 2 фрейма:
    // звук должен быть нулевой, но фаза должна продолжить движение.
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1, avantgarde::toParamIndex(avantgarde::TrackParamId::MuteEnabled), 1.0f);
    auto muted2 = make_ctx(2);
    clear_out(muted2);
    tr.process(muted2.ctx);
    REQUIRE(absf(muted2.out0[0]) < 1e-4f);
    REQUIRE(absf(muted2.out0[1]) < 1e-4f);

    // Снимаем mute: ожидаем не "второй" сэмпл, а следующий по фазе
    // после двух mute-фреймов (в нашем случае это самый громкий 4-й сэмпл).
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1, avantgarde::toParamIndex(avantgarde::TrackParamId::MuteEnabled), 0.0f);
    clear_out(one);
    tr.process(one.ctx);
    const float afterUnmute = absf(one.out0[0]);
    REQUIRE(afterUnmute > 0.75f);
    REQUIRE(afterUnmute > firstAudible * 2.5f);
}

TEST_CASE("ClipTrack: setSlotLengthInBars stretches playback length to target bars") {
    avantgarde::ClipTrackImpl tr(/*outputSampleRate*/8.0);

    // 8 frames at 8 Hz => 1 second source clip.
    const int sr = 8;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767,32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_bars.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, false) == true);

    // Auto-stretch to 1 bar at default 120 BPM, 4/4:
    // 1 bar = 2 sec => 16 frames at 8 Hz.
    REQUIRE(tr.setSlotLengthInBars(0, 1) == true);

    auto t = make_ctx(16);
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);

    const int nz = count_non_zero(t.out0);
    REQUIRE(nz >= 15);
}

TEST_CASE("ClipTrack: auto-stretch responds to transport tempo changes") {
    avantgarde::ClipTrackImpl tr(/*outputSampleRate*/8.0);

    const int sr = 8;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767,32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_tempo_stretch.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, false) == true);
    REQUIRE(tr.setSlotLengthInBars(0, 1) == true);

    // Start from 120 BPM.
    send_cmd(tr, avantgarde::CmdId::SetTempoBpm, 0, 0, 120.0f);
    REQUIRE(absf(tr.getParam(avantgarde::toParamIndex(avantgarde::TrackParamId::PlaybackInc)) - 0.5f) < 1e-3f);
    auto t120 = make_ctx(16);
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t120);
    tr.process(t120.ctx);
    const int nz120 = count_non_zero(t120.out0);

    // Increase tempo to 240 BPM => target bar duration halves.
    send_cmd(tr, avantgarde::CmdId::SetTempoBpm, 0, 0, 240.0f);
    REQUIRE(absf(tr.getParam(avantgarde::toParamIndex(avantgarde::TrackParamId::PlaybackInc)) - 1.0f) < 1e-3f);
    auto t240 = make_ctx(16);
    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t240);
    tr.process(t240.ctx);
    const int nz240 = count_non_zero(t240.out0);

    REQUIRE(nz120 >= 15);
    REQUIRE(nz240 <= 9);
    REQUIRE(nz240 < nz120);
}

TEST_CASE("ClipTrack: clearSlot resets audio (no playback after clear)") {
    avantgarde::ClipTrackImpl tr;

    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_test_clear.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);

    auto t = make_ctx(4);

    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);
    float sumBefore = 0.0f;
    for (float v: t.out0) sumBefore += absf(v);
    REQUIRE(sumBefore > 0.1f);

    // clear slot (outside RT), then process should be silent even if we send Play
    REQUIRE(tr.clearSlot(0) == true);

    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);
    float sumAfter = 0.0f;
    for (float v: t.out0) sumAfter += absf(v);
    REQUIRE(sumAfter < 1e-4f);
}

TEST_CASE("ClipTrack: IParameterized surface exposes and applies track params") {
    avantgarde::ClipTrackImpl tr;

    REQUIRE(tr.getParamCount() == 11);
    REQUIRE(tr.getParamMeta(avantgarde::toParamIndex(avantgarde::TrackParamId::MuteEnabled)).name == "track.mute");

    tr.setParam(avantgarde::toParamIndex(avantgarde::TrackParamId::Gain01), 0.25f);
    REQUIRE(absf(tr.getParam(avantgarde::toParamIndex(avantgarde::TrackParamId::Gain01)) - 0.25f) < 1e-6f);

    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_param_iface.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);
    REQUIRE(tr.setSlotLooping(0, true) == true);

    auto t = make_ctx(4);

    send_cmd(tr, avantgarde::CmdId::Play, 0);
    clear_out(t);
    tr.process(t.ctx);
    float sumBeforeMute = 0.0f;
    for (float v : t.out0) sumBeforeMute += absf(v);
    REQUIRE(sumBeforeMute > 0.05f);

    tr.setParam(avantgarde::toParamIndex(avantgarde::TrackParamId::MuteEnabled), 1.0f);
    clear_out(t);
    tr.process(t.ctx);
    float sumAfterMute = 0.0f;
    for (float v : t.out0) sumAfterMute += absf(v);
    REQUIRE(sumAfterMute < 1e-4f);
}

TEST_CASE("ClipTrack: note mode policies stop playback on NoteOff and retrigger on NoteOn") {
    avantgarde::ClipTrackImpl tr;

    const int sr = 48000;
    std::vector<int16_t> pcm = { 32767,32767,32767,32767,32767,32767,32767,32767 };
    const fs::path tmp = fs::temp_directory_path() / "ag_cliptrack_note_mode.wav";
    write_wav_pcm16(tmp, sr, 1, pcm);
    REQUIRE(tr.loadSlotFromFile(0, tmp.string().c_str()) == true);

    // NOTE/SAMPLER preset:
    // - one-shot gate (follow transport off)
    // - mode: Note
    // - launch: RetriggerOnNoteOn
    // - stop: ByNoteOff
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1,
             avantgarde::toParamIndex(avantgarde::TrackParamId::FollowTransportEnabled), 0.0f);
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1,
             avantgarde::toParamIndex(avantgarde::TrackParamId::PlaybackMode),
             avantgarde::toParamValue(avantgarde::TrackPlaybackModeValue::Note));
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1,
             avantgarde::toParamIndex(avantgarde::TrackParamId::LaunchPolicy),
             avantgarde::toParamValue(avantgarde::TrackLaunchPolicyValue::RetriggerOnNoteOn));
    send_cmd(tr, avantgarde::CmdId::ParamSet, /*slot*/-1,
             avantgarde::toParamIndex(avantgarde::TrackParamId::StopPolicy),
             avantgarde::toParamValue(avantgarde::TrackStopPolicyValue::ByNoteOff));

    auto t = make_ctx(4);

    // NoteOn -> should start playback.
    send_cmd(tr, avantgarde::CmdId::NoteOn, /*slot*/-1, /*index=*/60, /*value=*/1.0f);
    clear_out(t);
    tr.process(t.ctx);
    float sumOn = 0.0f;
    for (float v : t.out0) sumOn += absf(v);
    REQUIRE(sumOn > 0.1f);

    // NoteOff same note -> should stop playback in ByNoteOff policy.
    send_cmd(tr, avantgarde::CmdId::NoteOff, /*slot*/-1, /*index=*/60, /*value=*/0.0f);
    clear_out(t);
    tr.process(t.ctx);
    float sumOff = 0.0f;
    for (float v : t.out0) sumOff += absf(v);
    REQUIRE(sumOff < 1e-4f);
}
