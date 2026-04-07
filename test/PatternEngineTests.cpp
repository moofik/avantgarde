#include <catch2/catch_all.hpp>

#include "service/pattern/PatternEngine.h"

using namespace avantgarde;

namespace {

TransportRtSnapshot makeTransport(bool playing, uint64_t sampleTime) {
    TransportRtSnapshot s{};
    s.playing = playing;
    s.tsNum = 4;
    s.tsDen = 4;
    s.ppq = 96;
    s.bpm = 120.0f;
    s.quant = QuantizeMode::Bar;
    s.swing = 0.0f;
    s.sampleTime = sampleTime;
    return s;
}

PatternState makePattern(PatternId id) {
    PatternState p{};
    p.id = id;
    p.lengthInSteps = 64;
    p.stepsPerBeat = 4;
    return p;
}

} // namespace

TEST_CASE("PatternEngine: activate only existing patterns") {
    PatternEngine engine{48000.0};
    REQUIRE_FALSE(engine.setActivePattern(2));

    REQUIRE(engine.putPattern(makePattern(2)));
    REQUIRE(engine.setActivePattern(2));
    CHECK(engine.activePatternId() == 2);
}

TEST_CASE("PatternEngine: quantized switch promotes active pattern when ready") {
    PatternEngine engine{48000.0};
    REQUIRE(engine.putPattern(makePattern(1)));
    REQUIRE(engine.putPattern(makePattern(3)));
    REQUIRE(engine.setActivePattern(1));
    CHECK(engine.activePatternId() == 1);

    engine.requestSwitch(3, QuantizeMode::Bar);

    // До границы бара switch не готов.
    engine.onTransportRt(makeTransport(true, 50000));
    PatternId switched = kInvalidPatternId;
    REQUIRE_FALSE(engine.popReadySwitch(switched));
    CHECK(engine.activePatternId() == 1);

    // На границе бара switch должен сработать.
    engine.onTransportRt(makeTransport(true, 96000));
    REQUIRE(engine.popReadySwitch(switched));
    CHECK(switched == 3);
    CHECK(engine.activePatternId() == 3);
}

TEST_CASE("PatternEngine: ignores ready switch to missing pattern") {
    PatternEngine engine{48000.0};
    REQUIRE(engine.putPattern(makePattern(1)));
    REQUIRE(engine.setActivePattern(1));

    // Запросим switch на отсутствующий паттерн.
    engine.requestSwitch(99, QuantizeMode::None);
    engine.onTransportRt(makeTransport(true, 0));

    PatternId switched = kInvalidPatternId;
    REQUIRE_FALSE(engine.popReadySwitch(switched));
    CHECK(engine.activePatternId() == 1);
}

TEST_CASE("PatternEngine: putPattern keeps bank and snapshot manager in sync") {
    PatternEngine engine{48000.0};
    PatternState p = makePattern(7);
    p.transport.bpm = 126.0f;

    REQUIRE(engine.putPattern(p));
    CHECK(engine.bank().contains(7));
    CHECK(engine.snapshots().contains(7));

    const CompiledPatternSnapshot* snap = nullptr;
    REQUIRE(engine.snapshots().get(7, snap));
    REQUIRE(snap != nullptr);
    CHECK(snap->transport.bpm == Catch::Approx(126.0f));
}

TEST_CASE("PatternEngine: erasePattern removes from bank and snapshots and resets active id") {
    PatternEngine engine{48000.0};
    REQUIRE(engine.putPattern(makePattern(4)));
    REQUIRE(engine.setActivePattern(4));
    CHECK(engine.activePatternId() == 4);

    REQUIRE(engine.erasePattern(4));
    CHECK_FALSE(engine.bank().contains(4));
    CHECK_FALSE(engine.snapshots().contains(4));
    CHECK(engine.activePatternId() == kInvalidPatternId);
}
