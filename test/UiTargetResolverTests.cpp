#include <catch2/catch_all.hpp>

#include "service/ui/UiTargetResolver.h"

using namespace avantgarde;

TEST_CASE("UiTargetResolver: tracks speed fallback from bind") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::Tracks,
        UiLayoutNodeType::Knob,
        "",
        "track.selected.speed");
    REQUIRE(r.ok);
    REQUIRE(r.kind == UiTargetResolution::Kind::TrackSpeed);
    REQUIRE(r.canonical == "param.track.selected.speed");
}

TEST_CASE("UiTargetResolver: tracks gain target is supported") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::Tracks,
        UiLayoutNodeType::Knob,
        "param.track.selected.gain",
        "track.selected.gain");
    REQUIRE(r.ok);
    REQUIRE(r.kind == UiTargetResolution::Kind::TrackGain);
    REQUIRE(r.canonical == "param.track.selected.gain");
}

TEST_CASE("UiTargetResolver: tracks mute alias maps to canonical target") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::Tracks,
        UiLayoutNodeType::Switch,
        "track.selected.muted",
        "track.selected.mute");
    REQUIRE(r.ok);
    REQUIRE(r.kind == UiTargetResolution::Kind::TrackMute);
    REQUIRE(r.canonical == "param.track.selected.mute");
}

TEST_CASE("UiTargetResolver: fx bind fallback maps to param.fx.selected") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::FxEditor,
        UiLayoutNodeType::Knob,
        "",
        "fx.selected.param.2");
    REQUIRE(r.ok);
    REQUIRE(r.kind == UiTargetResolution::Kind::FxParam);
    REQUIRE(r.canonical == "param.fx.selected.2");
    REQUIRE(r.paramIndex == 2);
}

TEST_CASE("UiTargetResolver: explicit fx selected target is supported") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::FxEditor,
        UiLayoutNodeType::Switch,
        "param.fx.selected.selected",
        "fx.selected.param.0");
    REQUIRE(r.ok);
    REQUIRE(r.kind == UiTargetResolution::Kind::FxParam);
    REQUIRE(r.canonical == "param.fx.selected.selected");
    REQUIRE(r.paramIndex == -1);
}

TEST_CASE("UiTargetResolver: unsupported target in strict context fails") {
    const UiTargetResolution r = UiTargetResolver::resolve(
        UiScene::Tracks,
        UiLayoutNodeType::Knob,
        "param.unknown.anything",
        "track.selected.speed");
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
}
