#pragma once

#import <AppKit/AppKit.h>

#include "contracts/IUiGestureInput.h"

namespace avantgarde::macos {

// Маппинг macOS key events -> унифицированные UiGestureEvent.
UiGestureEvent mapPrimitiveWindowEvent(NSEvent* event) noexcept;

} // namespace avantgarde::macos
