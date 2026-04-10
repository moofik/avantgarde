#include <catch2/catch_all.hpp>

#include "service/ui/UiCapabilityService.h"

using namespace avantgarde;

TEST_CASE("UiCapabilityService: selected track/fx flags") {
    UiState state{};
    UiNavState nav{};
    nav.selectedTrack = 0;
    nav.selectedFx = 0;

    REQUIRE_FALSE(UiCapabilityService::hasSelectedTrack(state, nav));
    REQUIRE_FALSE(UiCapabilityService::hasSelectedFx(state, nav));

    UiTrackStateView track{};
    track.id = 0;
    track.fxCount = 1;
    state.tracks.push_back(track);

    REQUIRE(UiCapabilityService::hasSelectedTrack(state, nav));
    REQUIRE(UiCapabilityService::hasSelectedFx(state, nav));
}

TEST_CASE("UiCapabilityService: track target inactive without selected track") {
    UiState state{};
    UiNavState nav{};

    UiCapabilityState cap = UiCapabilityService::resolve(
        UiScene::Tracks,
        "track.selected.gain",
        "param.track.selected.gain",
        state,
        nav);
    REQUIRE(cap.bindSupported);
    REQUIRE_FALSE(cap.bindAvailable);
    REQUIRE(cap.targetSupported);
    REQUIRE_FALSE(cap.targetActive);
}

TEST_CASE("UiCapabilityService: fx target active only when selected FX exists") {
    UiState state{};
    UiTrackStateView track{};
    track.id = 0;
    track.fxCount = 1;
    state.tracks.push_back(track);

    UiNavState nav{};
    nav.selectedTrack = 0;
    nav.selectedFx = 0;

    UiCapabilityState cap = UiCapabilityService::resolve(
        UiScene::FxEditor,
        "fx.selected.param.selected",
        "param.fx.selected.selected",
        state,
        nav);
    REQUIRE(cap.bindSupported);
    REQUIRE(cap.bindAvailable);
    REQUIRE(cap.targetSupported);
    REQUIRE(cap.targetActive);

    nav.selectedFx = 1; // вне диапазона fxCount=1
    cap = UiCapabilityService::resolve(
        UiScene::FxEditor,
        "fx.selected.param.selected",
        "param.fx.selected.selected",
        state,
        nav);
    REQUIRE_FALSE(cap.bindAvailable);
    REQUIRE_FALSE(cap.targetActive);
}

TEST_CASE("UiCapabilityService: evaluateCondition supports visible_if/state-if semantics") {
    UiState state{};
    UiNavState nav{};
    UiPreparedParams params{};
    UiLayoutNode node{};
    node.id = "track_gain";
    node.bind = "track.selected.gain";
    node.target = "param.track.selected.gain";

    REQUIRE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "", state, nav, params, true));
    REQUIRE_FALSE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "track.selected.exists", state, nav, params, true));
    REQUIRE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "!track.selected.exists", state, nav, params, true));
    REQUIRE_FALSE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "target.active", state, nav, params, true));

    UiTrackStateView tr{};
    tr.id = 0;
    state.tracks.push_back(tr);
    REQUIRE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "track.selected.exists", state, nav, params, true));
    REQUIRE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "target.active", state, nav, params, true));

    params.flag["custom_condition"] = false;
    REQUIRE_FALSE(UiCapabilityService::evaluateCondition(
        UiScene::Tracks, node, "custom_condition", state, nav, params, true));
}
