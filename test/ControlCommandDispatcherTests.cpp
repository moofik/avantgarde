#include <catch2/catch_all.hpp>
#include <vector>

#include "control/ControlCommandDispatcher.h"

using namespace avantgarde;

namespace {

struct MockQueue final : IRtCommandQueue {
    int pushCalls = 0;
    std::vector<RtCommand> pushed;

    bool push(const RtCommand& cmd) noexcept override {
        ++pushCalls;
        pushed.push_back(cmd);
        return true;
    }

    bool pop(RtCommand&) noexcept override { return false; }
    void clear() noexcept override {}
    std::size_t capacity() const noexcept override { return 32; }
    std::size_t size() const noexcept override { return 0; }
    bool overflowFlagAndReset() noexcept override { return false; }
};

} // namespace

TEST_CASE("ControlCommandDispatcher: play goes directly to rtQueue") {
    MockQueue q;
    ControlCommandDispatcher d(&q);
    REQUIRE(d.sendPlay(0, 0));

    REQUIRE(q.pushCalls == 1);
    REQUIRE(q.pushed[0].id == static_cast<uint16_t>(CmdId::Play));
}

TEST_CASE("ControlCommandDispatcher: quantize mode command goes to rtQueue") {
    MockQueue q;
    ControlCommandDispatcher d(&q);
    REQUIRE(d.setQuantizeMode(QuantizeMode::Beat));

    REQUIRE(q.pushCalls == 1);
    REQUIRE(q.pushed[0].id == static_cast<uint16_t>(CmdId::QuantizeMode));
    REQUIRE(q.pushed[0].track == -1);
    REQUIRE(q.pushed[0].slot == -1);
    REQUIRE(q.pushed[0].value == Catch::Approx(1.0f));
}

TEST_CASE("ControlCommandDispatcher: tempo and time signature go to rtQueue as global commands") {
    MockQueue q;
    ControlCommandDispatcher d(&q);

    REQUIRE(d.setTempoBpm(133.0f));
    REQUIRE(d.setTimeSignature(7, 8));

    REQUIRE(q.pushCalls == 2);

    REQUIRE(q.pushed[0].id == static_cast<uint16_t>(CmdId::SetTempoBpm));
    REQUIRE(q.pushed[0].track == -1);
    REQUIRE(q.pushed[0].slot == -1);
    REQUIRE(q.pushed[0].value == Catch::Approx(133.0f));

    REQUIRE(q.pushed[1].id == static_cast<uint16_t>(CmdId::SetTimeSig));
    REQUIRE(q.pushed[1].track == -1);
    REQUIRE(q.pushed[1].slot == -1);
    REQUIRE(q.pushed[1].index == 8);
    REQUIRE(q.pushed[1].value == Catch::Approx(7.0f));
}

TEST_CASE("ControlCommandDispatcher: param set goes to rtQueue with index") {
    MockQueue q;
    ControlCommandDispatcher d(&q);
    REQUIRE(d.sendParamSet(1, 0, 2, 1.5f));

    REQUIRE(q.pushCalls == 1);
    REQUIRE(q.pushed[0].id == static_cast<uint16_t>(CmdId::ParamSet));
    REQUIRE(q.pushed[0].track == 1);
    REQUIRE(q.pushed[0].slot == 0);
    REQUIRE(q.pushed[0].index == 2);
    REQUIRE(q.pushed[0].value == Catch::Approx(1.5f));
}

TEST_CASE("ControlCommandDispatcher: note commands go to rtQueue with normalized payload") {
    MockQueue q;
    ControlCommandDispatcher d(&q);

    REQUIRE(d.sendNoteOn(2, /*key=*/60, /*velocity=*/1.5f));      // clamp -> 1.0
    REQUIRE(d.sendNoteOff(2, /*key=*/60));
    REQUIRE(d.sendNoteDetune(2, /*key=*/60, /*detune=*/-2.0f));   // clamp -> -1.0

    REQUIRE(q.pushCalls == 3);

    REQUIRE(q.pushed[0].id == static_cast<uint16_t>(CmdId::NoteOn));
    REQUIRE(q.pushed[0].track == 2);
    REQUIRE(q.pushed[0].slot == -1);
    REQUIRE(q.pushed[0].index == 60);
    REQUIRE(q.pushed[0].value == Catch::Approx(1.0f));

    REQUIRE(q.pushed[1].id == static_cast<uint16_t>(CmdId::NoteOff));
    REQUIRE(q.pushed[1].track == 2);
    REQUIRE(q.pushed[1].slot == -1);
    REQUIRE(q.pushed[1].index == 60);

    REQUIRE(q.pushed[2].id == static_cast<uint16_t>(CmdId::NoteDetune));
    REQUIRE(q.pushed[2].track == 2);
    REQUIRE(q.pushed[2].slot == -1);
    REQUIRE(q.pushed[2].index == 60);
    REQUIRE(q.pushed[2].value == Catch::Approx(-1.0f));
}
