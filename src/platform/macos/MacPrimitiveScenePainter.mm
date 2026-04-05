#include "platform/macos/MacPrimitiveScenePainter.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "contracts/ui/components/UiComponents.h"
#include "platform/render/PreparedLayoutUtils.h"
#include "platform/render/RenderGeometry.h"
#include "service/ui/layout/UiLayoutEngine.h"

namespace avantgarde::macos {
namespace {

NSFont* resizedFont(NSFont* font, CGFloat size) {
    if (!font) {
        return nil;
    }
    NSFont* resized = [NSFont fontWithDescriptor:[font fontDescriptor] size:size];
    return resized ? resized : font;
}

bool hasFontExtensionUtf8(std::string_view path) {
    namespace fs = std::filesystem;
    const fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
}

std::vector<std::string> buildFontPathCandidates(std::string_view specUtf8,
                                                 std::string_view cwdUtf8) {
    namespace fs = std::filesystem;
    std::vector<std::string> out{};
    if (specUtf8.empty()) {
        return out;
    }
    auto pushUnique = [&out](const fs::path& p) {
        const std::string s = p.lexically_normal().string();
        if (s.empty()) {
            return;
        }
        if (std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    };

    const fs::path spec(specUtf8);
    const fs::path cwd(cwdUtf8);

    if (spec.is_absolute()) {
        pushUnique(spec);
    } else {
        pushUnique(cwd / spec);
    }
    // Короткая форма: только имя файла -> assets/fonts/<name>.
    if (spec.parent_path().empty()) {
        pushUnique(cwd / "assets" / "fonts" / spec);
    }
    // Альтернатива: fonts/<name> -> assets/fonts/<name>.
    if (specUtf8.rfind("fonts/", 0) == 0U) {
        pushUnique(cwd / "assets" / spec);
    }
    return out;
}

std::string toLowerAscii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

struct NodeTextFxStyle {
    bool active{false};
    CGFloat jitterX{0.0};
    CGFloat splitPx{0.0};
    CGFloat alpha{1.0};
    uint8_t bandCount{2U};
    bool alternatePhase{false};
};

} // namespace

void renderPreparedLayoutScene(const MacPrimitiveScenePaintContext& ctx,
                               const UiPreparedLayout& prepared) {
    if (!ctx.canvas || !prepared.layoutTemplate) {
        return;
    }

    CALayer* root = [ctx.canvas layer];
    if (!root) {
        return;
    }
    [root setSublayers:nil];

    const NSRect canvasBounds = [ctx.canvas bounds];
    const render::UiFrameMetrics metrics = render::computeFrameMetrics(
        prepared,
        static_cast<float>(canvasBounds.size.width),
        static_cast<float>(canvasBounds.size.height),
        static_cast<float>(ctx.cellW),
        static_cast<float>(ctx.cellH),
        static_cast<float>(ctx.margin));
    const uint16_t frameWChars = metrics.frameWidthChars;
    const uint16_t innerH = metrics.innerHeightChars;
    const uint16_t frameHChars = metrics.frameHeightChars;
    const CGFloat cellH = metrics.cellHeightPx;

    auto pxRectForChars = [&](int16_t x, int16_t y, uint16_t w, uint16_t h) -> CGRect {
        const render::UiRectPx topDown = render::charsToPixelsTopDown(metrics, x, y, w, h);
        // UiLayout идет в top-down координатах, CoreAnimation — bottom-up.
        const render::UiRectPx bottomUp =
            render::toBottomUp(topDown, static_cast<float>(canvasBounds.size.height));
        return CGRectMake(bottomUp.x, bottomUp.y, bottomUp.w, bottomUp.h);
    };

    auto fontForNode = [&](const UiLayoutNode* node, NSFont* fallback) -> NSFont* {
        if (!node) {
            return fallback;
        }
        const CGFloat baseSize = (fallback ? fallback.pointSize : 12.0);
        const CGFloat size = (node->fontSize > 0.0f)
                                 ? std::clamp<CGFloat>(node->fontSize, 6.0, 64.0)
                                 : baseSize;
        if (node->font.empty()) {
            return resizedFont(fallback, size);
        }
        std::string f = toLowerAscii(node->font);
        if (f == "default") {
            return resizedFont(fallback, size);
        }
        if (f == "gothic") {
            return resizedFont(ctx.gothicFont ? ctx.gothicFont : fallback, size);
        }
        if (f == "body") {
            return resizedFont(ctx.bodyFont ? ctx.bodyFont : fallback, size);
        }

        const std::string cacheKey = f + "|" + std::to_string(static_cast<int>(std::lround(size)));
        if (ctx.dynamicFontCache) {
            if (const auto it = ctx.dynamicFontCache->find(cacheKey); it != ctx.dynamicFontCache->end()) {
                return it->second ? it->second : fallback;
            }
        }

        // 1) Пробуем как PostScript имя.
        NSFont* loaded = [NSFont fontWithName:[NSString stringWithUTF8String:f.c_str()] size:size];

        // 2) Пробуем как путь к font-файлу.
        if (!loaded && hasFontExtensionUtf8(f)) {
            const std::vector<std::string> candidates = buildFontPathCandidates(f, ctx.cwd);
            for (const std::string& candidate : candidates) {
                NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:candidate.c_str()]];
                if (!url) {
                    continue;
                }
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
                loaded = [NSFont fontWithName:(__bridge NSString*)psName size:size];
                CFRelease(psName);
                if (loaded) {
                    break;
                }
            }
        }

        if (ctx.dynamicFontCache) {
            ctx.dynamicFontCache->emplace(cacheKey, loaded);
        }
        if (loaded) {
            return loaded;
        }
        return fallback;
    };

    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    auto glitchStyleForNode = [&](const UiLayoutNode* node, const IUiComponent* component) -> NodeTextFxStyle {
        NodeTextFxStyle style{};
        if (!node || !ctx.visualFx) {
            return style;
        }

        VisualFxRequest request{};
        request.nodeId = node->id;
        request.instanceKey = component ? std::string(component->id()) : std::string{};
        request.nodeText = node->text;
        request.effect = node->effect;
        request.effectTrigger = node->effectTrigger;
        request.effectTriggerOutMs = node->effectTriggerOutMs;
        request.effectIntervalMs = node->effectIntervalMs;
        request.effectAmount = node->effectAmount;
        request.effectSpeed = node->effectSpeed;
        request.nowMs = nowMs;

        if (const auto* knob = dynamic_cast<const UiKnobComponent*>(component)) {
            request.hasValue01 = true;
            request.value01 = knob->value01;
        } else if (const auto* sw = dynamic_cast<const UiSwitchComponent*>(component)) {
            const uint16_t denom = std::max<uint16_t>(
                1U,
                static_cast<uint16_t>(sw->options.size() > 1U ? sw->options.size() - 1U : 1U));
            request.hasValue01 = true;
            request.value01 = static_cast<float>(sw->selectedIndex) / static_cast<float>(denom);
        } else if (const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component)) {
            request.hasValue01 = true;
            request.value01 = anim->intensity01;
        }

        const VisualFxTextStyle fx = ctx.visualFx->resolveTextStyle(request);
        style.active = fx.active;
        style.jitterX = static_cast<CGFloat>(fx.jitterX);
        style.splitPx = static_cast<CGFloat>(fx.splitPx);
        style.alpha = static_cast<CGFloat>(fx.alpha);
        style.bandCount = fx.bandCount;
        style.alternatePhase = fx.alternatePhase;
        return style;
    };

    // Внешняя рамка кадра.
    CGRect frameRectPx = CGRectZero;
    {
        CAShapeLayer* frame = [CAShapeLayer layer];
        frame.frame = root.bounds;
        frame.fillColor = [NSColor clearColor].CGColor;
        frame.strokeColor = ctx.mid.CGColor;
        frame.lineWidth = 1.4;
        CGMutablePathRef p = CGPathCreateMutable();
        const CGRect r = pxRectForChars(0, 0, frameWChars, frameHChars);
        frameRectPx = r;
        CGPathAddRoundedRect(p, nullptr, r, 4.0, 4.0);
        frame.path = p;
        CGPathRelease(p);
        [root addSublayer:frame];
    }

    // Контент сцены рисуем в отдельный слой с маской по внутренней рамке.
    // Это жестко ограничивает отрисовку: ни один элемент не может "вылезти" за границы кадра.
    CALayer* contentLayer = [CALayer layer];
    contentLayer.frame = root.bounds;
    {
        const CGRect clipRect = CGRectInset(frameRectPx, 1.0, 1.0);
        CAShapeLayer* clipMask = [CAShapeLayer layer];
        clipMask.frame = contentLayer.bounds;
        CGMutablePathRef p = CGPathCreateMutable();
        CGPathAddRect(p, nullptr, clipRect);
        clipMask.path = p;
        CGPathRelease(p);
        contentLayer.mask = clipMask;
    }
    [root addSublayer:contentLayer];

    render::UiComponentIndex byId = render::buildComponentIndex(prepared);

    const uint16_t innerW = static_cast<uint16_t>(frameWChars - 2U);
    const UiLayoutEngine::Result layout = UiLayoutEngine::arrange(prepared.layoutTemplate->root, innerW, innerH);

    auto textLayer = [&](const CGRect& rect,
                         NSString* text,
                         NSColor* color,
                         NSFont* font,
                         CATextLayerAlignmentMode align,
                         bool wrapText,
                         const NodeTextFxStyle* fx) {
        if (!text || [text length] == 0) {
            return;
        }

        const CGFloat scale = [NSScreen mainScreen] ? [NSScreen mainScreen].backingScaleFactor : 2.0;
        auto makeTextLayer = [&](NSString* drawText,
                                 const CGRect& layerRect,
                                 NSColor* layerColor,
                                 CGFloat opacity) -> CATextLayer* {
            CATextLayer* t = [CATextLayer layer];
            t.frame = layerRect;
            t.string = drawText;
            t.foregroundColor = layerColor.CGColor;
            t.font = (__bridge CFTypeRef)font;
            t.fontSize = font.pointSize;
            t.alignmentMode = align;
            t.wrapped = wrapText ? YES : NO;
            t.contentsScale = std::max<CGFloat>(scale, 1.0);
            t.opacity = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
            return t;
        };
        auto pushTextLayer = [&](NSString* drawText,
                                 const CGRect& layerRect,
                                 NSColor* layerColor,
                                 CGFloat opacity) {
            [contentLayer addSublayer:makeTextLayer(drawText, layerRect, layerColor, opacity)];
        };

        auto drawSegment = [&](NSString* drawText, const CGRect& drawRect) {
            if (!fx || !fx->active) {
                pushTextLayer(drawText, drawRect, color, 1.0);
                return;
            }

            const CGRect base = CGRectOffset(drawRect, fx->jitterX, 0.0);
            if (fx->splitPx > 0.01) {
                NSColor* splitA = [ctx.mid colorWithAlphaComponent:0.42];
                NSColor* splitB = [ctx.text colorWithAlphaComponent:0.26];
                pushTextLayer(drawText, CGRectOffset(base, -fx->splitPx, 0.0), splitA, 1.0);
                pushTextLayer(drawText, CGRectOffset(base, fx->splitPx, 0.0), splitB, 1.0);
            }
            pushTextLayer(drawText, base, color, fx->alpha);

            // Срезы по крупным полосам: верх/середина/низ с попеременным направлением.
            const uint8_t bands = std::max<uint8_t>(2U, fx->bandCount);
            const CGFloat bandH = base.size.height / static_cast<CGFloat>(bands);
            for (uint8_t i = 0; i < bands; ++i) {
                const bool rightShift = ((i + (fx->alternatePhase ? 1U : 0U)) % 2U) == 0U;
                const CGFloat dir = rightShift ? 1.0 : -1.0;
                const CGFloat shiftX = dir * (0.35 + fx->splitPx * 0.95);
                const CGFloat sliceY = base.origin.y + static_cast<CGFloat>(i) * bandH;
                const CGFloat sliceH = (i + 1U == bands) ? (base.size.height - static_cast<CGFloat>(i) * bandH) : bandH;

                CATextLayer* slice = makeTextLayer(
                    drawText,
                    CGRectOffset(base, shiftX, 0.0),
                    [color colorWithAlphaComponent:std::clamp<CGFloat>(fx->alpha + 0.02, 0.0, 1.0)],
                    1.0);

                CAShapeLayer* mask = [CAShapeLayer layer];
                mask.frame = CGRectMake(0.0, 0.0, slice.frame.size.width, slice.frame.size.height);
                CGMutablePathRef p = CGPathCreateMutable();
                CGPathAddRect(p, nullptr, CGRectMake(0.0, sliceY - slice.frame.origin.y, slice.frame.size.width, sliceH));
                mask.path = p;
                CGPathRelease(p);
                slice.mask = mask;
                [contentLayer addSublayer:slice];
            }
        };

        // Для декоративных шрифтов с нулевым space-глифом рисуем слова по отдельности
        // и задаем межсловный интервал вручную.
        bool manualWordSpacing = false;
        if (font && [text rangeOfString:@" "].location != NSNotFound) {
            NSDictionary* attrs = @{NSFontAttributeName : font};
            const CGFloat spaceW = [@" " sizeWithAttributes:attrs].width;
            manualWordSpacing = (spaceW <= 0.5);
        }

        if (!manualWordSpacing || wrapText) {
            drawSegment(text, rect);
            return;
        }

        NSDictionary* attrs = @{NSFontAttributeName : font};
        const CGFloat gap = std::max<CGFloat>(4.0, font.pointSize * 0.42);
        const NSUInteger len = [text length];

        auto measureWord = [&](NSRange r) -> CGFloat {
            if (r.length == 0U) {
                return 0.0;
            }
            NSString* word = [text substringWithRange:r];
            return [word sizeWithAttributes:attrs].width;
        };

        CGFloat contentW = 0.0;
        for (NSUInteger i = 0; i < len;) {
            const unichar ch = [text characterAtIndex:i];
            if (ch == ' ') {
                NSUInteger j = i;
                while (j < len && [text characterAtIndex:j] == ' ') {
                    ++j;
                }
                contentW += gap * static_cast<CGFloat>(j - i);
                i = j;
            } else {
                NSUInteger j = i;
                while (j < len && [text characterAtIndex:j] != ' ') {
                    ++j;
                }
                contentW += measureWord(NSMakeRange(i, j - i));
                i = j;
            }
        }

        CGFloat startX = rect.origin.x;
        if (align == kCAAlignmentCenter) {
            startX += std::max<CGFloat>(0.0, (rect.size.width - contentW) * 0.5);
        } else if (align == kCAAlignmentRight) {
            startX += std::max<CGFloat>(0.0, rect.size.width - contentW);
        }

        CGFloat cursorX = startX;
        for (NSUInteger i = 0; i < len;) {
            const unichar ch = [text characterAtIndex:i];
            if (ch == ' ') {
                NSUInteger j = i;
                while (j < len && [text characterAtIndex:j] == ' ') {
                    ++j;
                }
                cursorX += gap * static_cast<CGFloat>(j - i);
                i = j;
            } else {
                NSUInteger j = i;
                while (j < len && [text characterAtIndex:j] != ' ') {
                    ++j;
                }
                const NSRange r = NSMakeRange(i, j - i);
                NSString* word = [text substringWithRange:r];
                const CGFloat wordW = std::max<CGFloat>(1.0, measureWord(r));
                const CGRect wordRect = CGRectMake(cursorX, rect.origin.y, wordW, rect.size.height);
                drawSegment(word, wordRect);
                cursorX += wordW;
                i = j;
            }
        }
    };

    for (const UiLayoutBox& box : layout.boxes) {
        if (!box.node || box.node->id.empty()) {
            continue;
        }
        const auto it = byId.find(box.node->id);
        const IUiComponent* component = (it == byId.end()) ? nullptr : it->second;

        const CGRect rect = pxRectForChars(static_cast<int16_t>(box.rect.x + 1),
                                           static_cast<int16_t>(box.rect.y + 1),
                                           box.rect.width,
                                           box.rect.height);

        switch (box.node->type) {
            case UiLayoutNodeType::StatusBar:
            case UiLayoutNodeType::Text: {
                const NodeTextFxStyle fx = glitchStyleForNode(box.node, component);
                NSString* text = nil;
                NSFont* textFont = fontForNode(box.node, ctx.bodyFont);
                CGRect textRect = rect;
                const bool wrapText = box.node->textWrap;
                if (const auto* s = dynamic_cast<const UiStatusBarComponent*>(component)) {
                    text = [NSString stringWithUTF8String:s->text.c_str()];
                    const bool isHeaderTitle = (box.node->id == "header_title");
                    const CGRect panelRect = rect;
                    CALayer* panel = [CALayer layer];
                    panel.frame = panelRect;
                    if (isHeaderTitle) {
                        panel.backgroundColor = [ctx.mid colorWithAlphaComponent:0.22].CGColor;
                    } else {
                        panel.backgroundColor = ctx.panel.CGColor;
                    }
                    [contentLayer addSublayer:panel];

                    if (isHeaderTitle) {
                        // Красим верхний зазор до внутренней границы рамки тем же тоном,
                        // но не задеваем нижние строки статуса.
                        const CGFloat frameInnerTop = CGRectGetMaxY(frameRectPx) - 1.0;
                        const CGFloat gapBottom = CGRectGetMaxY(panelRect);
                        const CGFloat gapH = frameInnerTop - gapBottom;
                        if (gapH > 0.5) {
                            CALayer* topFill = [CALayer layer];
                            topFill.frame = CGRectMake(panelRect.origin.x, gapBottom, panelRect.size.width, gapH);
                            topFill.backgroundColor = [ctx.mid colorWithAlphaComponent:0.22].CGColor;
                            [contentLayer addSublayer:topFill];
                        }
                    }
                    if (isHeaderTitle && textFont) {
                        const CGFloat lineH =
                            std::min<CGFloat>(rect.size.height, std::max<CGFloat>(textFont.pointSize * 1.18, 16.0));
                        textRect = CGRectMake(rect.origin.x,
                                              rect.origin.y + std::max<CGFloat>(0.0, rect.size.height - lineH - 1.0),
                                              rect.size.width,
                                              lineH);
                    }
                } else if (const auto* t = dynamic_cast<const UiTextComponent*>(component)) {
                    text = [NSString stringWithUTF8String:t->text.c_str()];
                } else if (!box.node->text.empty()) {
                    text = [NSString stringWithUTF8String:box.node->text.c_str()];
                }
                textLayer(textRect, text, ctx.text, textFont, kCAAlignmentLeft, wrapText, &fx);
            } break;

            case UiLayoutNodeType::Separator: {
                CALayer* line = [CALayer layer];
                line.backgroundColor = ctx.mid.CGColor;
                line.frame = CGRectMake(rect.origin.x, rect.origin.y + rect.size.height * 0.5, rect.size.width, 1.0);
                [contentLayer addSublayer:line];
            } break;

            case UiLayoutNodeType::List:
            case UiLayoutNodeType::Spacer: {
                const auto* list = dynamic_cast<const UiListComponent*>(component);
                if (!list) {
                    break;
                }
                const CGFloat rowH = std::max<CGFloat>(cellH - 2.0, 14.0);
                const std::size_t maxRows = std::min<std::size_t>(
                    list->rows.size(),
                    static_cast<std::size_t>(std::max<int>(1, static_cast<int>(std::floor(rect.size.height / rowH)))));
                for (std::size_t i = 0; i < maxRows; ++i) {
                    const bool selected = (list->selectedRow >= 0) &&
                                          (static_cast<std::size_t>(list->selectedRow) == i);
                    std::string row = render::markerPrefix(list->marker, selected) + list->rows[i];
                    NSString* rowText = [NSString stringWithUTF8String:row.c_str()];
                    const CGFloat rowTop = rect.origin.y + rect.size.height - static_cast<CGFloat>(i + 1U) * rowH;
                    const CGRect rr = CGRectMake(rect.origin.x,
                                                 rowTop,
                                                 rect.size.width,
                                                 rowH);
                    textLayer(rr, rowText, ctx.text, ctx.bodyFont, kCAAlignmentLeft, false, nullptr);
                }
            } break;

            case UiLayoutNodeType::Knob: {
                const auto* knob = dynamic_cast<const UiKnobComponent*>(component);
                if (!knob) {
                    break;
                }
                NSFont* labelFont = fontForNode(box.node, ctx.bodyFont);
                const CGFloat labelH = knob->label.empty()
                                           ? 0.0
                                           : std::max<CGFloat>(12.0, (labelFont ? labelFont.pointSize * 1.15 : 12.0));
                const CGFloat knobAreaY = rect.origin.y;
                const CGFloat knobAreaH = std::max<CGFloat>(12.0, rect.size.height - labelH - (labelH > 0.0 ? 2.0 : 0.0));
                const CGFloat maxSide = std::max<CGFloat>(8.0, std::min<CGFloat>(knobAreaH - 1.0, rect.size.width - 6.0));
                const CGFloat baseSide = std::max<CGFloat>(14.0, std::min<CGFloat>(30.0, maxSide));
                const CGFloat knobScale = std::clamp<CGFloat>(
                    (box.node->knobSize > 0.0f) ? static_cast<CGFloat>(box.node->knobSize) : 1.0,
                    0.2,
                    4.0);
                const CGFloat sz = std::clamp(baseSide * knobScale, 8.0, maxSide);
                const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
                const CGFloat cy = knobAreaY + knobAreaH * 0.5;
                const CGFloat r = sz * 0.42;

                CAShapeLayer* ring = [CAShapeLayer layer];
                ring.frame = contentLayer.bounds;
                ring.fillColor = [NSColor clearColor].CGColor;
                ring.strokeColor = ctx.text.CGColor;
                ring.lineWidth = 1.7;
                CGMutablePathRef ringPath = CGPathCreateMutable();
                CGPathAddEllipseInRect(ringPath, nullptr, CGRectMake(cx - r, cy - r, r * 2.0, r * 2.0));
                ring.path = ringPath;
                CGPathRelease(ringPath);
                [contentLayer addSublayer:ring];

                const CGFloat v = std::clamp(knob->value01, 0.0f, 1.0f);
                const CGFloat a = (225.0 - v * 270.0) * static_cast<CGFloat>(M_PI / 180.0);
                const CGFloat nx = cx + std::cos(a) * r * 0.85;
                const CGFloat ny = cy + std::sin(a) * r * 0.85;

                CAShapeLayer* needle = [CAShapeLayer layer];
                needle.frame = contentLayer.bounds;
                needle.fillColor = [NSColor clearColor].CGColor;
                needle.strokeColor = ctx.text.CGColor;
                needle.lineWidth = 1.8;
                CGMutablePathRef nPath = CGPathCreateMutable();
                CGPathMoveToPoint(nPath, nullptr, cx, cy);
                CGPathAddLineToPoint(nPath, nullptr, nx, ny);
                needle.path = nPath;
                CGPathRelease(nPath);
                [contentLayer addSublayer:needle];

                if (!knob->label.empty()) {
                    const NodeTextFxStyle fx = glitchStyleForNode(box.node, component);
                    NSString* label = [NSString stringWithUTF8String:knob->label.c_str()];
                    const CGRect lr = CGRectMake(rect.origin.x + 1.0,
                                                 rect.origin.y + rect.size.height - labelH,
                                                 std::max<CGFloat>(8.0, rect.size.width - 2.0),
                                                 labelH);
                    textLayer(lr, label, ctx.text, labelFont, kCAAlignmentCenter, false, &fx);
                }
            } break;

            case UiLayoutNodeType::Switch: {
                const auto* sw = dynamic_cast<const UiSwitchComponent*>(component);
                if (!sw) {
                    break;
                }
                NSFont* labelFont = fontForNode(box.node, ctx.gothicFont);
                const CGFloat labelH = sw->label.empty()
                                           ? 0.0
                                           : std::max<CGFloat>(14.0, (labelFont ? labelFont.pointSize * 1.2 : 14.0));
                if (!sw->label.empty()) {
                    const NodeTextFxStyle fx = glitchStyleForNode(box.node, component);
                    NSString* label = [NSString stringWithUTF8String:sw->label.c_str()];
                    const CGRect lr = CGRectMake(rect.origin.x, rect.origin.y, rect.size.width, labelH);
                    textLayer(lr, label, ctx.text, labelFont, kCAAlignmentLeft, false, &fx);
                }
                const CGFloat bodyAreaY = rect.origin.y + labelH + (labelH > 0.0 ? 2.0 : 0.0);
                const CGFloat bodyAreaH = std::max<CGFloat>(12.0, rect.size.height - labelH - (labelH > 0.0 ? 2.0 : 0.0));
                const CGFloat h = std::max<CGFloat>(12.0, std::min<CGFloat>(16.0, bodyAreaH - 2.0));
                const CGFloat w = std::max<CGFloat>(42.0, rect.size.width * 0.8);
                const CGFloat x = rect.origin.x;
                const CGFloat y = bodyAreaY + std::max<CGFloat>(1.0, (bodyAreaH - h) * 0.5);
                const CGFloat radius = h * 0.5;

                CAShapeLayer* body = [CAShapeLayer layer];
                body.frame = contentLayer.bounds;
                body.fillColor = ctx.panel.CGColor;
                body.strokeColor = ctx.text.CGColor;
                body.lineWidth = 1.4;
                CGMutablePathRef bPath = CGPathCreateMutable();
                CGPathAddRoundedRect(bPath, nullptr, CGRectMake(x, y, w, h), radius, radius);
                body.path = bPath;
                CGPathRelease(bPath);
                [contentLayer addSublayer:body];

                const uint16_t total = std::max<uint16_t>(2U, static_cast<uint16_t>(sw->options.empty() ? 2U : sw->options.size()));
                const uint16_t idx = std::min<uint16_t>(sw->selectedIndex, static_cast<uint16_t>(total - 1U));
                const CGFloat t = static_cast<CGFloat>(idx) / static_cast<CGFloat>(total - 1U);
                const CGFloat thumbR = h * 0.38;
                const CGFloat thumbX = x + thumbR + (w - thumbR * 2.0) * t;
                const CGFloat thumbY = y + h * 0.5;

                CAShapeLayer* thumb = [CAShapeLayer layer];
                thumb.frame = contentLayer.bounds;
                thumb.fillColor = ctx.text.CGColor;
                CGMutablePathRef tPath = CGPathCreateMutable();
                CGPathAddEllipseInRect(tPath, nullptr,
                                       CGRectMake(thumbX - thumbR, thumbY - thumbR, thumbR * 2.0, thumbR * 2.0));
                thumb.path = tPath;
                CGPathRelease(tPath);
                [contentLayer addSublayer:thumb];
            } break;

            case UiLayoutNodeType::AnimSlot: {
                const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component);
                if (!anim) {
                    break;
                }
                // Анимационный слот рисуем квадратом 128x128 (или максимально возможным,
                // если контейнер меньше) и центрируем внутри выделенной зоны layout.
                const CGFloat availW = std::max<CGFloat>(0.0, rect.size.width - 8.0);
                const CGFloat availH = std::max<CGFloat>(0.0, rect.size.height - 8.0);
                const CGFloat side = std::max<CGFloat>(24.0, std::min<CGFloat>(128.0, std::min<CGFloat>(availW, availH)));
                const CGFloat drawX = rect.origin.x + (rect.size.width - side) * 0.5;
                CGFloat drawY = rect.origin.y + (rect.size.height - side) * 0.5;
                switch (box.node->align) {
                    case UiLayoutAlign::Start:
                        drawY = rect.origin.y + std::max<CGFloat>(0.0, rect.size.height - side - 2.0);
                        break;
                    case UiLayoutAlign::End:
                        drawY = rect.origin.y + 2.0;
                        break;
                    case UiLayoutAlign::Center:
                    default:
                        break;
                }
                const CGRect drawRect = CGRectMake(drawX, drawY, side, side);

                CAShapeLayer* frame = [CAShapeLayer layer];
                frame.frame = contentLayer.bounds;
                frame.fillColor = [NSColor clearColor].CGColor;
                frame.strokeColor = ctx.mid.CGColor;
                frame.lineWidth = 1.1;
                CGMutablePathRef p = CGPathCreateMutable();
                CGPathAddRoundedRect(p, nullptr, drawRect, 4.0, 4.0);
                frame.path = p;
                CGPathRelease(p);
                [contentLayer addSublayer:frame];

                if (!anim->label.empty()) {
                    NSString* title = [NSString stringWithUTF8String:anim->label.c_str()];
                    textLayer(CGRectMake(drawRect.origin.x + 6.0, drawRect.origin.y + 4.0, drawRect.size.width - 8.0, 14.0),
                              title,
                              ctx.mid,
                              ctx.bodyFont,
                              kCAAlignmentLeft,
                              false,
                              nullptr);
                }
            } break;

            case UiLayoutNodeType::TrackView:
            case UiLayoutNodeType::ManagerView:
            case UiLayoutNodeType::FxListView:
            case UiLayoutNodeType::FxEditorView:
            case UiLayoutNodeType::Column:
            case UiLayoutNodeType::Row:
            case UiLayoutNodeType::Unknown:
            default:
                break;
        }
    }
}

} // namespace avantgarde::macos
