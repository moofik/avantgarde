#include <catch2/catch_all.hpp>

#include "service/ui/UiBindNormalizer.h"
#include "service/ui/UiBindParser.h"
#include "service/ui/UiBindRegistry.h"

using namespace avantgarde;

TEST_CASE("UiBindNormalizer: trims/lowers and normalizes separators") {
    const std::string normalized = UiBindNormalizer::normalize("  Track_Selected-Speed  ");
    REQUIRE(normalized == "track.selected.speed");
}

TEST_CASE("UiBindParser: parses fx.selected.param.<index>") {
    const UiBindParsed parsed = UiBindParser::parse("fx.selected.param.5");
    REQUIRE(parsed.ok);
    REQUIRE(parsed.ns == "fx");
    REQUIRE(parsed.actionId == UiAction::Id::SceneFxParamValue);
    REQUIRE(parsed.paramIndex == 5);
}

TEST_CASE("UiBindRegistry: validates namespace-first canonical keys") {
    const UiBindRegistry& registry = UiBindRegistry::instance();

    std::string error{};
    REQUIRE(registry.isCanonicalSupported("track.selected.speed", error));

    error.clear();
    REQUIRE_FALSE(registry.isCanonicalSupported("unknown.foo", error));
    REQUIRE_FALSE(error.empty());
}
