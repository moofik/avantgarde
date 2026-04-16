#pragma once

#import <AppKit/AppKit.h>

#include "contracts/IUiGestureInput.h"

namespace avantgarde::macos {

// Маппинг macOS key events -> сырые PrimitiveInputEvent.
// Важно: здесь нет scene-aware логики и нет распознавания hold/tap.
PrimitiveInputEvent mapPrimitiveWindowEvent(NSEvent* event) noexcept;

} // namespace avantgarde::macos
