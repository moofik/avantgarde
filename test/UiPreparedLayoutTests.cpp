#include <catch2/catch_all.hpp>

#include "service/ui/TracksWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"
#include "service/ui/layout/UiPreparedLayoutAsciiRenderer.h"

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
            .addComponent(UiSwitchBuilder("s1").label("RETRIG").options({"OFF", "1", "2"}).selectedIndex(1).selected(false))
            .addComponent(UiSeparatorBuilder("sep").style(UiSeparatorBuilder::Style::Heavy))
            .addComponent(UiListBuilder("tracks_body").addRow("T1").addRow("T2").selectedRow(1))
    ).build();

    REQUIRE(prepared.sceneId == "tracks");
    REQUIRE(prepared.layoutTemplate == &tpl);
    REQUIRE(prepared.frameWidth == 80);
    REQUIRE(prepared.components.size() == 5);
    REQUIRE(prepared.components[0]->type() == UiComponentType::StatusBar);
    REQUIRE(prepared.components[1]->type() == UiComponentType::Knob);
    REQUIRE(prepared.components[2]->type() == UiComponentType::Switch);
    REQUIRE(prepared.components[3]->type() == UiComponentType::Separator);
    REQUIRE(prepared.components[4]->type() == UiComponentType::List);

    const auto* knob = dynamic_cast<const UiKnobComponent*>(prepared.components[1].get());
    REQUIRE(knob != nullptr);
    REQUIRE(knob->label == "VOL");
    REQUIRE(knob->selected);

    const auto* sw = dynamic_cast<const UiSwitchComponent*>(prepared.components[2].get());
    REQUIRE(sw != nullptr);
    REQUIRE(sw->options.size() == 3);
    REQUIRE(sw->selectedIndex == 1);
}

TEST_CASE("UiPreparedLayoutAsciiRenderer: root layout size overrides prepared frame hint") {
    const char* toml = R"(
id = "fixed"
[layout]
type = "column"
width = 40
height = 10

[[layout.children]]
type = "statusbar"
id = "header_title"
text = "FIXED"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    UiPreparedLayout prepared = std::move(
        UiPreparedLayoutBuilder{}
            .sceneId("fixed")
            .templateRef(&tpl)
            .frameWidth(80)
            .frameHeightHint(24)
            .addComponent(UiStatusBarBuilder("header_title").text("FIXED"))
    ).build();

    const std::vector<std::string> lines = UiPreparedLayoutAsciiRenderer::render(prepared);
    REQUIRE(lines.size() == 12U); // inner(10) + borders(2)
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

TEST_CASE("TracksWidget: page rows use vector index, not UiTrackStateView.id") {
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
    state.tracks.resize(4);
    // Имитируем рассинхрон id-поля: UI должен рисовать по фактическому индексу.
    state.tracks[0].id = 0;
    state.tracks[1].id = 2;
    state.tracks[2].id = 4;
    state.tracks[3].id = 6;
    for (std::size_t i = 0; i < state.tracks.size(); ++i) {
        state.tracks[i].state = UiTrackState::Stopped;
        state.tracks[i].clipName = "clip.wav";
    }

    UiNavState nav{};
    nav.scene = UiScene::Tracks;
    nav.selectedTrack = 1;
    nav.trackPage = 0;
    state.transport.activeTrack = 1;

    UiPreparedLayout prepared{};
    REQUIRE(widget.buildPreparedLayout(prepared, state, nav));

    const UiListComponent* list = nullptr;
    for (const auto& c : prepared.components) {
        if (!c) continue;
        if (c->id() == "tracks_body" && c->type() == UiComponentType::List) {
            list = dynamic_cast<const UiListComponent*>(c.get());
            break;
        }
    }
    REQUIRE(list != nullptr);

    bool hasT2 = false;
    bool hasT3 = false;
    bool hasT1 = false;
    for (const std::string& row : list->rows) {
        hasT2 = hasT2 || (row.find(" T2 ") != std::string::npos);
        hasT3 = hasT3 || (row.find(" T3 ") != std::string::npos);
        hasT1 = hasT1 || (row.find(" T1 ") != std::string::npos);
    }
    REQUIRE(hasT2);
    REQUIRE_FALSE(hasT1);
    REQUIRE_FALSE(hasT3);
}

TEST_CASE("TracksWidget: layout-driven renderer shows only active track page") {
    const char* toml = R"(
id = "tracks"
[layout]
type = "column"
padding = 0
gap = 0
[[layout.children]]
type = "statusbar"
id = "header_title"
text = "AVANTGARDE"
[[layout.children]]
type = "text"
id = "transport_line"
[[layout.children]]
type = "text"
id = "active_line"
[[layout.children]]
type = "separator"
id = "sep_top"
[[layout.children]]
type = "track_view"
id = "track_view"
[[layout.children.children]]
type = "list"
id = "tracks_body"
[[layout.children.children]]
type = "row"
id = "track_knobs_row"
[[layout.children.children.children]]
type = "knob"
id = "track_speed"
bind = "speed"
[[layout.children.children.children]]
type = "knob"
id = "track_gain"
bind = "gain"
[[layout.children.children]]
type = "anim_slot"
id = "track_anim"
bind = "current"
[[layout.children]]
type = "separator"
id = "sep_bottom"
[[layout.children]]
type = "text"
id = "action_status"
[[layout.children]]
type = "text"
id = "keys_hint"
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
    state.transport.quant = QuantizeMode::Bar;
    state.tracks.resize(4);
    for (std::size_t i = 0; i < state.tracks.size(); ++i) {
        state.tracks[i].id = static_cast<uint8_t>(i);
        state.tracks[i].state = UiTrackState::Stopped;
        state.tracks[i].clipName = "clip.wav";
    }

    auto hasTrackLabel = [](const std::vector<std::string>& lines, std::string_view marker) {
        for (const std::string& line : lines) {
            if (line.find(marker) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    UiNavState nav{};
    nav.scene = UiScene::Tracks;

    // Active track = 2: должен быть виден только T2.
    nav.selectedTrack = 1;
    nav.trackPage = 0;
    state.transport.activeTrack = 1;
    UiPreparedLayout preparedP1{};
    REQUIRE(widget.buildPreparedLayout(preparedP1, state, nav));
    REQUIRE(preparedP1.frameHeightHint >= 14);
    const std::vector<std::string> linesP1 = UiPreparedLayoutAsciiRenderer::render(preparedP1);
    REQUIRE(hasTrackLabel(linesP1, " T2 "));
    REQUIRE_FALSE(hasTrackLabel(linesP1, " T1 "));
    REQUIRE_FALSE(hasTrackLabel(linesP1, " T3 "));
    REQUIRE_FALSE(hasTrackLabel(linesP1, " T4 "));
    REQUIRE(hasTrackLabel(linesP1, " action:"));
    REQUIRE(hasTrackLabel(linesP1, " keys ["));

    // Active track = 4: должен быть виден только T4.
    nav.selectedTrack = 3;
    nav.trackPage = 3;
    state.transport.activeTrack = 3;
    UiPreparedLayout preparedP2{};
    REQUIRE(widget.buildPreparedLayout(preparedP2, state, nav));
    const std::vector<std::string> linesP2 = UiPreparedLayoutAsciiRenderer::render(preparedP2);
    REQUIRE(hasTrackLabel(linesP2, " T4 "));
    REQUIRE_FALSE(hasTrackLabel(linesP2, " T1 "));
    REQUIRE_FALSE(hasTrackLabel(linesP2, " T2 "));
    REQUIRE_FALSE(hasTrackLabel(linesP2, " T3 "));
}
