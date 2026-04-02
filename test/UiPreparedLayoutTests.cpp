#include <catch2/catch_all.hpp>

#include "service/ui/TracksWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"

using namespace avantgarde;

TEST_CASE("UiPreparedLayoutBuilder: builds typed component pack") {
    UiLayoutTemplate tpl{};
    tpl.widgetId = "tracks";

    UiPreparedLayout prepared = std::move(
        UiPreparedLayoutBuilder{}
            .sceneId("tracks")
            .templateRef(&tpl)
            .frameWidth(80)
            .frameHeightHint(24)
            .addComponent(UiStatusBarBuilder("header_title").text("AVANTGARDE"))
            .addComponent(UiKnobBuilder("k1").label("VOL").value01(0.95f).selected(true))
            .addComponent(UiSeparatorBuilder("sep").style(UiSeparatorBuilder::Style::Heavy))
            .addComponent(UiListBuilder("tracks_body").addRow("T1").addRow("T2").selectedRow(1))
    ).build();

    REQUIRE(prepared.sceneId == "tracks");
    REQUIRE(prepared.layoutTemplate == &tpl);
    REQUIRE(prepared.frameWidth == 80);
    REQUIRE(prepared.components.size() == 4);
    REQUIRE(prepared.components[0]->type() == UiComponentType::StatusBar);
    REQUIRE(prepared.components[1]->type() == UiComponentType::Knob);
    REQUIRE(prepared.components[2]->type() == UiComponentType::Separator);
    REQUIRE(prepared.components[3]->type() == UiComponentType::List);

    const auto* knob = dynamic_cast<const UiKnobComponent*>(prepared.components[1].get());
    REQUIRE(knob != nullptr);
    REQUIRE(knob->label == "VOL");
    REQUIRE(knob->selected);
}

TEST_CASE("TracksWidget: provides prepared layout without renderer coupling") {
    const char* toml = R"(
id = "tracks"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
id = "header_title"
text = "MAIN"
[[layout.children]]
type = "spacer"
id = "tracks_body"
[[layout.children]]
type = "text"
id = "action_status"
[[layout.children]]
type = "text"
id = "keys_hint"
text = " keys test "
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    TracksWidget widget(TracksWidget::Options{
        .frameWidth = 64,
        .headerTitle = "AVANTGARDE",
        .speedStep = 0.05f,
        .bpmStep = 1.0f,
        .layoutTemplate = tpl,
    });

    UiState state{};
    state.transport.bpm = 120.0f;
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].state = UiTrackState::Stopped;
    state.tracks[0].clipName = "loop.wav";

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 0;

    UiPreparedLayout prepared{};
    REQUIRE(widget.buildPreparedLayout(prepared, state, nav));
    REQUIRE(prepared.sceneId == "tracks");
    REQUIRE(prepared.layoutTemplate != nullptr);
    REQUIRE_FALSE(prepared.components.empty());

    bool hasHeader = false;
    bool hasBody = false;
    for (const auto& c : prepared.components) {
        if (!c) continue;
        if (c->id() == "header_title" && c->type() == UiComponentType::StatusBar) {
            hasHeader = true;
        }
        if (c->id() == "tracks_body" && c->type() == UiComponentType::List) {
            hasBody = true;
        }
    }
    REQUIRE(hasHeader);
    REQUIRE(hasBody);
}
