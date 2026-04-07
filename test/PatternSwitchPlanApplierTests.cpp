#include <catch2/catch_all.hpp>

#include "service/pattern/PatternSwitchPlanApplier.h"

using namespace avantgarde;

namespace {

struct MockPatternTarget final : IPatternApplyTarget {
    float tempo{0.0f};
    uint8_t tsNum{0};
    uint8_t tsDen{0};
    QuantizeMode quant{QuantizeMode::None};
    float swing{0.0f};

    bool muted{false};
    bool armed{false};
    uint32_t bars{0};
    uint32_t clipRef{0};
    bool cleared{false};
    uint16_t trackParamIndex{0};
    float trackParamValue{0.0f};
    uint8_t fxSlot{0};
    uint16_t fxParamIndex{0};
    float fxParamValue{0.0f};

    bool setTransportTempo(float bpm) noexcept override {
        tempo = bpm;
        return true;
    }
    bool setTransportTimeSignature(uint8_t num, uint8_t den) noexcept override {
        tsNum = num;
        tsDen = den;
        return true;
    }
    bool setTransportQuantize(QuantizeMode q) noexcept override {
        quant = q;
        return true;
    }
    bool setTransportSwing(float swing01) noexcept override {
        swing = swing01;
        return true;
    }
    bool setTrackMuted(uint8_t, bool m) noexcept override {
        muted = m;
        return true;
    }
    bool setTrackArmed(uint8_t, bool a) noexcept override {
        armed = a;
        return true;
    }
    bool setTrackBars(uint8_t, uint32_t b) noexcept override {
        bars = b;
        return true;
    }
    bool setTrackClipRef(uint8_t, uint32_t clipRefId) noexcept override {
        clipRef = clipRefId;
        return true;
    }
    bool clearTrackSample(uint8_t) noexcept override {
        cleared = true;
        return true;
    }
    bool setTrackParam(uint8_t, uint16_t paramIndex, float value) noexcept override {
        trackParamIndex = paramIndex;
        trackParamValue = value;
        return true;
    }
    bool setFxParam(uint8_t, uint8_t slot, uint16_t paramIndex, float value) noexcept override {
        fxSlot = slot;
        fxParamIndex = paramIndex;
        fxParamValue = value;
        return true;
    }
};

} // namespace

TEST_CASE("PatternSwitchPlanApplier: applies transport/track/fx ops and clip clear semantics") {
    CompiledSwitchPlan plan{};
    plan.from = 1;
    plan.to = 2;
    plan.ops = {
        PatternApplyOp{.kind = PatternApplyOpKind::TransportSetTempo, .value = 128.0f},
        PatternApplyOp{.kind = PatternApplyOpKind::TransportSetTimeSig, .index = 8, .value = 7.0f},
        PatternApplyOp{.kind = PatternApplyOpKind::TransportSetQuant, .value = 2.0f},
        PatternApplyOp{.kind = PatternApplyOpKind::TransportSetSwing, .value = 0.33f},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackSetMuted, .trackId = 0, .value = 1.0f},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackSetArmed, .trackId = 0, .value = 1.0f},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackSetBars, .trackId = 0, .valueU32 = 8},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackSetClipRef, .trackId = 0, .valueU32 = 1234},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackSetClipRef, .trackId = 0, .valueU32 = 0},
        PatternApplyOp{.kind = PatternApplyOpKind::TrackParamSet, .trackId = 0, .index = 99, .value = 0.75f},
        PatternApplyOp{.kind = PatternApplyOpKind::FxParamSet, .trackId = 0, .slot = 2, .index = 5, .value = 0.5f}
    };

    MockPatternTarget target{};
    const PatternSwitchApplyReport report = PatternSwitchPlanApplier::apply(plan, target);

    CHECK(report.totalOps == plan.ops.size());
    CHECK(report.failedOps == 0);
    CHECK(report.appliedOps == plan.ops.size());
    CHECK(report.ok());

    CHECK(target.tempo == Catch::Approx(128.0f));
    CHECK(target.tsNum == 7);
    CHECK(target.tsDen == 8);
    CHECK(target.quant == QuantizeMode::Bar);
    CHECK(target.swing == Catch::Approx(0.33f));
    CHECK(target.muted);
    CHECK(target.armed);
    CHECK(target.bars == 8);
    CHECK(target.clipRef == 1234);
    CHECK(target.cleared);
    CHECK(target.trackParamIndex == 99);
    CHECK(target.trackParamValue == Catch::Approx(0.75f));
    CHECK(target.fxSlot == 2);
    CHECK(target.fxParamIndex == 5);
    CHECK(target.fxParamValue == Catch::Approx(0.5f));
}

