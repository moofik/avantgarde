#include <catch2/catch_all.hpp>

#include <algorithm>

#include "platform/render/PreparedLayoutUtils.h"
#include "service/ui/UiWidgetFactory.h"

using namespace avantgarde;

TEST_CASE("SequencerWidget: list scene opens lane scene on enter") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Sequencer);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.sequencer.patternId = 2;
    state.sequencer.lengthBars = 2;
    state.sequencer.quant = SequencerQuantize::Sixteenth;
    UiSequencerLaneView lane{};
    lane.laneId = 0;
    lane.kind = UiSequencerLaneKind::Event;
    lane.label = "SNAP (T1)";
    state.sequencer.lanes.push_back(lane);

    UiNavState nav{};
    nav.scene = UiScene::Sequencer;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));
    auto index = render::buildComponentIndex(prepared);
    REQUIRE(index.find("sequencer_lanes") != index.end());

    const WidgetOutput out = widget->onGesture(UiGesture::ListEnter, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    CHECK(out.intents[0].type == UiIntentType::OpenScene);
    CHECK(out.intents[0].scene == UiScene::SequencerLane);
}

TEST_CASE("SequencerWidget: lane scene exposes waveform and lane actions") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::SequencerLane);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.sequencer.patternId = 1;
    state.sequencer.lengthBars = 4;
    state.sequencer.lengthTicks = 1536;
    state.sequencer.quant = SequencerQuantize::Quarter;
    UiSequencerLaneView lane{};
    lane.laneId = 0;
    lane.kind = UiSequencerLaneKind::Automation;
    lane.label = "AUTO T1 S-1 P2";
    state.sequencer.lanes.push_back(lane);
    state.sequencer.activeLane = 0;

    UiSequencerPointView p0{};
    p0.objectId = 1;
    p0.tick = 0;
    p0.value = 0.2f;
    UiSequencerPointView p1{};
    p1.objectId = 2;
    p1.tick = 768;
    p1.value = 0.9f;
    state.sequencer.points.push_back(p0);
    state.sequencer.points.push_back(p1);

    UiNavState nav{};
    nav.scene = UiScene::SequencerLane;
    nav.sequencerZoom = 3;

    UiPreparedLayout prepared{};
    REQUIRE(widget->buildPreparedLayout(prepared, state, nav));
    auto index = render::buildComponentIndex(prepared);
    auto it = index.find("lane_curve");
    REQUIRE(it != index.end());
    const auto* wave = dynamic_cast<const UiWaveformComponent*>(it->second);
    REQUIRE(wave != nullptr);
    REQUIRE_FALSE(wave->peaks01.empty());
    CHECK(std::any_of(wave->peaks01.begin(), wave->peaks01.end(), [](float v) { return v > 0.5f; }));

    UiActionCatalog catalog = widget->queryAvailableActions(state, nav);
    const auto objIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneSequencerObjectSelect;
    });
    REQUIRE(objIt != catalog.actions.end());
}

TEST_CASE("PatternEditWidget: pattern actions emit sequencer intents") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::PatternEdit);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.sequencer.lengthBars = 64;
    state.sequencer.quant = SequencerQuantize::Quarter;
    state.sequencer.resetOnLoop = true;

    UiNavState nav{};
    nav.scene = UiScene::PatternEdit;

    UiActionCatalog catalog = widget->queryAvailableActions(state, nav);
    const auto lenIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneSequencerPatternLength;
    });
    REQUIRE(lenIt != catalog.actions.end());

    UiAction lenAction = *lenIt;
    lenAction.op = UiAction::Op::AdjustNext;
    const WidgetOutput lenOut = widget->onAction(lenAction, state, nav);
    REQUIRE(lenOut.handled);
    REQUIRE(lenOut.intents.size() == 1);
    CHECK(lenOut.intents[0].type == UiIntentType::SequencerSetPatternLengthBars);
    CHECK(lenOut.intents[0].value == Catch::Approx(66.0f));

    const auto quantIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneSequencerQuant;
    });
    REQUIRE(quantIt != catalog.actions.end());
    UiAction quantAction = *quantIt;
    quantAction.op = UiAction::Op::AdjustPrev;
    const WidgetOutput quantOut = widget->onAction(quantAction, state, nav);
    REQUIRE(quantOut.handled);
    REQUIRE(quantOut.intents.size() == 1);
    CHECK(quantOut.intents[0].type == UiIntentType::SequencerSetQuant);

    const auto modeIt = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const UiAction& a) {
        return a.def.id == UiAction::Id::SceneSequencerLoopMode;
    });
    REQUIRE(modeIt != catalog.actions.end());
    UiAction modeAction = *modeIt;
    modeAction.op = UiAction::Op::Apply;
    const WidgetOutput modeOut = widget->onAction(modeAction, state, nav);
    REQUIRE(modeOut.handled);
    REQUIRE(modeOut.intents.size() == 1);
    CHECK(modeOut.intents[0].type == UiIntentType::SequencerSetLoopMode);
    CHECK(modeOut.intents[0].value == Catch::Approx(0.0f));
}

TEST_CASE("SequencerWidget: delete gesture removes point in lane mode") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::SequencerLane);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;
    state.sequencer.points.resize(1);
    state.sequencer.points[0].objectId = 1;
    state.sequencer.points[0].tick = 120;
    state.sequencer.points[0].value = 0.5f;

    UiNavState nav{};
    nav.scene = UiScene::SequencerLane;

    const WidgetOutput out = widget->onGesture(UiGesture::DeleteObject, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    CHECK(out.intents[0].type == UiIntentType::SequencerDeleteSelectedObject);
}

TEST_CASE("SequencerWidget: delete gesture removes lane in list mode") {
    UiWidgetFactory factory{};
    auto widget = factory.create(UiScene::Sequencer);
    REQUIRE(widget != nullptr);

    UiState state{};
    state.sequencer.lanes.resize(1);
    state.sequencer.lanes[0].kind = UiSequencerLaneKind::Automation;

    UiNavState nav{};
    nav.scene = UiScene::Sequencer;

    const WidgetOutput out = widget->onGesture(UiGesture::DeleteObject, state, nav);
    REQUIRE(out.handled);
    REQUIRE(out.intents.size() == 1);
    CHECK(out.intents[0].type == UiIntentType::SequencerDeleteSelectedLane);
}
