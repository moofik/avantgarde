#include <catch2/catch_all.hpp>

#include "service/ui/FxListWidget.h"
#include "service/ui/ManagerWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"

using namespace avantgarde;

TEST_CASE("ManagerWidget: applies TOML title and keys hint") {
    const char* toml = R"(
id = "manager"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
text = "FILE MANAGER"
[[layout.children]]
type = "text"
id = "keys_hint"
text = " keys [MANAGER TEST] "
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    ManagerWidget widget(60, tpl);
    UiState state{};
    UiNavState nav{};
    nav.scene = UiScene::Manager;
    nav.selectedTrack = 0;
    nav.managerCwd = ".";

    UiTextBuffer out{};
    widget.render(out, state, nav);
    REQUIRE_FALSE(out.lines.empty());

    bool hasTitle = false;
    bool hasKeys = false;
    for (const std::string& line : out.lines) {
        if (line.find("FILE MANAGER") != std::string::npos) {
            hasTitle = true;
        }
        if (line.find("MANAGER TEST") != std::string::npos) {
            hasKeys = true;
        }
    }
    REQUIRE(hasTitle);
    REQUIRE(hasKeys);
}

TEST_CASE("FxListWidget: applies TOML title and keys hint") {
    const char* toml = R"(
id = "fx_list"
[layout]
type = "column"
[[layout.children]]
type = "statusbar"
text = "FX CHAIN"
[[layout.children]]
type = "text"
id = "keys_hint"
text = " keys [FXLIST TEST] "
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    FxListWidget widget(60, tpl);
    UiState state{};
    state.tracks.resize(1);
    state.tracks[0].id = 0;
    state.tracks[0].fxCount = 1;
    state.tracks[0].fxChainIds.push_back("fx.reverb.schroeder");

    UiNavState nav{};
    nav.scene = UiScene::FxList;
    nav.selectedTrack = 0;
    nav.selectedFx = 0;

    UiTextBuffer out{};
    widget.render(out, state, nav);
    REQUIRE_FALSE(out.lines.empty());

    bool hasTitle = false;
    bool hasKeys = false;
    for (const std::string& line : out.lines) {
        if (line.find("FX CHAIN") != std::string::npos) {
            hasTitle = true;
        }
        if (line.find("FXLIST TEST") != std::string::npos) {
            hasKeys = true;
        }
    }
    REQUIRE(hasTitle);
    REQUIRE(hasKeys);
}

