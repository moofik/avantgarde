#include <catch2/catch_all.hpp>

#include "contracts/IAudioEngine.h"
#include "contracts/ITrack.h"
#include "contracts/IParamBridge.h"
#include "contracts/IRtCommandQueue.h"
#include "contracts/types.h"
#include "contracts/ids.h"

#include <vector>
#include <memory>

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

    void pushParam(Target, std::size_t, float) override {}

    void swapBuffers() override { ++swaps; }
};

// Фабрика из реализации (объявлена в AudioEngine.cpp)
namespace avantgarde {
    std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue*, IParamBridge*);
}

// Helpers
static AudioProcessContext makeCtx(int frames = 256) {
    AudioProcessContext ctx{};
    ctx.nframes = static_cast<std::size_t>(frames);
    ctx.in = nullptr; // smoke-тесты не требуют реальных буферов
    ctx.out = nullptr;
    return ctx;
}

// --- Tests ---

TEST_CASE("Register/Process No-Crash") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);
    eng->setSampleRate(48000.0);
    auto ctx= makeCtx();
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

TEST_CASE("onCommand() routes to RT queue; RT pops in processBlock") {
    MockRtQueue q;
    MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);

    Command c{"play", {0, -1}, 1.0f};
    eng->onCommand(c); // вне RT → push

    REQUIRE(q.size() == 1);
    auto ctx = makeCtx();
    eng->processBlock(ctx); // RT → pop
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
    eng->onCommand(c); // третья переполнит cap=2 в мок-реализации

    REQUIRE(q.overflowFlagAndReset() == true);
    REQUIRE(q.overflowFlagAndReset() == false); // флаг сброшен
}

TEST_CASE("ParamSet is routed into track->onRtCommand") {
    MockRtQueue q; MockParamBridge p;
    auto eng = avantgarde::MakeAudioEngine(&q, &p);
    auto t = std::make_unique<MockTrack>();
    auto* tp = t.get();
    eng->registerTrack(std::move(t));

    // вне RT → формируем команду (как сейчас через onCommand или сразу push в очередь)
    RtCommand rc{};
    rc.id = static_cast<uint16_t>(CmdId::ParamSet);
    rc.track = 0; rc.index = 3; rc.value = 0.75f;
    q.push(rc);

    auto ctx = makeCtx();
    eng->processBlock(ctx);

    REQUIRE(tp->seen.size() == 1);
    REQUIRE(tp->seen[0].index == 3);
    REQUIRE(tp->seen[0].value == Catch::Approx(0.75f));
}