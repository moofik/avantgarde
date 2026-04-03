#include <catch2/catch_all.hpp>

#include "contracts/FxRegistry.h"

using namespace avantgarde;

TEST_CASE("FxRegistry: resolves canonical ids and aliases") {
    const FxDescriptor* reverbA = FxRegistry::find("fx.reverb.schroeder");
    const FxDescriptor* reverbB = FxRegistry::find("reverb");
    const FxDescriptor* stutterA = FxRegistry::find("fx.stutter.sync");
    const FxDescriptor* stutterB = FxRegistry::find("stutter");
    REQUIRE(reverbA != nullptr);
    REQUIRE(reverbB != nullptr);
    REQUIRE(stutterA != nullptr);
    REQUIRE(stutterB != nullptr);
    REQUIRE(reverbA->id == FxRegistry::kReverbSchroederId);
    REQUIRE(reverbB->id == FxRegistry::kReverbSchroederId);
    REQUIRE(stutterA->id == FxRegistry::kStutterId);
    REQUIRE(stutterB->id == FxRegistry::kStutterId);
}

TEST_CASE("FxRegistry: descriptors expose parameter metadata") {
    const FxDescriptor& reverb = FxRegistry::findOrFallback("fx.reverb.schroeder");
    REQUIRE(reverb.paramCount == 4);
    REQUIRE(reverb.params[0].label == "Wet");
    REQUIRE(reverb.params[3].label == "Width");

    const FxDescriptor& stutter = FxRegistry::findOrFallback("fx.stutter.sync");
    REQUIRE(stutter.paramCount == 4);
    REQUIRE(stutter.params[0].label == "Wet");
    REQUIRE(stutter.params[1].label == "Rate");
    REQUIRE(stutter.params[2].label == "Gate");
    REQUIRE(stutter.params[3].label == "Retrig");
}
