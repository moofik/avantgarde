#pragma once

#import <AppKit/AppKit.h>

#include <string>
#include <unordered_map>

#include "contracts/UiPreparedLayout.h"
#include "platform/render/VisualFxProcessor.h"

namespace avantgarde::macos {

// Контекст платформенного painter-а для primitive UI.
// Хранит только зависимости, нужные для отрисовки текущего кадра.
struct MacPrimitiveScenePaintContext {
    NSView* canvas{nil};

    NSFont* bodyFont{nil};
    NSFont* gothicFont{nil};

    NSColor* panel{nil};
    NSColor* mid{nil};
    NSColor* text{nil};

    CGFloat cellW{12.0};
    CGFloat cellH{18.0};
    CGFloat margin{14.0};

    std::string cwd{};
    std::unordered_map<std::string, NSFont*>* dynamicFontCache{nullptr};
    VisualFxProcessor* visualFx{nullptr};
};

// Полная отрисовка prepared-layout кадра в canvas.
void renderPreparedLayoutScene(const MacPrimitiveScenePaintContext& ctx,
                               const UiPreparedLayout& prepared);

} // namespace avantgarde::macos

