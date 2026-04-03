#include <catch2/catch_all.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "contracts/IAudioEngine.h"
#include "contracts/IAudioModule.h"
#include "contracts/IParamBridge.h"
#include "contracts/IRtCommandQueue.h"
#include "contracts/ITrack.h"
#include "contracts/ids.h"
#include "runtime/ClipTrack.cpp"
#include "runtime/ParamBridgeDualBuffer.cpp"

using namespace avantgarde;
namespace fs = std::filesystem;

namespace avantgarde {
std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue*, IParamBridge*);
}

namespace {

struct MockQueue final : IRtCommandQueue {
    std::vector<RtCommand> q;

    bool push(const RtCommand& cmd) noexcept override {
        q.push_back(cmd);
        return true;
    }

    bool pop(RtCommand& out) noexcept override {
        if (q.empty()) return false;
        out = q.front();
        q.erase(q.begin());
        return true;
    }

    void clear() noexcept override { q.clear(); }
    std::size_t capacity() const noexcept override { return 1024; }
    std::size_t size() const noexcept override { return q.size(); }
    bool overflowFlagAndReset() noexcept override { return false; }
};

struct GainFxModule final : IAudioModule {
    float gain01{1.0f};
    ParamMeta meta{"Gain", 0.0f, 1.0f, false, "x"};

    void init(double, std::size_t) override {}
    void reset() override {}
    std::size_t getParamCount() const override { return 1; }
    float getParam(std::size_t) const override { return gain01; }
    void setParam(std::size_t index, float value) override {
        if (index == 0) {
            gain01 = std::clamp(value, 0.0f, 1.0f);
        }
    }
    const ParamMeta& getParamMeta(std::size_t) const override { return meta; }

    void process(const AudioProcessContext& ctx) override {
        const float* in0 = ctx.in[0];
        const float* in1 = ctx.in[1] ? ctx.in[1] : in0;
        float* out0 = ctx.out[0];
        float* out1 = ctx.out[1] ? ctx.out[1] : out0;
        for (std::size_t i = 0; i < ctx.nframes; ++i) {
            out0[i] = in0[i] * gain01;
            out1[i] = in1[i] * gain01;
        }
    }
};

static std::array<ITrack*, 2> gTracks{};

IParameterized* ResolveTarget(Target t) noexcept {
    if (t.trackId < 0 || static_cast<std::size_t>(t.trackId) >= gTracks.size()) return nullptr;
    if (t.slotId < 0) return nullptr;
    ITrack* tr = gTracks[static_cast<std::size_t>(t.trackId)];
    if (!tr) return nullptr;
    return tr->getModule(static_cast<std::size_t>(t.slotId));
}

static inline void write_u16_le(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu) };
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

fs::path write_wav_pcm16(const fs::path& path, int sampleRate, int channels, const std::vector<int16_t>& interleaved) {
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate * blockAlign);
    const uint32_t dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
    const uint32_t riffChunkSize = 4u + 8u + 16u + 8u + dataSize;

    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f.write("RIFF", 4);
    write_u32_le(f, riffChunkSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    write_u32_le(f, 16);
    write_u16_le(f, 1);
    write_u16_le(f, static_cast<uint16_t>(channels));
    write_u32_le(f, static_cast<uint32_t>(sampleRate));
    write_u32_le(f, byteRate);
    write_u16_le(f, blockAlign);
    write_u16_le(f, bitsPerSample);
    f.write("data", 4);
    write_u32_le(f, dataSize);
    f.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataSize));
    return path;
}

AudioProcessContext makeCtx(std::vector<float>& out0, std::vector<float>& out1) {
    static std::array<float*, 2> outPtrs{};
    outPtrs[0] = out0.data();
    outPtrs[1] = out1.data();
    AudioProcessContext ctx{};
    ctx.in = nullptr;
    ctx.out = outPtrs.data();
    ctx.nframes = out0.size();
    return ctx;
}

RtCommand makeCmd(CmdId id, int16_t track, int16_t slot = 0, uint16_t index = 0, float value = 0.0f) {
    RtCommand cmd{};
    cmd.id = static_cast<uint16_t>(id);
    cmd.track = track;
    cmd.slot = slot;
    cmd.index = index;
    cmd.value = value;
    return cmd;
}

} // namespace

TEST_CASE("Engine: two tracks keep independent FX chains and ParamBridge routes by track/slot") {
    MockQueue q;
    ParamBridgeDualBuffer pb(64, &ResolveTarget);
    auto engine = MakeAudioEngine(&q, &pb);

    auto tr0 = std::make_unique<ClipTrackImpl>();
    auto tr1 = std::make_unique<ClipTrackImpl>();
    auto* tr0Ptr = tr0.get();
    auto* tr1Ptr = tr1.get();

    const fs::path tmp = fs::temp_directory_path() / "ag_fx_chain_two_tracks.wav";
    std::vector<int16_t> pcm(16, 32767);
    write_wav_pcm16(tmp, 48000, 1, pcm);
    REQUIRE(tr0Ptr->loadSlotFromFile(0, tmp.string().c_str()));
    REQUIRE(tr1Ptr->loadSlotFromFile(0, tmp.string().c_str()));
    REQUIRE(tr0Ptr->setSlotLooping(0, false));
    REQUIRE(tr1Ptr->setSlotLooping(0, false));

    tr0Ptr->addModule(std::make_unique<GainFxModule>());
    tr0Ptr->addModule(std::make_unique<GainFxModule>());
    tr1Ptr->addModule(std::make_unique<GainFxModule>());

    gTracks[0] = tr0Ptr;
    gTracks[1] = tr1Ptr;

    engine->registerTrack(std::move(tr0));
    engine->registerTrack(std::move(tr1));

    pb.pushParam(Target{0, 0}, 0, 0.5f);
    pb.pushParam(Target{0, 1}, 0, 0.5f);
    pb.pushParam(Target{1, 0}, 0, 0.5f);

    q.push(makeCmd(CmdId::Play, 0, 0, 0, 1.0f));
    q.push(makeCmd(CmdId::Play, 1, 0, 0, 1.0f));

    std::vector<float> out0(8, 0.0f), out1(8, 0.0f);
    auto ctx = makeCtx(out0, out1);
    engine->processBlock(ctx);

    // Track0: 1.0 * 0.5 * 0.5 = 0.25; Track1: 1.0 * 0.5 = 0.5; total ~0.75
    REQUIRE(out0[0] == Catch::Approx(0.75f).margin(0.08f));
    REQUIRE(out0[1] == Catch::Approx(0.75f).margin(0.08f));

    // Change only track1 FX via ParamBridge and verify track0 chain stays unchanged.
    pb.pushParam(Target{1, 0}, 0, 0.0f);
    q.push(makeCmd(CmdId::Stop, 0, 0));
    q.push(makeCmd(CmdId::Stop, 1, 0));
    q.push(makeCmd(CmdId::Play, 0, 0, 0, 1.0f));
    q.push(makeCmd(CmdId::Play, 1, 0, 0, 1.0f));

    std::fill(out0.begin(), out0.end(), 0.0f);
    std::fill(out1.begin(), out1.end(), 0.0f);
    engine->processBlock(ctx);

    // Only track0 remains audible: ~0.25
    REQUIRE(out0[0] == Catch::Approx(0.25f).margin(0.08f));
    REQUIRE(out0[1] == Catch::Approx(0.25f).margin(0.08f));
}

TEST_CASE("ClipTrack FX slot can be bypassed without removing module") {
    MockQueue q;
    ParamBridgeDualBuffer pb(16, &ResolveTarget);
    auto engine = MakeAudioEngine(&q, &pb);

    auto tr = std::make_unique<ClipTrackImpl>();
    auto* trPtr = tr.get();

    const fs::path tmp = fs::temp_directory_path() / "ag_fx_bypass.wav";
    std::vector<int16_t> pcm(32, 32767);
    write_wav_pcm16(tmp, 48000, 1, pcm);
    REQUIRE(trPtr->loadSlotFromFile(0, tmp.string().c_str()));
    REQUIRE(trPtr->setSlotLooping(0, true));

    trPtr->addModule(std::make_unique<GainFxModule>());
    gTracks[0] = trPtr;
    gTracks[1] = nullptr;
    engine->registerTrack(std::move(tr));

    // Start playback and set gain FX to 0.0 -> signal should be near silence.
    q.push(makeCmd(CmdId::Play, 0, 0, 0, 1.0f));
    q.push(makeCmd(CmdId::ParamSet, 0, 0, 0, 0.0f));

    std::vector<float> out0(16, 0.0f), out1(16, 0.0f);
    auto ctx = makeCtx(out0, out1);
    engine->processBlock(ctx);
    REQUIRE(std::fabs(out0[0]) < 0.02f);

    // Disable FX slot via common FX param -> module stays in slot, audio bypasses FX.
    q.push(makeCmd(CmdId::ParamSet, 0, 0, toParamIndex(FxCommonParamId::Enabled), 0.0f));
    std::fill(out0.begin(), out0.end(), 0.0f);
    std::fill(out1.begin(), out1.end(), 0.0f);
    engine->processBlock(ctx);
    REQUIRE(out0[0] == Catch::Approx(1.0f).margin(0.08f));
}
