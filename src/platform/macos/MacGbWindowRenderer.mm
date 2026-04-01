#include "platform/macos/MacGbWindowRenderer.h"
#include "service/ui/GbFrameComposer.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

#include <cstdio>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>

namespace avantgarde {
namespace {

// Ручной micro-bias для позиционирования overlay-заголовка относительно monospace-сетки.
// Держим константами, чтобы правка была локальной и предсказуемой при смене шрифта.
constexpr CGFloat kHeaderOverlayXBias = 0.0;
constexpr CGFloat kHeaderOverlayYBias = 0.0;

// Утилита преобразования 8-bit RGB в NSColor.
NSColor* colorFromRgb(uint8_t r, uint8_t g, uint8_t b) {
    return [NSColor colorWithSRGBRed:static_cast<CGFloat>(r) / 255.0
                               green:static_cast<CGFloat>(g) / 255.0
                                blue:static_cast<CGFloat>(b) / 255.0
                               alpha:1.0];
}

// Проверяем расширение файла на допустимые font-контейнеры.
bool isFontExtension(NSString* ext) {
    if (!ext) {
        return false;
    }
    const NSString* lower = [ext lowercaseString];
    return [lower isEqualToString:@"ttf"] || [lower isEqualToString:@"otf"] ||
           [lower isEqualToString:@"ttc"] || [lower isEqualToString:@"otc"];
}

// Находит шрифты в assets/fonts, регистрирует их в процессе и возвращает PostScript names.
// Это позволяет пользователю подкидывать кастомные font-файлы без перекомпиляции.
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
    for (NSString* entry in entries) {
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
        NSString* psNameObj = (__bridge NSString*)psName;
        [names addObject:psNameObj];
        CFRelease(psName);
    }
    return names;
}

// Берет первый доступный шрифт из списка кандидатов.
NSFont* pickFirstFont(NSArray<NSString*>* names, CGFloat size, NSFont* fallback) {
    for (NSString* name in names) {
        NSFont* f = [NSFont fontWithName:name size:size];
        if (f) {
            return f;
        }
    }
    return fallback;
}

// Берет первый фиксированной ширины (моноширинный) шрифт.
NSFont* pickFixedPitchFont(NSArray<NSString*>* names, CGFloat size) {
    for (NSString* name in names) {
        NSFont* f = [NSFont fontWithName:name size:size];
        if (f && [f isFixedPitch]) {
            return f;
        }
    }
    return nil;
}

// Ширина одного глифа в заданном шрифте.
CGFloat glyphWidth(NSFont* font, NSString* glyph) {
    if (!font || !glyph || [glyph length] == 0) {
        return 0.0;
    }
    return [glyph sizeWithAttributes:@{ NSFontAttributeName: font }].width;
}

// Проверка, что "рамочные" символы имеют стабильную ширину.
// Если метрики нестабильны, рамки визуально "плывут".
bool hasStableFrameMetrics(NSFont* font) {
    if (!font) {
        return false;
    }
    const CGFloat ref = glyphWidth(font, @"M");
    if (ref <= 0.0) {
        return false;
    }
    NSArray<NSString*>* probes = @[
        @" ", @"A", @"0", @"|", @"-",
        @"═", @"║", @"╔", @"╗", @"╚", @"╝", @"╠", @"╣", @"╟", @"╢",
        @"─", @"│", @"┌", @"┐", @"└", @"┘", @"├", @"┤"
    ];
    for (NSString* glyph in probes) {
        const CGFloat w = glyphWidth(font, glyph);
        if (w <= 0.0) {
            return false;
        }
        if (std::fabs(w - ref) > 0.2) {
            return false;
        }
    }
    return true;
}

// Таблица соответствия keyCode -> UiInputAction для macOS окна.
UiInputAction mapWindowKeyCode(unsigned short keyCode) noexcept {
    switch (keyCode) {
        case 53: return UiInputAction::BackScene;       // Esc
        case 12: return UiInputAction::Quit;            // Q
        case 18: return UiInputAction::SelectPrevTrack; // 1
        case 19: return UiInputAction::SelectNextTrack; // 2
        case 43: return UiInputAction::TrackPagePrev;   // ,
        case 47: return UiInputAction::TrackPageNext;   // .
        case 46: return UiInputAction::OpenManager;     // M
        case 38: return UiInputAction::ListDown;        // J
        case 40: return UiInputAction::ListUp;          // K
        case 36: return UiInputAction::ListEnter;       // Enter
        case 4:  return UiInputAction::ListParent;      // H
        case 51: return UiInputAction::ListParent;      // Backspace
        case 49: return UiInputAction::PreviewPlay;     // Space
        case 0:  return UiInputAction::PreviewAutoToggle; // A
        case 35: return UiInputAction::PlayActiveTrack; // P
        case 1:  return UiInputAction::StopActiveTrack; // S
        case 32: return UiInputAction::UnmuteActiveTrack; // U
        case 34: return UiInputAction::MuteActiveTrack;   // I
        case 17: return UiInputAction::MuteToggleActiveTrack; // T
        case 15: return UiInputAction::ArmToggleActiveTrack; // R
        case 41: return UiInputAction::ActionFocusPrev;  // ;
        case 39: return UiInputAction::ActionFocusNext;  // '
        case 44: return UiInputAction::ActionAdjustPrev; // /
        case 31: return UiInputAction::ActionApply;      // O
        case 16: return UiInputAction::ActionUndo;       // Y
        case 24: return UiInputAction::TrackSpeedUp;    // =
        case 27: return UiInputAction::TrackSpeedDown;  // -
        case 6:  return UiInputAction::QuantNone;       // Z
        case 7:  return UiInputAction::QuantBeat;       // X
        case 8:  return UiInputAction::QuantBar;        // C
        case 30: return UiInputAction::BpmUp;           // ]
        case 33: return UiInputAction::BpmDown;         // [
        case 122: return UiInputAction::F1;             // F1
        case 120: return UiInputAction::F2;             // F2
        case 99:  return UiInputAction::F3;             // F3
        case 118: return UiInputAction::F4;             // F4
        case 96:  return UiInputAction::F5;             // F5
        case 97:  return UiInputAction::F6;             // F6
        case 98:  return UiInputAction::F7;             // F7
        case 100: return UiInputAction::F8;             // F8
        case 101: return UiInputAction::F9;             // F9
        case 109: return UiInputAction::F10;            // F10
        case 103: return UiInputAction::F11;            // F11
        case 111: return UiInputAction::F12;            // F12
        default: break;
    }
    return UiInputAction::None;
}

// Дополнительный маппинг по printable-символам.
UiInputAction mapWindowChars(NSString* chars) noexcept {
    if (!chars || [chars length] == 0) {
        return UiInputAction::None;
    }
    const unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 27: return UiInputAction::BackScene;
        case '\r':
        case '\n':
            return UiInputAction::ListEnter;
        case 8:
        case 127:
            return UiInputAction::ListParent;
        case ' ':
            return UiInputAction::PreviewPlay;
        case 'u':
        case 'U':
            return UiInputAction::UnmuteActiveTrack;
        case 'i':
        case 'I':
            return UiInputAction::MuteActiveTrack;
        case 't':
        case 'T':
            return UiInputAction::MuteToggleActiveTrack;
        case 'r':
        case 'R':
            return UiInputAction::ArmToggleActiveTrack;
        case ';':
            return UiInputAction::ActionFocusPrev;
        case '\'':
            return UiInputAction::ActionFocusNext;
        case '/':
            return UiInputAction::ActionAdjustPrev;
        case '?':
            return UiInputAction::ActionAdjustNext;
        case 'o':
        case 'O':
            return UiInputAction::ActionApply;
        case 'y':
        case 'Y':
            return UiInputAction::ActionUndo;
        case ',':
            return UiInputAction::TrackPagePrev;
        case '.':
            return UiInputAction::TrackPageNext;
        default:
            return UiInputAction::None;
    }
}

// Унифицированный перевод NSEvent в UiInputAction.
UiInputAction mapWindowEvent(NSEvent* event) noexcept {
    if (!event || [event type] != NSEventTypeKeyDown) {
        return UiInputAction::None;
    }
    // Для '/' и '?' учитываем Shift на уровне физической клавиши,
    // чтобы не зависеть от символа текущей раскладки.
    if ([event keyCode] == 44) { // Slash key
        const NSEventModifierFlags mods =
            ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
        return (mods & NSEventModifierFlagShift)
                   ? UiInputAction::ActionAdjustNext
                   : UiInputAction::ActionAdjustPrev;
    }
    const UiInputAction byKeyCode = mapWindowKeyCode([event keyCode]);
    if (byKeyCode != UiInputAction::None) {
        return byKeyCode;
    }
    return mapWindowChars([event charactersIgnoringModifiers]);
}

} // namespace

struct MacGbWindowRenderer::Impl {
    // Топ-уровневое окно предпросмотра UI.
    NSWindow* window{nil};
    // Основное текстовое полотно (body с monospace рамками).
    NSTextView* textView{nil};
    // Overlay-заголовок (готический шрифт), рендерится поверх body.
    NSTextField* headerOverlay{nil};
    // Последний выведенный кадр; нужен для cheap dedup, чтобы не перерисовывать одинаковый текст.
    NSString* lastText{nil};
    // Шрифты: декоративный header и моноширинный body.
    NSFont* headerFont{nil};
    NSFont* bodyFont{nil};
    // Кэш цветов палитры активной темы.
    NSColor* bg{nil};
    NSColor* panel{nil};
    NSColor* mid{nil};
    NSColor* text{nil};
    // Активная тема интерфейса.
    UiTheme theme{UiTheme::Gothic};
    // Защита очереди input-событий окна.
    std::mutex inputMutex{};
    // FIFO действий клавиатуры, которые считывает control-loop.
    std::deque<UiInputAction> inputQueue{};
    // Локальный AppKit monitor для перехвата хоткеев внутри window режима.
    id keyMonitor{nil};

    explicit Impl(UiTheme t) : theme(t) {}
};

MacGbWindowRenderer::MacGbWindowRenderer(UiTheme theme, uint16_t textWidth)
    : impl_(new Impl(theme)),
      textWidth_(textWidth) {
    NSArray<NSString*>* assetFonts = discoverAssetFontPostScriptNames();

    // Явная инициализация AppKit окружения в desktop режиме.
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    const NSRect frame = NSMakeRect(120.0, 120.0, 780.0, 560.0);
    impl_->window = [[NSWindow alloc] initWithContentRect:frame
                                                 styleMask:(NSWindowStyleMaskTitled |
                                                            NSWindowStyleMaskClosable |
                                                            NSWindowStyleMaskMiniaturizable)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    [impl_->window setTitle:@"Avantgarde // Gothic UI Preview"];
    [impl_->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:[[impl_->window contentView] bounds]];
    [scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [scroll setHasVerticalScroller:YES];
    [scroll setHasHorizontalScroller:YES];
    [scroll setBorderType:NSNoBorder];

    impl_->textView = [[NSTextView alloc] initWithFrame:[[impl_->window contentView] bounds]];
    [impl_->textView setEditable:NO];
    [impl_->textView setSelectable:NO];
    [impl_->textView setAutomaticQuoteSubstitutionEnabled:NO];
    [impl_->textView setAutomaticDashSubstitutionEnabled:NO];
    [impl_->textView setAutomaticDataDetectionEnabled:NO];
    [impl_->textView setAutomaticTextCompletionEnabled:NO];
    [impl_->textView setAutomaticSpellingCorrectionEnabled:NO];
    [impl_->textView setHorizontallyResizable:YES];
    [impl_->textView setVerticallyResizable:YES];
    [impl_->textView setRichText:YES];
    [impl_->textView setUsesFontPanel:NO];
    [impl_->textView setTextContainerInset:NSMakeSize(8.0, 10.0)];
    [impl_->textView setSelectable:YES];

    // Header может быть декоративным и не обязан быть моноширинным.
    impl_->headerFont = assetFonts.count > 0
                        ? pickFirstFont(assetFonts, 28.0, nil)
                        : nil;
    if (!impl_->headerFont) {
        impl_->headerFont = pickFirstFont(
            @[ @"UnifrakturCook-Bold", @"UnifrakturCook-Regular", @"Old English Text MT" ],
            28.0,
            [NSFont boldSystemFontOfSize:24.0]
        );
    }

    // Body обязан быть моноширинным: на нем завязана геометрия рамок.
    impl_->bodyFont = pickFixedPitchFont(assetFonts, 10.0);
    if (!impl_->bodyFont && assetFonts.count > 0) {
        // If custom font is not fixed-pitch, keep monospaced body for frame alignment.
        std::fprintf(stderr,
                     "[gb-window] Loaded custom font is not fixed-pitch. "
                     "Using monospaced body font.\n");
    }
    if (impl_->bodyFont && !hasStableFrameMetrics(impl_->bodyFont)) {
        std::fprintf(stderr,
                     "[gb-window] Custom fixed-pitch font has unstable frame glyph metrics. "
                     "Using monospaced fallback for stable borders.\n");
        impl_->bodyFont = nil;
    }
    if (!impl_->bodyFont) {
        impl_->bodyFont = pickFirstFont(
            @[ @"PressStart2P-Regular", @"Press Start 2P", @"Silkscreen-Regular", @"Menlo" ],
            10.0,
            [NSFont monospacedSystemFontOfSize:12.0 weight:NSFontWeightRegular]
        );
    }
    [impl_->textView setFont:impl_->bodyFont];

    if (assetFonts.count > 0) {
        std::fprintf(stderr,
                     "[gb-window] Using asset font(s). Header='%s', Body='%s'\n",
                     [[impl_->headerFont fontName] UTF8String],
                     [[impl_->bodyFont fontName] UTF8String]);
    } else {
        std::fprintf(stderr,
                     "[gb-window] No fonts found in assets/fonts. Using fallback fonts.\n");
    }

    const UiThemePalette palette = uiThemePalette(theme);
    impl_->bg = colorFromRgb(palette.bg.r, palette.bg.g, palette.bg.b);
    impl_->panel = colorFromRgb(palette.panel.r, palette.panel.g, palette.panel.b);
    impl_->mid = colorFromRgb(palette.mid.r, palette.mid.g, palette.mid.b);
    impl_->text = colorFromRgb(palette.text.r, palette.text.g, palette.text.b);
    [impl_->textView setBackgroundColor:impl_->bg];
    [impl_->textView setTextColor:impl_->text];

    // Готический заголовок рисуем отдельным overlay:
    // рамка в тексте остается строго monospace и не зависит от декоративного шрифта.
    impl_->headerOverlay = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [impl_->headerOverlay setStringValue:@"AVANTGARDE"];
    [impl_->headerOverlay setEditable:NO];
    [impl_->headerOverlay setSelectable:NO];
    [impl_->headerOverlay setBezeled:NO];
    [impl_->headerOverlay setBordered:NO];
    [impl_->headerOverlay setDrawsBackground:NO];
    [impl_->headerOverlay setBackgroundColor:[NSColor clearColor]];
    [impl_->headerOverlay setTextColor:impl_->text];
    [impl_->headerOverlay setFont:impl_->headerFont];
    [impl_->headerOverlay sizeToFit];

    const CGFloat cellW = std::max<CGFloat>(glyphWidth(impl_->bodyFont, @"M"), 1.0);
    const CGFloat lineH = std::max<CGFloat>([[impl_->textView layoutManager] defaultLineHeightForFont:impl_->bodyFont], 1.0);
    const NSSize inset = [impl_->textView textContainerInset];
    const CGFloat leading = inset.width + (cellW * 2.0) + kHeaderOverlayXBias;
    const CGFloat top = inset.height + lineH + kHeaderOverlayYBias;

    [impl_->headerOverlay setTranslatesAutoresizingMaskIntoConstraints:NO];
    [impl_->textView addSubview:impl_->headerOverlay positioned:NSWindowAbove relativeTo:nil];
    [NSLayoutConstraint activateConstraints:@[
        [impl_->headerOverlay.leadingAnchor constraintEqualToAnchor:impl_->textView.leadingAnchor constant:leading],
        [impl_->headerOverlay.topAnchor constraintEqualToAnchor:impl_->textView.topAnchor constant:top]
    ]];

    [scroll setDocumentView:impl_->textView];
    [[impl_->window contentView] addSubview:scroll];

    [impl_->window makeFirstResponder:impl_->textView];
    [impl_->window makeKeyWindow];
    [impl_->window orderFrontRegardless];

    // Local monitor кладет действия в очередь и "съедает" уже обработанные хоткеи.
    __block Impl* weakImpl = impl_;
    impl_->keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                               handler:^NSEvent* _Nullable(NSEvent* _Nonnull event) {
        if (!weakImpl) {
            return event;
        }
        const UiInputAction action = mapWindowEvent(event);
        if (action == UiInputAction::None) {
            return event;
        }
        std::lock_guard<std::mutex> lock(weakImpl->inputMutex);
        weakImpl->inputQueue.push_back(action);
        return nil; // consume mapped hotkey in window mode
    }];
}

MacGbWindowRenderer::~MacGbWindowRenderer() {
    if (impl_) {
        if (impl_->window) {
            [impl_->window orderOut:nil];
            [impl_->window close];
            impl_->window = nil;
        }
        if (impl_->keyMonitor) {
            [NSEvent removeMonitor:impl_->keyMonitor];
            impl_->keyMonitor = nil;
        }
        impl_->headerOverlay = nil;
        impl_->textView = nil;
        impl_->lastText = nil;
        delete impl_;
        impl_ = nullptr;
    }
}

void MacGbWindowRenderer::render(const UiState& state) {
    const std::string frame = GbFrameComposer::buildMonochromeFrame(state, textWidth_);
    renderCustomFrame(frame, /*showHeaderOverlay=*/true);
}

void MacGbWindowRenderer::renderCustomFrame(const std::string& monoFrame, bool showHeaderOverlay) {
    if (!impl_ || !impl_->textView) {
        return;
    }

    if (impl_->headerOverlay) {
        [impl_->headerOverlay setHidden:!showHeaderOverlay];
    }

    NSString* text = [NSString stringWithUTF8String:monoFrame.c_str()];
    if (!text) {
        return;
    }
    // Кадр не изменился: избегаем лишнего обновления NSTextStorage.
    if (impl_->lastText && [impl_->lastText isEqualToString:text]) {
        return;
    }
    impl_->lastText = text;

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    [paragraph setLineBreakMode:NSLineBreakByClipping];
    [paragraph setLineSpacing:1.0];

    NSMutableAttributedString* attr = [[NSMutableAttributedString alloc] initWithString:text];
    NSRange full = NSMakeRange(0, [text length]);
    [attr addAttributes:@{
        NSFontAttributeName: impl_->bodyFont,
        NSForegroundColorAttributeName: impl_->text,
        NSParagraphStyleAttributeName: paragraph
    } range:full];

    NSArray<NSString*>* lines = [text componentsSeparatedByString:@"\n"];
    NSUInteger offset = 0;
    for (NSString* line in lines) {
        const NSUInteger len = [line length];
        if (len == 0) {
            offset += 1;
            continue;
        }
        const NSRange lineRange = NSMakeRange(offset, len);
        const bool isBorder = [line hasPrefix:@"╔"] || [line hasPrefix:@"╚"] ||
                              [line hasPrefix:@"╠"] || [line hasPrefix:@"╟"];
        const bool isHeader = [line containsString:@"AVANTGARDE"];
        const bool isTransport = [line containsString:@" TRN:"] || [line containsString:@" ACTIVE:"];
        const bool isActiveTrack = [line containsString:@"▶ T"];

        if (isBorder) {
            [attr addAttributes:@{
                NSFontAttributeName: impl_->bodyFont,
                NSForegroundColorAttributeName: impl_->mid
            } range:lineRange];
        } else if (isHeader) {
            [attr addAttributes:@{
                NSFontAttributeName: impl_->bodyFont,
                NSForegroundColorAttributeName: impl_->text,
                NSBackgroundColorAttributeName: impl_->panel
            } range:lineRange];

            NSRange titleLocal = [line rangeOfString:@"AVANTGARDE"];
            if (showHeaderOverlay && titleLocal.location != NSNotFound) {
                NSRange titleGlobal = NSMakeRange(offset + titleLocal.location, titleLocal.length);
                [attr addAttributes:@{
                    NSForegroundColorAttributeName: [NSColor clearColor]
                } range:titleGlobal];
            }
        } else if (isTransport || isActiveTrack) {
            [attr addAttributes:@{
                NSForegroundColorAttributeName: impl_->text,
                NSBackgroundColorAttributeName: impl_->panel
            } range:lineRange];
        }
        offset += len + 1;
    }

    [[impl_->textView textStorage] setAttributedString:attr];
}

void MacGbWindowRenderer::pumpEvents() noexcept {
    // Неблокирующий pump очереди AppKit-событий.
    // Вызывается часто из main loop, чтобы держать input latency низкой.
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

bool MacGbWindowRenderer::readNextInputEvent(UiInputEvent& out) noexcept {
    out.action = UiInputAction::None;
    if (!impl_) {
        return false;
    }
    // Очередь input общая для monitor callback и control-thread.
    std::lock_guard<std::mutex> lock(impl_->inputMutex);
    if (impl_->inputQueue.empty()) {
        return false;
    }
    out.action = impl_->inputQueue.front();
    impl_->inputQueue.pop_front();
    return true;
}

} // namespace avantgarde
