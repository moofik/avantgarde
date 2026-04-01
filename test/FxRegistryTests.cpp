#include <catch2/catch_all.hpp>

#include "contracts/FxRegistry.h"

using namespace avantgarde;

TEST_CASE("FxRegistry: resolves canonical ids and aliases") {
    const FxDescriptor* reverbA = FxRegistry::find("fx.reverb.schroeder");
    const FxDescriptor* reverbB = FxRegistry::find("reverb");
    REQUIRE(reverbA != nullptr);
    REQUIRE(reverbB != nullptr);
    REQUIRE(reverbA->id == FxRegistry::kReverbSchroederId);
    REQUIRE(reverbB->id == FxRegistry::kReverbSchroederId);
}

TEST_CASE("FxRegistry: descriptors expose parameter metadata") {
    const FxDescriptor& reverb = FxRegistry::findOrFallback("fx.reverb.schroeder");
    REQUIRE(reverb.paramCount == 4);
    REQUIRE(reverb.params[0].label == "Wet");
    REQUIRE(reverb.params[3].label == "Width");
}
