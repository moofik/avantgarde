#include <catch2/catch_all.hpp>

#include "service/ui/UiBindResolver.h"

using namespace avantgarde;

TEST_CASE("UiBindResolver: FxEditor knob bind resolves from canonical fx.selected.param key") {
    const UiBindResolution r0 = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob,
                                                        "fx.selected.param.0");
    REQUIRE(r0.ok);
    REQUIRE(r0.canonical == "fx.selected.param.0");
    REQUIRE(r0.actionId == UiAction::Id::SceneFxParamValue);
    REQUIRE(r0.paramIndex == 0);

    const UiBindResolution r2 = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob,
                                                        "fx.selected.param.2");
    REQUIRE(r2.ok);
    REQUIRE(r2.canonical == "fx.selected.param.2");
    REQUIRE(r2.actionId == UiAction::Id::SceneFxParamValue);
    REQUIRE(r2.paramIndex == 2);
}

TEST_CASE("UiBindResolver: FxEditor defaults for empty bind are predictable") {
    const UiBindResolution knobDefault = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob, "");
    REQUIRE(knobDefault.ok);
    REQUIRE(knobDefault.canonical == "fx.selected.param.selected");
    REQUIRE(knobDefault.actionId == UiAction::Id::SceneFxParamValue);
    REQUIRE(knobDefault.paramIndex == -1);

    const UiBindResolution animDefault = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, "");
    REQUIRE(animDefault.ok);
    REQUIRE(animDefault.canonical == "fx.anim.current");
}

TEST_CASE("UiBindResolver: legacy fx bind forms are rejected") {
    const UiBindResolution legacy = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob, "fx.param.3");
    REQUIRE_FALSE(legacy.ok);
    REQUIRE_FALSE(legacy.error.empty());
}

TEST_CASE("UiBindResolver: FxEditor anim aliases map to canonical keys") {
    const UiBindResolution r = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, "reverb");
    REQUIRE(r.ok);
    REQUIRE(r.canonical == "fx.anim.reverb");

    const UiBindResolution h = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, "anim.hpf");
    REQUIRE(h.ok);
    REQUIRE(h.canonical == "fx.anim.hpf");
}

TEST_CASE("UiBindResolver: invalid bind returns explanatory error") {
    const UiBindResolution bad = UiBindResolver::resolve(UiScene::FxEditor, UiLayoutNodeType::Knob, "wet");
    REQUIRE_FALSE(bad.ok);
    REQUIRE_FALSE(bad.error.empty());
}

TEST_CASE("UiBindResolver: Tracks knob aliases map to canonical keys") {
    const UiBindResolution speed = UiBindResolver::resolve(UiScene::Tracks, UiLayoutNodeType::Knob, "speed");
    REQUIRE(speed.ok);
    REQUIRE(speed.canonical == "track.selected.speed");

    const UiBindResolution bpm = UiBindResolver::resolve(UiScene::Tracks, UiLayoutNodeType::Knob, "bpm");
    REQUIRE(bpm.ok);
    REQUIRE(bpm.canonical == "transport.bpm");

    const UiBindResolution looper = UiBindResolver::resolve(UiScene::Tracks, UiLayoutNodeType::Switch, "looper");
    REQUIRE(looper.ok);
    REQUIRE(looper.canonical == "track.selected.looper_mode");
}
