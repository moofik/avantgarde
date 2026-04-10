#include <catch2/catch_all.hpp>

#include "service/ui/UiWidgetFactory.h"

using namespace avantgarde;

TEST_CASE("UiWidgetFactory: default options load layouts from assets/ui/layouts") {
    UiWidgetFactory factory{};

    REQUIRE_NOTHROW(factory.create(UiScene::Tracks));
    REQUIRE_NOTHROW(factory.create(UiScene::TrackContext));
    REQUIRE_NOTHROW(factory.create(UiScene::SampleEdit));
    REQUIRE_NOTHROW(factory.create(UiScene::Manager));
    REQUIRE_NOTHROW(factory.create(UiScene::FxList));
    REQUIRE_NOTHROW(factory.create(UiScene::FxEditor));
}

