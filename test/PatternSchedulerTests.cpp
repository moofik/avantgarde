#include <catch2/catch_all.hpp>

#include "service/pattern/PatternScheduler.h"

using namespace avantgarde;

namespace {

TransportRtSnapshot makeTransport(bool playing,
                                  float bpm,
                                  uint8_t tsNum,
                                  uint8_t tsDen,
                                  uint64_t sampleTime) {
    TransportRtSnapshot s{};
    s.playing = playing;
    s.tsNum = tsNum;
    s.tsDen = tsDen;
    s.ppq = 96;
    s.bpm = bpm;
    s.quant = QuantizeMode::Bar;
    s.swing = 0.0f;
    s.sampleTime = sampleTime;
    return s;
}

} // namespace

TEST_CASE("PatternScheduler: Quantize None switches immediately") {
    PatternScheduler scheduler{48000.0};
    scheduler.requestSwitch(PatternSwitchRequest{
        .target = 3,
        .quantize = QuantizeMode::None
    });

    const auto tr = makeTransport(true, 120.0f, 4, 4, 1234);
    scheduler.onTransport(tr);

    PatternId ready = kInvalidPatternId;
    REQUIRE(scheduler.popReadySwitch(ready));
    CHECK(ready == 3);
}

TEST_CASE("PatternScheduler: switch is immediate while transport is stopped") {
    PatternScheduler scheduler{48000.0};
    scheduler.requestSwitch(PatternSwitchRequest{
        .target = 5,
        .quantize = QuantizeMode::Bar
    });

    const auto tr = makeTransport(false, 120.0f, 4, 4, 50000);
    scheduler.onTransport(tr);

    PatternId ready = kInvalidPatternId;
    REQUIRE(scheduler.popReadySwitch(ready));
    CHECK(ready == 5);
}

TEST_CASE("PatternScheduler: Quantize Beat waits for next beat") {
    PatternScheduler scheduler{48000.0};
    scheduler.requestSwitch(PatternSwitchRequest{
        .target = 9,
        .quantize = QuantizeMode::Beat
    });

    // 120 bpm => 24000 samples per beat.
    auto tr = makeTransport(true, 120.0f, 4, 4, 1000);
    scheduler.onTransport(tr);

    PatternId ready = kInvalidPatternId;
    REQUIRE_FALSE(scheduler.popReadySwitch(ready));

    tr.sampleTime = 23999;
    scheduler.onTransport(tr);
    REQUIRE_FALSE(scheduler.popReadySwitch(ready));

    tr.sampleTime = 24000;
    scheduler.onTransport(tr);
    REQUIRE(scheduler.popReadySwitch(ready));
    CHECK(ready == 9);
}

TEST_CASE("PatternScheduler: latest request wins before due point") {
    PatternScheduler scheduler{48000.0};
    auto tr = makeTransport(true, 120.0f, 4, 4, 1000);

    scheduler.requestSwitch(PatternSwitchRequest{
        .target = 1,
        .quantize = QuantizeMode::Bar
    });
    scheduler.onTransport(tr);

    scheduler.requestSwitch(PatternSwitchRequest{
        .target = 2,
        .quantize = QuantizeMode::Bar
    });
    scheduler.onTransport(tr);

    PatternId ready = kInvalidPatternId;
    REQUIRE_FALSE(scheduler.popReadySwitch(ready));

    // 4/4 @120 bpm => 96000 samples per bar.
    tr.sampleTime = 96000;
    scheduler.onTransport(tr);
    REQUIRE(scheduler.popReadySwitch(ready));
    CHECK(ready == 2);
}

