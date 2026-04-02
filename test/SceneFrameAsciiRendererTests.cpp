#include <catch2/catch_all.hpp>

#include "service/ui/layout/SceneFrameAsciiRenderer.h"

using namespace avantgarde;

TEST_CASE("SceneFrameAsciiRenderer: draws rect, line and text") {
    SceneFrame frame{};
    frame.width = 24;
    frame.height = 8;

    frame.rects.push_back(SceneFrameRect{
        .x = 0,
        .y = 0,
        .width = 24,
        .height = 8,
    });
    frame.hlines.push_back(SceneFrameHLine{
        .x = 1,
        .y = 3,
        .length = 22,
        .glyph = "─",
    });
    frame.texts.push_back(SceneFrameText{
        .x = 2,
        .y = 1,
        .text = "SCENE FRAME",
    });

    const std::vector<std::string> lines = SceneFrameAsciiRenderer::render(frame);
    REQUIRE(lines.size() == 8);
    REQUIRE(lines[0].find("╔") == 0);
    REQUIRE(lines[1].find("SCENE FRAME") != std::string::npos);
    REQUIRE(lines[3].find("─") != std::string::npos);
}

