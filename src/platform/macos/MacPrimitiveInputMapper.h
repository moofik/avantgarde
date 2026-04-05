#pragma once

#import <AppKit/AppKit.h>

#include "contracts/IUiGestureInput.h"

namespace avantgarde::macos {

// Маппинг macOS key events -> унифицированные UiGesture.
UiGesture mapPrimitiveWindowEvent(NSEvent* event) noexcept;

} // namespace avantgarde::macos

