#include <catch2/catch_all.hpp>

#include "contracts/FxRegistry.h"

using namespace avantgarde;

TEST_CASE("FxRegistry: resolves canonical ids and aliases") {
    const FxDescriptor* reverbA = FxRegistry::find("fx.reverb.schroeder");
    const FxDescriptor* reverbB = FxRegistry::find("reverb");
    const FxDescriptor* stutterA = FxRegistry::find("fx.stutter.sync");
    const FxDescriptor* stutterB = FxRegistry::find("stutter");
    const FxDescriptor* bufferA = FxRegistry::find("fx.buffer.superglitch");
    const FxDescriptor* bufferB = FxRegistry::find("buffer");
    const FxDescriptor* superA = FxRegistry::find("fx.superglitch.v1");
    const FxDescriptor* superB = FxRegistry::find("super-glitch");
    REQUIRE(reverbA != nullptr);
    REQUIRE(reverbB != nullptr);
    REQUIRE(stutterA != nullptr);
    REQUIRE(stutterB != nullptr);
    REQUIRE(bufferA != nullptr);
    REQUIRE(bufferB != nullptr);
    REQUIRE(superA != nullptr);
    REQUIRE(superB != nullptr);
    REQUIRE(reverbA->id == FxRegistry::kReverbSchroederId);
    REQUIRE(reverbB->id == FxRegistry::kReverbSchroederId);
    REQUIRE(stutterA->id == FxRegistry::kStutterId);
    REQUIRE(stutterB->id == FxRegistry::kStutterId);
    REQUIRE(bufferA->id == FxRegistry::kBufferFxId);
    REQUIRE(bufferB->id == FxRegistry::kBufferFxId);
    REQUIRE(superA->id == FxRegistry::kSuperGlitchId);
    REQUIRE(superB->id == FxRegistry::kSuperGlitchId);
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

    const FxDescriptor& buffer = FxRegistry::findOrFallback("fx.buffer.superglitch");
    REQUIRE(buffer.paramCount == 8);
    REQUIRE(buffer.params[0].label == "Mix");
    REQUIRE(buffer.params[1].label == "Slice");
    REQUIRE(buffer.params[2].label == "Repeat");
    REQUIRE(buffer.params[3].label == "Speed");
    REQUIRE(buffer.params[6].label == "Retrig");
    REQUIRE(buffer.params[7].label == "Reverse");

    const FxDescriptor& super = FxRegistry::findOrFallback("fx.superglitch.v1");
    REQUIRE(super.paramCount == 8);
    REQUIRE(super.params[1].label == "Subslice");
    REQUIRE(super.params[4].label == "Mode");
    REQUIRE(super.params[5].label == "Phrase");
}
