#include <catch2/catch_all.hpp>

#include "runtime/TransportBridgeDualBuffer.h"
#include "contracts/ids.h"

using namespace avantgarde;

TEST_CASE("TransportBridgeDualBuffer: defaults are stable") {
    TransportBridgeDualBuffer tr;
    tr.swapBuffers();
    const auto& rt = tr.rt();

    REQUIRE(rt.playing == false);
    REQUIRE(rt.tsNum == 4);
    REQUIRE(rt.tsDen == 4);
    REQUIRE(rt.ppq == 96);
    REQUIRE(rt.bpm == Catch::Approx(120.0f));
    REQUIRE(rt.quant == QuantizeMode::Bar);
    REQUIRE(rt.swing == Catch::Approx(0.0f));
    REQUIRE(rt.sampleTime == 0);
}

TEST_CASE("TransportBridgeDualBuffer: control updates become visible after swap") {
    TransportBridgeDualBuffer tr;

    tr.setPlaying(true);
    tr.setTempo(133.5f);
    tr.setTimeSignature(3, 8);
    tr.setQuantize(QuantizeMode::Beat);
    tr.setSwing(0.42f);

    tr.swapBuffers();
    const auto& rt = tr.rt();

    REQUIRE(rt.playing == true);
    REQUIRE(rt.bpm == Catch::Approx(133.5f));
    REQUIRE(rt.tsNum == 3);
    REQUIRE(rt.tsDen == 8);
    REQUIRE(rt.quant == QuantizeMode::Beat);
    REQUIRE(rt.swing == Catch::Approx(0.42f));
}

TEST_CASE("TransportBridgeDualBuffer: sampleTime is RT-owned") {
    TransportBridgeDualBuffer tr;

    tr.swapBuffers();
    REQUIRE(tr.rt().sampleTime == 0);

    tr.advanceSampleTime(256);
    REQUIRE(tr.rt().sampleTime == 256);

    tr.setPlaying(true);
    tr.swapBuffers();
    REQUIRE(tr.rt().sampleTime == 256);

    tr.advanceSampleTime(128);
    REQUIRE(tr.rt().sampleTime == 384);
}

TEST_CASE("TransportBridgeDualBuffer: invalid values are normalized") {
    TransportBridgeDualBuffer tr;

    tr.setTempo(-100.0f);
    tr.setTimeSignature(0, 7);
    tr.setSwing(2.5f);
    tr.swapBuffers();

    const auto& rt = tr.rt();
    REQUIRE(rt.bpm == Catch::Approx(20.0f));
    REQUIRE(rt.tsNum == 4);
    REQUIRE(rt.tsDen == 4);
    REQUIRE(rt.swing == Catch::Approx(1.0f));
}

TEST_CASE("TransportBridgeDualBuffer: IParameterized surface controls transport") {
    TransportBridgeDualBuffer tr;

    REQUIRE(tr.getParamCount() == 6);
    REQUIRE(tr.getParamMeta(toParamIndex(TransportParamId::TempoNorm)).name == "transport.tempo_norm");

    tr.setParam(toParamIndex(TransportParamId::Playing), 1.0f);
    tr.setParam(toParamIndex(TransportParamId::TempoNorm), 0.0f);   // 20 BPM
    tr.setParam(toParamIndex(TransportParamId::QuantizeNorm), 0.5f); // Beat
    tr.setParam(toParamIndex(TransportParamId::Swing01), 0.75f);

    tr.swapBuffers();
    const auto& rt = tr.rt();
    REQUIRE(rt.playing == true);
    REQUIRE(rt.bpm == Catch::Approx(20.0f));
    REQUIRE(rt.quant == QuantizeMode::Beat);
    REQUIRE(rt.swing == Catch::Approx(0.75f));
}
