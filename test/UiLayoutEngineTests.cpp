#include <catch2/catch_all.hpp>

#include "service/ui/UiLayoutTomlLoader.h"
#include "service/ui/layout/UiLayoutEngine.h"

using namespace avantgarde;

TEST_CASE("UiLayoutEngine: arranges column with auto body area") {
    const char* toml = R"(
id = "test"
[layout]
type = "column"
padding = 0
gap = 0

[[layout.children]]
type = "statusbar"
id = "title"
width = "100%"

[[layout.children]]
type = "spacer"
id = "body"
height = "auto"
width = "100%"

[[layout.children]]
type = "text"
id = "footer"
width = "100%"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    const UiLayoutEngine::Result r = UiLayoutEngine::arrange(tpl.root, 60, 20);
    const UiLayoutBox* title = UiLayoutEngine::findById(r, "title");
    const UiLayoutBox* body = UiLayoutEngine::findById(r, "body");
    const UiLayoutBox* footer = UiLayoutEngine::findById(r, "footer");
    REQUIRE(title != nullptr);
    REQUIRE(body != nullptr);
    REQUIRE(footer != nullptr);

    REQUIRE(title->rect.y == 0);
    REQUIRE(title->rect.height == 1);
    REQUIRE(footer->rect.y == 19);
    REQUIRE(footer->rect.height == 1);
    REQUIRE(body->rect.y == 1);
    REQUIRE(body->rect.height == 18);
}

TEST_CASE("UiLayoutEngine: supports row percent split") {
    const char* toml = R"(
id = "row_test"
[layout]
type = "row"
padding = 0
gap = 0

[[layout.children]]
type = "spacer"
id = "left"
width = "65%"
height = "100%"

[[layout.children]]
type = "spacer"
id = "right"
width = "35%"
height = "100%"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    const UiLayoutEngine::Result r = UiLayoutEngine::arrange(tpl.root, 100, 10);
    const UiLayoutBox* left = UiLayoutEngine::findById(r, "left");
    const UiLayoutBox* right = UiLayoutEngine::findById(r, "right");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);

    REQUIRE(left->rect.width == 65);
    REQUIRE(right->rect.width == 35);
    REQUIRE(left->rect.y == 0);
    REQUIRE(right->rect.y == 0);
    REQUIRE(right->rect.x == 65);
}

TEST_CASE("UiLayoutEngine: wraps row children when they do not fit width") {
    const char* toml = R"(
id = "wrap_row"
[layout]
type = "row"
padding = 0
gap = 1

[[layout.children]]
type = "knob"
id = "k1"

[[layout.children]]
type = "knob"
id = "k2"

[[layout.children]]
type = "knob"
id = "k3"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    const UiLayoutEngine::Result r = UiLayoutEngine::arrange(tpl.root, 40, 10);
    const UiLayoutBox* k1 = UiLayoutEngine::findById(r, "k1");
    const UiLayoutBox* k2 = UiLayoutEngine::findById(r, "k2");
    const UiLayoutBox* k3 = UiLayoutEngine::findById(r, "k3");
    REQUIRE(k1 != nullptr);
    REQUIRE(k2 != nullptr);
    REQUIRE(k3 != nullptr);

    REQUIRE(k1->rect.y == 0);
    REQUIRE(k2->rect.y == 0);
    REQUIRE(k3->rect.y > k2->rect.y);
}

TEST_CASE("UiLayoutEngine: row grid with percent width wraps to next line") {
    const char* toml = R"(
id = "grid_row"
[layout]
type = "row"
padding = 0
gap = 2

[[layout.children]]
type = "knob"
id = "k1"
width = "49%"

[[layout.children]]
type = "knob"
id = "k2"
width = "49%"

[[layout.children]]
type = "knob"
id = "k3"
width = "49%"

[[layout.children]]
type = "knob"
id = "k4"
width = "49%"
)";

    UiLayoutTemplate tpl{};
    std::string err{};
    REQUIRE(UiLayoutTomlLoader::loadFromString(toml, tpl, err));

    const UiLayoutEngine::Result r = UiLayoutEngine::arrange(tpl.root, 60, 12);
    const UiLayoutBox* k1 = UiLayoutEngine::findById(r, "k1");
    const UiLayoutBox* k2 = UiLayoutEngine::findById(r, "k2");
    const UiLayoutBox* k3 = UiLayoutEngine::findById(r, "k3");
    const UiLayoutBox* k4 = UiLayoutEngine::findById(r, "k4");
    REQUIRE(k1 != nullptr);
    REQUIRE(k2 != nullptr);
    REQUIRE(k3 != nullptr);
    REQUIRE(k4 != nullptr);

    REQUIRE(k1->rect.y == k2->rect.y);
    REQUIRE(k3->rect.y == k4->rect.y);
    REQUIRE(k3->rect.y > k1->rect.y);
}
