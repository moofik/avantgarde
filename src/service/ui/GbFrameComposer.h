#pragma once

#include <cstdint>
#include <string>

#include "contracts/IUi.h"

namespace avantgarde {

// Service-layer frame builder for GB-style textual UI.
// Keeps formatting/business presentation logic out of renderer backends.
class GbFrameComposer {
public:
    // Build deterministic monochrome frame from runtime snapshot.
    // width is total character width including left/right frame borders.
    static std::string buildMonochromeFrame(const UiState& state, uint16_t width);
};

} // namespace avantgarde
