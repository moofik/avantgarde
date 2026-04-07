#include <catch2/catch_all.hpp>

#include <algorithm>
#include <numeric>
#include <vector>

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
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active); // baseline init, без автозапуска

    req.nowMs = 1100;
    req.value01 = 0.20f;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 1300;
    req.value01 = 0.20f;
    REQUIRE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 2101;
    req.value01 = 0.20f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active);

    req.nowMs = 2150;
    req.value01 = 0.30f;
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
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(a).active);

    a.nowMs = 1030;
    a.value01 = 0.3f;
    REQUIRE(fx.resolveGlitchTextStyle(a).active);

    VisualFxRequest b = a;
    b.instanceKey = "sceneA.knobB";
    b.nowMs = 1000;
    b.value01 = 0.6f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(b).active);
    b.nowMs = 1040;
    b.value01 = 0.7f;
    REQUIRE(fx.resolveGlitchTextStyle(b).active);

    a.nowMs = 2100;
    a.value01 = 0.3f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(a).active);

    b.nowMs = 2100;
    b.value01 = 0.7f;
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(b).active);
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
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
    req.nowMs = 1050;
    req.value01 = 0.2f;
    REQUIRE(fx.resolveTextStyle(req).active);

    req.nowMs = 2800;
    req.value01 = 0.2f;
    REQUIRE(fx.resolveTextStyle(req).active);

    req.nowMs = 3051;
    req.value01 = 0.2f;
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
    req.value01 = 0.1f;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);

    for (int i = 0; i < 30; ++i) {
        req.nowMs = 1120 + static_cast<uint64_t>(i) * 120ULL;
        req.value01 = std::clamp(0.12f + 0.02f * static_cast<float>(i), 0.0f, 1.0f);
        REQUIRE(fx.resolveTextStyle(req).active);
    }

    req.nowMs += 1200;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: change trigger respects effect_speed for release timing") {
    VisualFxProcessor fxFast{};
    VisualFxRequest fast{};
    fast.nodeId = "mode_label";
    fast.instanceKey = "tracks.mode";
    fast.effect = "glitch";
    fast.effectTrigger = "change";
    fast.effectTriggerOutMs = 1000;
    fast.effectSpeed = 2.0f;
    fast.hasValue01 = true;
    fast.nowMs = 1000;
    fast.value01 = 0.1f;
    REQUIRE_FALSE(fxFast.resolveTextStyle(fast).active);
    fast.nowMs = 1050;
    fast.value01 = 0.2f;
    REQUIRE(fxFast.resolveTextStyle(fast).active);
    fast.nowMs = 1750; // 700ms > (1000 / speed=2) => уже должен отпуститься
    REQUIRE_FALSE(fxFast.resolveTextStyle(fast).active);

    VisualFxProcessor fxSlow{};
    VisualFxRequest slow = fast;
    slow.effectSpeed = 0.5f;
    slow.nowMs = 1000;
    slow.value01 = 0.1f;
    REQUIRE_FALSE(fxSlow.resolveTextStyle(slow).active);
    slow.nowMs = 1050;
    slow.value01 = 0.2f;
    REQUIRE(fxSlow.resolveTextStyle(slow).active);
    slow.nowMs = 1750; // 700ms < (1000 / speed=0.5 => 2000ms), еще активен
    REQUIRE(fxSlow.resolveTextStyle(slow).active);
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
    REQUIRE_FALSE(fx.resolveGlitchTextStyle(req).active);

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
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
    req.nowMs = 1500;
    const bool activeShort = fx.resolveTextStyle(req).active;

    VisualFxProcessor fx2{};
    req.effectIntervalMs = 12000;
    req.nowMs = 0;
    REQUIRE_FALSE(fx2.resolveTextStyle(req).active);
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
    REQUIRE_FALSE(fx.resolveBlockStyle(req).active); // warmup, эффект еще не стартует
    req.nowMs = 6001;

    const VisualFxBlockStyle block = fx.resolveBlockStyle(req);
    REQUIRE(block.active);
    const auto* glitch = std::get_if<GlitchVisualFxPayload>(&block.payload);
    REQUIRE(glitch != nullptr);
    REQUIRE(glitch->splitPx > 0.0f);
}

TEST_CASE("VisualFxProcessor: resolves glow effect style") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "mode_label";
    req.instanceKey = "tracks.mode";
    req.effect = "glow";
    req.effectTrigger = "change";
    req.effectTriggerOutMs = 1000;
    req.effectAmount = 0.30f;
    req.effectSpeed = 1.0f;
    req.hasValue01 = true;

    req.nowMs = 1000;
    req.value01 = 0.10f;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);

    req.nowMs = 1080;
    req.value01 = 0.30f;
    const VisualFxTextStyle style = fx.resolveTextStyle(req);
    REQUIRE(style.active);
    REQUIRE(style.splitPx > 0.0f);
}

TEST_CASE("VisualFxProcessor: glow parses hex tint color correctly") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "mode_label";
    req.instanceKey = "tracks.mode";
    req.effect = "glow";
    req.effectColor = "#210000";
    req.effectTrigger = "time";
    req.effectAmount = 0.35f;
    req.effectSpeed = 1.0f;
    req.effectIntervalMs = 1U;

    req.nowMs = 1000U;
    REQUIRE_FALSE(fx.resolveBlockStyle(req).active); // warmup epoch
    req.nowMs = 1400U;
    const VisualFxBlockStyle block = fx.resolveBlockStyle(req);
    REQUIRE(block.active);
    const auto* glow = std::get_if<GlowVisualFxPayload>(&block.payload);
    REQUIRE(glow != nullptr);
    REQUIRE(glow->hasTint);
    REQUIRE(glow->tintR == Catch::Approx(33.0f / 255.0f).margin(0.001f));
    REQUIRE(glow->tintG == Catch::Approx(0.0f).margin(0.0001f));
    REQUIRE(glow->tintB == Catch::Approx(0.0f).margin(0.0001f));
}

TEST_CASE("VisualFxProcessor: glow change trigger without value does not fallback to time") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "mode_label";
    req.instanceKey = "tracks.mode";
    req.effect = "glow";
    req.effectTrigger = "change";
    req.effectIntervalMs = 200U;
    req.effectAmount = 0.35f;
    req.effectSpeed = 1.0f;

    req.nowMs = 1000U;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
    req.nowMs = 5000U;
    REQUIRE_FALSE(fx.resolveTextStyle(req).active);
}

TEST_CASE("VisualFxProcessor: rejects legacy chained effect id syntax") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "mode_label";
    req.instanceKey = "tracks.mode";
    req.effect = "glitch, glow";
    req.effectTrigger = "change";
    req.effectTriggerOutMs = 1000;
    req.effectAmount = 0.25f;
    req.effectSpeed = 1.0f;
    req.hasValue01 = true;

    req.nowMs = 1000;
    req.value01 = 0.10f;
    REQUIRE_FALSE(fx.resolveBlockStyle(req).active);

    req.nowMs = 1120;
    req.value01 = 0.25f;
    const VisualFxBlockStyle block = fx.resolveBlockStyle(req);
    REQUIRE_FALSE(block.active);
    REQUIRE(std::holds_alternative<std::monostate>(block.payload));
}

TEST_CASE("VisualFxProcessor: applyRgba modifies ROI for glow") {
    VisualFxProcessor fx{};
    std::vector<uint8_t> px(32U * 32U * 4U, 0U);
    // Одна непрозрачная точка-эмиттер в центре.
    const std::size_t center = (16U * 32U + 16U) * 4U;
    px[center + 0U] = 255U;
    px[center + 1U] = 255U;
    px[center + 2U] = 255U;
    px[center + 3U] = 255U;

    VisualFxRgbaView roi{};
    roi.pixels = px.data();
    roi.width = 32U;
    roi.height = 32U;
    roi.strideBytes = 32U * 4U;

    VisualFxRequest req{};
    req.effect = "glow";
    req.effectTrigger = "time";
    req.effectAmount = 0.5f;
    req.effectSpeed = 1.0f;
    req.effectIntervalMs = 1U;
    req.nowMs = 1000U;

    const uint32_t sumBefore =
        std::accumulate(px.begin(), px.end(), 0U, [](uint32_t acc, uint8_t v) { return acc + v; });
    // Первый вызов инициализирует time-epoch.
    (void)fx.applyRgba(roi, req);
    req.nowMs = 1400U;
    REQUIRE(fx.applyRgba(roi, req));
    const uint32_t sumAfter =
        std::accumulate(px.begin(), px.end(), 0U, [](uint32_t acc, uint8_t v) { return acc + v; });
    REQUIRE(sumAfter > sumBefore);
}

TEST_CASE("VisualFxProcessor: typing trigger change starts on value change") {
    VisualFxProcessor fx{};
    VisualFxRequest req{};
    req.nodeId = "dialog";
    req.instanceKey = "scene.dialog.line1";
    req.nodeText = "HELLO WORLD";
    req.effect = "typing";
    req.effectTrigger = "change";
    req.effectSpeed = 1.0f;
    req.hasValue01 = true;

    req.nowMs = 1000U;
    req.value01 = 0.10f;
    REQUIRE_FALSE(fx.resolveBlockStyle(req).active); // baseline

    req.nowMs = 1100U;
    req.value01 = 0.20f;
    const VisualFxBlockStyle started = fx.resolveBlockStyle(req);
    REQUIRE(started.active);
    const auto* typing = std::get_if<TypingVisualFxPayload>(&started.payload);
    REQUIRE(typing != nullptr);
    REQUIRE(typing->reveal01 <= 0.30f);
}

TEST_CASE("VisualFxProcessor: typing applyRgba hides unrevealed columns") {
    VisualFxProcessor fx{};
    std::vector<uint8_t> px(32U * 8U * 4U, 0U);
    // Имитируем "полосу текста" как непрозрачный блок.
    for (uint16_t y = 1U; y < 7U; ++y) {
        for (uint16_t x = 2U; x < 30U; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * 32U + x) * 4U;
            px[idx + 0U] = 240U;
            px[idx + 1U] = 220U;
            px[idx + 2U] = 210U;
            px[idx + 3U] = 255U;
        }
    }

    VisualFxRgbaView roi{};
    roi.pixels = px.data();
    roi.width = 32U;
    roi.height = 8U;
    roi.strideBytes = 32U * 4U;

    VisualFxRequest req{};
    req.nodeId = "dialog";
    req.instanceKey = "scene.dialog.line1";
    req.nodeText = "HELLO WORLD";
    req.effect = "typing";
    req.effectTrigger = "change";
    req.effectSpeed = 0.8f;
    req.hasValue01 = true;

    req.nowMs = 1000U;
    req.value01 = 0.10f;
    (void)fx.applyRgba(roi, req); // baseline

    req.nowMs = 1100U;
    req.value01 = 0.20f; // change -> старт печати
    const uint32_t sumBefore =
        std::accumulate(px.begin(), px.end(), 0U, [](uint32_t acc, uint8_t v) { return acc + v; });
    REQUIRE(fx.applyRgba(roi, req));
    const uint32_t sumAfter =
        std::accumulate(px.begin(), px.end(), 0U, [](uint32_t acc, uint8_t v) { return acc + v; });
    REQUIRE(sumAfter < sumBefore);
}
