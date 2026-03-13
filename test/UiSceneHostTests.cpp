#include <catch2/catch_all.hpp>

#include "service/ui/UiSceneHost.h"

using namespace avantgarde;

namespace {

class FakeWidget final : public IUiWidget {
public:
    const char* id() const noexcept override { return "fake"; }

    void render(UiTextBuffer& out, const UiState&, const UiNavState& navState) override {
        out.lines.push_back(navState.selectedTrack == 0 ? "track0" : "track1");
    }

    WidgetOutput onInput(UiInputAction action, const UiState&, UiNavState& navState) override {
        if (action == UiInputAction::BpmUp) {
            navState.cursor = static_cast<uint16_t>(navState.cursor + 1);
            return WidgetOutput{true, {}};
        }
        return {};
    }
};

} // namespace

TEST_CASE("UiSceneHost: registers widget and renders active scene") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};
    UiTextBuffer out{};
    REQUIRE(host.renderActive(out, state));
    REQUIRE(out.lines.size() == 1);
    REQUIRE(out.lines[0] == "track0");
}

TEST_CASE("UiSceneHost: handles global track selection and delegates local input") {
    UiSceneHost host;
    REQUIRE(host.registerWidget(UiScene::Tracks, std::make_unique<FakeWidget>()));

    UiState state{};

    const WidgetOutput globalOut = host.handleInput(UiInputAction::SelectTrack1, state);
    REQUIRE(globalOut.handled);
    REQUIRE(host.nav().selectedTrack == 1);

    const WidgetOutput localOut = host.handleInput(UiInputAction::BpmUp, state);
    REQUIRE(localOut.handled);
    REQUIRE(host.nav().cursor == 1);
}

