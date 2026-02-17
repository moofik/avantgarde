#include <catch2/catch_all.hpp>

#include "contracts/IAudioEngine.h"
#include "contracts/ITrack.h"
#include "contracts/IParamBridge.h"
#include "contracts/IRtCommandQueue.h"
#include "contracts/types.h"
#include "contracts/ids.h"


#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>

using namespace avantgarde;

// --- Mocks ---

struct MockTrack : ITrack {
    int calls = 0;
    std::vector<RtCommand> seen;

    void addModule(std::unique_ptr<IAudioModule>) override {}
    IAudioModule* getModule(std::size_t) override { return nullptr; }

    void process(const AudioProcessContext&) override { ++calls; }

    void onRtCommand(const RtCommand& cmd) noexcept override { seen.push_back(cmd); }
};

struct MockRtQueue : IRtCommandQueue {
    std::vector<RtCommand> q;
    std::size_t cap{8};
    bool overflow{false};

    bool push(const RtCommand &c) noexcept override {
        if (q.size() >= cap) {
            overflow = true;
            return false;
        }
        q.push_back(c);
        return true;
    }

    bool pop(RtCommand &out) noexcept override {
        if (q.empty()) return false;
        out = q.front();
        q.erase(q.begin());
        return true;
    }

    void clear() noexcept override {
        q.clear();
        overflow = false;
    }

    std::size_t capacity() const noexcept override { return cap; }
    std::size_t size() const noexcept override { return q.size(); }

    bool overflowFlagAndReset() noexcept override {
        bool f = overflow;
        overflow = false;
        return f;
    }
};

struct MockParamBridge : IParamBridge {
    int swaps = 0;
    int* phase = nullptr; // для проверки порядка

    void pushParam(Target, std::size_t, float) override {}

    void swapBuffers() override {
        ++swaps;
        if (phase) *phase = 10; // ParamBridge swapped
    }
};

// RT extension mock
struct MockRtExtension : IRtExtension {
    int beginCalls = 0;
    int endCalls = 0;

    int* phase = nullptr;

    void onBlockBegin(const AudioProcessContext&) noexcept override {
        ++beginCalls;
        if (phase) *phase = 1;
    }

    void onBlockEnd(const AudioProcessContext&) noexcept override {
        ++endCalls;
        if (phase) *phase = 3;
    }
};

// Record sink mock (master out)
struct MockRecordSink : IRtRecordSink {
    int writes = 0;
    const float* const* lastCh = nullptr;
    int lastFrames = 0;

    int marks = 0;
    uint32_t lastMark = 0;

    bool writeBlock(const float* const* ch, int nframes) noexcept override {
        ++writes;
        lastCh = ch;
        lastFrames = nframes;
        return true;
    }

    void mark(uint32_t code) noexcept override {
        ++marks;
        lastMark = code;
    }
};

// Transport bridge mock
struct MockTransportBridge : ITransportBridge {
    int swaps = 0;
    uint64_t advanced = 0;

    int* phase = nullptr;

    // Control-side (не используется в тестах)
    void setPlaying(bool) override {}
    void setTempo(float) override {}
    void setTimeSignature(uint8_t, uint8_t) override {}
    void setQuantize(QuantizeMode) override {}
    void setSwing(float) override {}

    // RT-side
    void swapBuffers() noexcept override {
        ++swaps;
        if (phase) {
            REQUIRE(*phase == 10); // после ParamBridge.swap
            *phase = 20;
        }
    }

    const TransportRtSnapshot& rt() const noexcept override { return snap; }

    void advanceSampleTime(uint64_t frames) noexcept override {
        advanced += frames;
        if (phase) {
            REQUIRE(*phase == 20); // сразу после swapBuffers
            *phase = 21;
        }
    }

    TransportRtSnapshot snap{};
};

// Фабрика из реализации (объявлена в AudioEngine.cpp)
namespace avantgarde {
    std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue*, IParamBridge*);
}

// Helpers
static AudioProcessContext makeCtx(int frames = 256) {
    AudioProcessContext ctx{};
    ctx.nframes = static_cast<std::size_t>(frames);
    ctx.in  = nullptr;
    ctx.out = nullptr;
    return ctx;
}

// --- Tests ---

TEST_CASE("Register/Process No-Crash") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);
    eng->setSampleRate(48000.0);

    auto ctx = makeCtx();
    REQUIRE_NOTHROW(eng->processBlock(ctx));
}

TEST_CASE("SingleTrack -> process() is called") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);
    eng->setSampleRate(48000.0);

    auto t = std::make_unique<MockTrack>();
    auto *tPtr = t.get();
    eng->registerTrack(std::move(t));

    auto ctx = makeCtx();
    eng->processBlock(ctx);
    REQUIRE(tPtr->calls == 1);
}

TEST_CASE("ParamBridge::swapBuffers() is called in prologue") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);
    eng->setSampleRate(48000.0);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(p.swaps == 1);
}

TEST_CASE("TransportBridge::swapBuffers() and advanceSampleTime() are called in prologue when set") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    MockTransportBridge tr;
    eng->setTransportBridge(&tr);

    auto ctx = makeCtx(256);
    eng->processBlock(ctx);

    REQUIRE(tr.swaps == 1);
    REQUIRE(tr.advanced == 256);
}

TEST_CASE("TransportBridge is not called when nullptr") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    MockTransportBridge tr;
    eng->setTransportBridge(nullptr);

    auto ctx = makeCtx(256);
    eng->processBlock(ctx);

    REQUIRE(tr.swaps == 0);
    REQUIRE(tr.advanced == 0);
}

TEST_CASE("onCommand() routes to RT queue; RT pops in processBlock") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    Command c{"play", {0, -1}, 1.0f};
    eng->onCommand(c);

    REQUIRE(q.size() == 1);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(q.size() == 0);
}

TEST_CASE("Queue overflow flag is observable") {
    MockRtQueue q;
    q.cap = 2;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    Command c{"play", {0, -1}, 1.0f};
    eng->onCommand(c);
    eng->onCommand(c);
    eng->onCommand(c);

    REQUIRE(q.overflowFlagAndReset() == true);
    REQUIRE(q.overflowFlagAndReset() == false);
}

TEST_CASE("ParamSet is routed into track->onRtCommand") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    auto t = std::make_unique<MockTrack>();
    auto* tp = t.get();
    eng->registerTrack(std::move(t));

    RtCommand rc{};
    rc.id = static_cast<uint16_t>(CmdId::ParamSet);
    rc.track = 0;
    rc.index = 3;
    rc.value = 0.75f;
    q.push(rc);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(tp->seen.size() == 1);
    REQUIRE(tp->seen[0].index == 3);
    REQUIRE(tp->seen[0].value == Catch::Approx(0.75f));
}

// --- IRtExtension ---

TEST_CASE("IRtExtension hooks are called (begin/end once per block)") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    MockRtExtension ext;
    eng->addRtExtension(&ext);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(ext.beginCalls == 1);
    REQUIRE(ext.endCalls == 1);
}

TEST_CASE("IRtExtension ordering: begin -> tracks -> end") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    int phase = 0;

    struct PhaseExt : IRtExtension {
        int beginCalls = 0;
        int endCalls = 0;
        int* phase = nullptr;

        void onBlockBegin(const AudioProcessContext&) noexcept override {
            ++beginCalls;
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 0);
            *phase = 1;
        }

        void onBlockEnd(const AudioProcessContext&) noexcept override {
            ++endCalls;
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 2);
            *phase = 3;
        }
    } ext;

    ext.phase = &phase;
    eng->addRtExtension(&ext);

    struct PhaseTrack : MockTrack {
        int* phase = nullptr;
        void process(const AudioProcessContext&) override {
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 1);
            *phase = 2;
            ++calls;
        }
    };

    auto t = std::make_unique<PhaseTrack>();
    auto* tp = t.get();
    tp->phase = &phase;
    eng->registerTrack(std::move(t));

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(ext.beginCalls == 1);
    REQUIRE(tp->calls == 1);
    REQUIRE(ext.endCalls == 1);
    REQUIRE(phase == 3);
}

// --- setMasterRecordSink ---

TEST_CASE("MasterRecordSink: writes ctx.out + nframes when set") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    MockRecordSink sink;
    eng->setMasterRecordSink(&sink);

    auto ctx = makeCtx(512);
    eng->processBlock(ctx);

    REQUIRE(sink.writes == 1);
    REQUIRE(sink.lastCh == ctx.out);
    REQUIRE(sink.lastFrames == 512);
}

TEST_CASE("MasterRecordSink: no writes when sink is nullptr") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    MockRecordSink sink;
    eng->setMasterRecordSink(nullptr);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(sink.writes == 0);
}

TEST_CASE("Full ordering: ParamBridge -> Transport -> RtExtension(begin) -> Track -> RtExtension(end) -> RecordSink") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    int phase = 0;
    p.phase = &phase;

    MockTransportBridge tr;
    tr.phase = &phase;
    eng->setTransportBridge(&tr);

    struct PhaseExt : IRtExtension {
        int beginCalls = 0;
        int endCalls = 0;
        int* phase = nullptr;

        void onBlockBegin(const AudioProcessContext&) noexcept override {
            ++beginCalls;
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 21); // transport advanced
            *phase = 30;
        }

        void onBlockEnd(const AudioProcessContext&) noexcept override {
            ++endCalls;
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 40); // track processed
            *phase = 50;
        }
    } ext;

    ext.phase = &phase;
    eng->addRtExtension(&ext);

    struct PhaseTrack : MockTrack {
        int* phase = nullptr;
        void process(const AudioProcessContext&) override {
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 30);
            *phase = 40;
            ++calls;
        }
    };

    auto t = std::make_unique<PhaseTrack>();
    auto* tp = t.get();
    tp->phase = &phase;
    eng->registerTrack(std::move(t));

    struct PhaseSink : IRtRecordSink {
        int writes = 0;
        int* phase = nullptr;

        bool writeBlock(const float* const*, int) noexcept override {
            ++writes;
            REQUIRE(phase != nullptr);
            REQUIRE(*phase == 50);
            *phase = 60;
            return true;
        }

        void mark(uint32_t) noexcept override {}
    } sink;

    sink.phase = &phase;
    eng->setMasterRecordSink(&sink);

    auto ctx = makeCtx(128);
    eng->processBlock(ctx);

    REQUIRE(p.swaps == 1);
    REQUIRE(tr.swaps == 1);
    REQUIRE(tr.advanced == 128);

    REQUIRE(ext.beginCalls == 1);
    REQUIRE(tp->calls == 1);
    REQUIRE(ext.endCalls == 1);

    REQUIRE(sink.writes == 1);
    REQUIRE(phase == 60);
}

