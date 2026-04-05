#include <catch2/catch_all.hpp>

#include <algorithm>

#include "platform/render/VisualFxProcessor.h"

using namespace avantgarde;

TEST_CASE("VisualFxProcessor: change trigger stays active while value is changing") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "knob_a";
    req.instanceKey = "sceneA.knobA";
    req.effect = "glitch";
    req.effectTrigger = "change";
    req.effectAmount = 0.3f;
    req.effectSpeed = 1.0f;
    req.hasValue01 = true;

    req.nowMs = 1000;
    req.value01 = 0.10f;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 1300;
    req.value01 = 0.10f;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 2100;
    req.value01 = 0.10f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 2150;
    req.value01 = 0.20f;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: change trigger keeps per-node independent state") {
    VisualFxProcessor fx{};

    VisualFxRequest a{};
    a.nodeId = "knob_shared";
    a.instanceKey = "sceneA.knobA";
    a.effect = "glitch";
    a.effectTrigger = "change";
    a.hasValue01 = true;
    a.value01 = 0.2f;
    a.nowMs = 1000;
    REQUIRE(fx.resolveGlitchTextStyle(a).active);

    VisualFxRequest b = a;
    b.instanceKey = "sceneA.knobB";
    b.value01 = 0.6f;
    REQUIRE(fx.resolveGlitchTextStyle(b).active);

    a.nowMs = 2100;
    a.value01 = 0.2f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(a).active);

    b.nowMs = 2100;
    b.value01 = 0.7f;
    REQUIRE(fx.resolveGlitchTextStyle(b).active);
}

TEST_CASE("VisualFxProcessor: change trigger uses configurable release timeout") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "knob_release";
    req.instanceKey = "sceneA.knobRelease";
    req.effect = "glitch";
    req.effectTrigger = "change";
    req.effectTriggerOutMs = 2000;
    req.hasValue01 = true;

    req.nowMs = 1000;
    req.value01 = 0.1f;
    REQUIRE(fx.resolveTextStyle(req).active);

    req.nowMs = 2800;
    req.value01 = 0.1f;
    REQUIRE(fx.resolveTextStyle(req).active);

    req.nowMs = 3050;
    req.value01 = 0.1f;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: change trigger stays active during long continuous change") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "knob_long";
    req.instanceKey = "sceneA.knobLong";
    req.effect = "glitch";
    req.effectTrigger = "change";
    req.effectTriggerOutMs = 1000;
    req.hasValue01 = true;
    req.nowMs = 1000;

    for (int i = 0; i < 30; ++i) {
        req.nowMs = 1000 + static_cast<uint64_t>(i) * 120ULL;
        req.value01 = std::clamp(0.1f + 0.02f * static_cast<float>(i), 0.0f, 1.0f);
        REQUIRE(fx.resolveTextStyle(req).active);
    }

    req.nowMs += 1200;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: interval controls stable start period in time mode") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "title";
    req.effect = "glitch";
    req.effectTrigger = "time";
    req.effectIntervalMs = 10000;
    req.effectAmount = 0.2f;
    req.effectSpeed = 0.2f;

    req.nowMs = 1000;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 2500;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 11000;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 12500;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: interval affects start period, not burst duration scaling") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "title";
    req.instanceKey = "tracks.header_title";
    req.effect = "glitch";
    req.effectTrigger = "time";
    req.effectAmount = 0.3f;
    req.effectSpeed = 0.2f;

    req.effectIntervalMs = 3000;
    req.nowMs = 0;
    REQUIRE(fx.resolveTextStyle(req).active);
    req.nowMs = 1500;
    const bool activeShort = fx.resolveTextStyle(req).active;

    VisualFxProcessor fx2{};
    req.effectIntervalMs = 12000;
    req.nowMs = 0;
    REQUIRE(fx2.resolveTextStyle(req).active);
    req.nowMs = 1500;
    const bool activeLong = fx2.resolveTextStyle(req).active;

    // Длительность вспышки в первую очередь определяется amount/speed, а не interval.
    REQUIRE(activeShort == activeLong);
}

TEST_CASE("VisualFxProcessor: resolves generic block style for glitch") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "anim";
    req.instanceKey = "fx.anim.current";
    req.effect = "glitch";
    req.effectTrigger = "time";
    req.effectIntervalMs = 4000;
    req.effectAmount = 0.5f;
    req.effectSpeed = 1.2f;
    req.nowMs = 2000;

    const VisualFxBlockStyle block = fx.resolveBlockStyle(req);
    REQUIRE(block.active);
    REQUIRE(block.splitPx > 0.0f);
}
