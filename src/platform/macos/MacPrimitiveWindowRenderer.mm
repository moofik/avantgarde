#include "platform/macos/MacPrimitiveWindowRenderer.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#include <filesystem>
#include <string>
#include <unordered_map>

#include "platform/macos/MacPrimitiveScenePainter.h"

namespace avantgarde {
namespace {

NSColor* colorFromRgb(uint8_t r, uint8_t g, uint8_t b) {
    return [NSColor colorWithSRGBRed:static_cast<CGFloat>(r) / 255.0
                               green:static_cast<CGFloat>(g) / 255.0
                                blue:static_cast<CGFloat>(b) / 255.0
                               alpha:1.0];
}

bool isFontExtension(NSString* ext) {
    if (!ext) {
        return false;
    }
    const NSString* lower = [ext lowercaseString];
    return [lower isEqualToString:@"ttf"] || [lower isEqualToString:@"otf"] ||
           [lower isEqualToString:@"ttc"] || [lower isEqualToString:@"otc"];
}

NSArray<NSString*>* discoverAssetFontPostScriptNames() {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* cwd = [fm currentDirectoryPath];
    NSString* fontDir = [cwd stringByAppendingPathComponent:@"assets/fonts"];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:fontDir isDirectory:&isDir] || !isDir) {
        return @[];
    }

    NSError* error = nil;
    NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:fontDir error:&error];
    if (!entries || error) {
        return @[];
    }

    NSMutableArray<NSString*>* names = [NSMutableArray array];
    for (NSString* entry : entries) {
        if (!isFontExtension([entry pathExtension])) {
            continue;
        }
        NSString* fullPath = [fontDir stringByAppendingPathComponent:entry];
        NSURL* url = [NSURL fileURLWithPath:fullPath];

        CFErrorRef regErr = nullptr;
        (void)CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url, kCTFontManagerScopeProcess, &regErr);
        if (regErr) {
            CFRelease(regErr);
        }

        CGDataProviderRef provider = CGDataProviderCreateWithURL((__bridge CFURLRef)url);
        if (!provider) {
            continue;
        }
        CGFontRef cgFont = CGFontCreateWithDataProvider(provider);
        CGDataProviderRelease(provider);
        if (!cgFont) {
            continue;
        }
        CFStringRef psName = CGFontCopyPostScriptName(cgFont);
        CGFontRelease(cgFont);
        if (!psName) {
            continue;
        }
        [names addObject:(__bridge NSString*)psName];
        CFRelease(psName);
    }
    return names;
}

NSFont* pickFirstFont(NSArray<NSString*>* names, CGFloat size, NSFont* fallback) {
    for (NSString* name : names) {
        NSFont* f = [NSFont fontWithName:name size:size];
        if (f) {
            return f;
        }
    }
    return fallback;
}

} // namespace

struct MacPrimitiveWindowRenderer::Impl {
    // Окно и canvas-слой процедурного рендера.
    NSWindow* window{nil};
    NSView* canvas{nil};

    // Шрифты: body моно + готический для лейблов/заголовков.
    NSFont* bodyFont{nil};
    NSFont* gothicFont{nil};

    // Палитра.
    NSColor* bg{nil};
    NSColor* panel{nil};
    NSColor* mid{nil};
    NSColor* text{nil};

    // Параметры "виртуальной сетки" (символьные координаты -> пиксели).
    CGFloat cellW{12.0};
    CGFloat cellH{18.0};
    CGFloat margin{14.0};

    // Runtime-контекст рендера.
    std::string cwd{};
    std::unordered_map<std::string, NSFont*> dynamicFontCache{};
    VisualFxProcessor visualFx{};
};

MacPrimitiveWindowRenderer::MacPrimitiveWindowRenderer(UiTheme theme)
    : impl_(new Impl()) {
    impl_->cwd = std::filesystem::current_path().string();

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    // Базовый таргет под компактный экран устройства: 640x480.
    const NSRect frame = NSMakeRect(120.0, 120.0, 640.0, 480.0);
    impl_->window = [[NSWindow alloc] initWithContentRect:frame
                                                 styleMask:(NSWindowStyleMaskTitled |
                                                            NSWindowStyleMaskClosable |
                                                            NSWindowStyleMaskMiniaturizable |
                                                            NSWindowStyleMaskResizable)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    [impl_->window setTitle:@"Avantgarde // Primitive UI Preview"];
    [impl_->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    impl_->canvas = [[NSView alloc] initWithFrame:[[impl_->window contentView] bounds]];
    [impl_->canvas setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [impl_->canvas setWantsLayer:YES];

    const UiThemePalette palette = uiThemePalette(theme);
    impl_->bg = colorFromRgb(palette.bg.r, palette.bg.g, palette.bg.b);
    impl_->panel = colorFromRgb(palette.panel.r, palette.panel.g, palette.panel.b);
    impl_->mid = colorFromRgb(palette.mid.r, palette.mid.g, palette.mid.b);
    impl_->text = colorFromRgb(palette.text.r, palette.text.g, palette.text.b);

    [[impl_->canvas layer] setBackgroundColor:impl_->bg.CGColor];
    [[impl_->window contentView] addSubview:impl_->canvas];

    NSArray<NSString*>* assetFonts = discoverAssetFontPostScriptNames();
    impl_->bodyFont = pickFirstFont(
        @[ @"PressStart2P-Regular", @"Press Start 2P", @"Silkscreen-Regular", @"Menlo" ],
        11.0,
        [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular]);
    impl_->gothicFont = pickFirstFont(
        assetFonts,
        12.0,
        pickFirstFont(@[ @"UnifrakturCook-Bold", @"UnifrakturCook-Regular", @"Old English Text MT" ],
                      12.0,
                      [NSFont boldSystemFontOfSize:12.0]));

}

MacPrimitiveWindowRenderer::~MacPrimitiveWindowRenderer() {
    if (!impl_) {
        return;
    }
    if (impl_->window) {
        [impl_->window orderOut:nil];
        [impl_->window close];
        impl_->window = nil;
    }
    impl_->canvas = nil;
    delete impl_;
    impl_ = nullptr;
}

void MacPrimitiveWindowRenderer::render(const UiState& state) {
    (void)state;
}

void MacPrimitiveWindowRenderer::renderPreparedLayout(const UiPreparedLayout& prepared) {
    if (!impl_) {
        return;
    }
    macos::MacPrimitiveScenePaintContext ctx{};
    ctx.canvas = impl_->canvas;
    ctx.bodyFont = impl_->bodyFont;
    ctx.gothicFont = impl_->gothicFont;
    ctx.panel = impl_->panel;
    ctx.mid = impl_->mid;
    ctx.text = impl_->text;
    ctx.cellW = impl_->cellW;
    ctx.cellH = impl_->cellH;
    ctx.margin = impl_->margin;
    ctx.cwd = impl_->cwd;
    ctx.dynamicFontCache = &impl_->dynamicFontCache;
    ctx.visualFx = &impl_->visualFx;
    macos::renderPreparedLayoutScene(ctx, prepared);
}

void MacPrimitiveWindowRenderer::pumpEvents() noexcept {
    for (;;) {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (!event) {
            break;
        }
        [NSApp sendEvent:event];
    }
    [NSApp updateWindows];
}

} // namespace avantgarde
