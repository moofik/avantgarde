#include <catch2/catch_all.hpp>

#include "service/pattern/PatternBank.h"

using namespace avantgarde;

TEST_CASE("PatternBank: put/get/contains/erase") {
    PatternBank bank{};

    PatternState p{};
    p.id = 7;
    p.lengthInSteps = 64;
    p.stepsPerBeat = 4;
    p.transport.bpm = 128.0f;

    REQUIRE(bank.size() == 0);
    REQUIRE_FALSE(bank.contains(p.id));

    REQUIRE(bank.put(p));
    REQUIRE(bank.size() == 1);
    REQUIRE(bank.contains(p.id));

    PatternState out{};
    REQUIRE(bank.get(7, out));
    CHECK(out.id == 7);
    CHECK(out.lengthInSteps == 64);
    CHECK(out.transport.bpm == Catch::Approx(128.0f));

    REQUIRE(bank.erase(7));
    REQUIRE(bank.size() == 0);
    REQUIRE_FALSE(bank.contains(7));
    REQUIRE_FALSE(bank.get(7, out));
}

TEST_CASE("PatternBank: invalid pattern id is rejected") {
    PatternBank bank{};
    PatternState p{};
    p.id = kInvalidPatternId;
    REQUIRE_FALSE(bank.put(p));
}

