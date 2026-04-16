#include <catch2/catch_all.hpp>

#include "service/ui/layout/UiPreparedParams.h"

using namespace avantgarde;

TEST_CASE("UiPreparedParams: findNumber resolves underscore/dot key variants") {
    UiPreparedParams p{};
    p.number["track.selected.tempo_sync"] = 1.0f;
    p.number["track.selected.speed"] = 0.75f;

    REQUIRE(p.findNumber("track.selected.tempo_sync").has_value());
    REQUIRE(*p.findNumber("track.selected.tempo_sync") == Catch::Approx(1.0f));

    // Совместимость: bind-нормализатор мог превратить "_" в ".".
    REQUIRE(p.findNumber("track.selected.tempo.sync").has_value());
    REQUIRE(*p.findNumber("track.selected.tempo.sync") == Catch::Approx(1.0f));

    // И обратная совместимость (если в map оказался dotted-ключ).
    REQUIRE(p.findNumber("track.selected.speed").has_value());
    REQUIRE(*p.findNumber("track.selected.speed") == Catch::Approx(0.75f));
    REQUIRE(p.findNumber("track_selected_speed").has_value());
    REQUIRE(*p.findNumber("track_selected_speed") == Catch::Approx(0.75f));
}

TEST_CASE("UiPreparedParams: findFlag resolves underscore/dot key variants") {
    UiPreparedParams p{};
    p.flag["track.selected.tempo_sync.selected"] = true;
    p.flag["track.selected.mode.selected"] = false;

    REQUIRE(p.findFlag("track.selected.tempo_sync.selected").has_value());
    REQUIRE(*p.findFlag("track.selected.tempo_sync.selected"));

    REQUIRE(p.findFlag("track.selected.tempo.sync.selected").has_value());
    REQUIRE(*p.findFlag("track.selected.tempo.sync.selected"));

    REQUIRE(p.findFlag("track_selected_mode_selected").has_value());
    REQUIRE(!*p.findFlag("track_selected_mode_selected"));
}
