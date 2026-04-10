#include "platform/macos/MacPrimitiveScenePainter.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/ui/components/UiComponents.h"
#include "platform/render/PreparedLayoutUtils.h"
#include "platform/render/RenderGeometry.h"
#include "platform/render/TextSliceGeometry.h"
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

std::vector<std::string> buildImagePathCandidates(std::string_view specUtf8,
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
    // Короткий формат: "images/foo.png" -> "assets/images/foo.png".
    if (specUtf8.rfind("images/", 0) == 0U) {
        pushUnique(cwd / "assets" / spec);
    }
    // Частый формат: "icons/foo.png" -> "assets/icons/foo.png".
    if (specUtf8.rfind("icons/", 0) == 0U) {
        pushUnique(cwd / "assets" / spec);
    }
    // Если указали только имя файла, попробуем assets/images/<name>.
    if (spec.parent_path().empty()) {
        pushUnique(cwd / "assets" / "images" / spec);
    }
    return out;
}

NSImage* loadIconImageCached(std::string_view specUtf8, std::string_view cwdUtf8) {
    if (specUtf8.empty()) {
        return nil;
    }
    static std::unordered_map<std::string, NSImage*> cache{};
    const std::string cacheKey(specUtf8);
    if (const auto it = cache.find(cacheKey); it != cache.end()) {
        return it->second;
    }
    NSImage* loaded = nil;
    const std::vector<std::string> candidates = buildImagePathCandidates(specUtf8, cwdUtf8);
    for (const std::string& candidate : candidates) {
        NSString* path = [NSString stringWithUTF8String:candidate.c_str()];
        if (!path) {
            continue;
        }
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:path];
        if (!img) {
            continue;
        }
        loaded = img; // Храним в статическом cache до конца процесса.
        break;
    }
    cache.emplace(cacheKey, loaded);
    return loaded;
}

bool parseHexColorSpec(std::string_view raw, CGFloat& r, CGFloat& g, CGFloat& b, CGFloat& a) {
    std::string s(raw);
    if (s.empty()) {
        return false;
    }
    if (s[0] == '#') {
        s.erase(s.begin());
    }
    const auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    const auto hexByte = [&](char hi, char lo) -> int {
        const int h = hexVal(hi);
        const int l = hexVal(lo);
        if (h < 0 || l < 0) return -1;
        return (h << 4) | l;
    };

    int rr = 0, gg = 0, bb = 0, aa = 255;
    if (s.size() == 3U || s.size() == 4U) {
        const int r0 = hexVal(s[0]);
        const int g0 = hexVal(s[1]);
        const int b0 = hexVal(s[2]);
        if (r0 < 0 || g0 < 0 || b0 < 0) return false;
        rr = (r0 << 4) | r0;
        gg = (g0 << 4) | g0;
        bb = (b0 << 4) | b0;
        if (s.size() == 4U) {
            const int a0 = hexVal(s[3]);
            if (a0 < 0) return false;
            aa = (a0 << 4) | a0;
        }
    } else if (s.size() == 6U || s.size() == 8U) {
        rr = hexByte(s[0], s[1]);
        gg = hexByte(s[2], s[3]);
        bb = hexByte(s[4], s[5]);
        if (rr < 0 || gg < 0 || bb < 0) return false;
        if (s.size() == 8U) {
            aa = hexByte(s[6], s[7]);
            if (aa < 0) return false;
        }
    } else {
        return false;
    }

    r = static_cast<CGFloat>(rr) / 255.0;
    g = static_cast<CGFloat>(gg) / 255.0;
    b = static_cast<CGFloat>(bb) / 255.0;
    a = static_cast<CGFloat>(aa) / 255.0;
    return true;
}

NSColor* resolveColorSpec(std::string_view raw, NSColor* fallback) {
    if (raw.empty()) {
        return fallback;
    }
    CGFloat r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    if (!parseHexColorSpec(raw, r, g, b, a)) {
        return fallback;
    }
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
}

const UiLayoutNode::StateSpec* nodeStateStyle(const UiLayoutNode* node,
                                              const IUiComponent* component) {
    if (!node || !component) {
        return nullptr;
    }
    switch (component->visualState()) {
        case IUiComponent::VisualState::Disabled:
            return &node->disabled;
        case IUiComponent::VisualState::Inactive:
            return &node->inactive;
        case IUiComponent::VisualState::Active:
        default:
            return &node->active;
    }
}

NSColor* nodeTextColor(const UiLayoutNode* node,
                       const IUiComponent* component,
                       NSColor* fallback) {
    if (!node) {
        return fallback;
    }
    const UiLayoutNode::StateSpec* st = nodeStateStyle(node, component);
    if (st && !st->textColor.empty()) {
        return resolveColorSpec(st->textColor, fallback);
    }
    return resolveColorSpec(node->textColor, fallback);
}

NSColor* nodeBorderColor(const UiLayoutNode* node,
                         const IUiComponent* component,
                         NSColor* fallback) {
    if (!node) {
        return fallback;
    }
    const UiLayoutNode::StateSpec* st = nodeStateStyle(node, component);
    if (st && !st->borderColor.empty()) {
        return resolveColorSpec(st->borderColor, fallback);
    }
    return resolveColorSpec(node->borderColor, fallback);
}

NSColor* nodeBackgroundColor(const UiLayoutNode* node,
                             const IUiComponent* component,
                             NSColor* fallback) {
    if (!node) {
        return fallback;
    }
    const UiLayoutNode::StateSpec* st = nodeStateStyle(node, component);
    if (st && !st->backgroundColor.empty()) {
        return resolveColorSpec(st->backgroundColor, fallback);
    }
    return resolveColorSpec(node->backgroundColor, fallback);
}

std::string toLowerAscii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

float textHashToValue01(std::string_view text) {
    const std::size_t h = std::hash<std::string_view>{}(text);
    const uint16_t bucket = static_cast<uint16_t>(h & 0xFFFFU);
    return static_cast<float>(bucket) / 65535.0f;
}

struct NodeTextFxChain {
    std::vector<VisualFxRequest> requests{};
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

    auto fontForNode = [&](const UiLayoutNode* node,
                           const IUiComponent* component,
                           NSFont* fallback) -> NSFont* {
        if (!node) {
            return fallback;
        }
        const UiLayoutNode::StateSpec* stateStyle = nodeStateStyle(node, component);
        const CGFloat baseSize = (fallback ? fallback.pointSize : 12.0);
        const float preferredSize = (stateStyle && stateStyle->fontSize > 0.0f)
                                        ? stateStyle->fontSize
                                        : node->fontSize;
        const CGFloat size = (preferredSize > 0.0f)
                                 ? std::clamp<CGFloat>(preferredSize, 6.0, 64.0)
                                 : baseSize;
        const std::string& preferredFont = (stateStyle && !stateStyle->font.empty())
                                               ? stateStyle->font
                                               : node->font;
        if (preferredFont.empty()) {
            return resizedFont(fallback, size);
        }
        const std::string fontSpec = preferredFont;
        const std::string fontAlias = toLowerAscii(fontSpec);
        if (fontAlias == "default") {
            return resizedFont(fallback, size);
        }
        if (fontAlias == "gothic") {
            return resizedFont(ctx.gothicFont ? ctx.gothicFont : fallback, size);
        }
        if (fontAlias == "body") {
            return resizedFont(ctx.bodyFont ? ctx.bodyFont : fallback, size);
        }

        const std::string cacheKey = fontSpec + "|" + std::to_string(static_cast<int>(std::lround(size)));
        if (ctx.dynamicFontCache) {
            if (const auto it = ctx.dynamicFontCache->find(cacheKey); it != ctx.dynamicFontCache->end()) {
                return it->second ? it->second : fallback;
            }
        }

        // 1) Пробуем как PostScript имя.
        NSFont* loaded = [NSFont fontWithName:[NSString stringWithUTF8String:fontSpec.c_str()] size:size];

        // 2) Пробуем как путь к font-файлу.
        if (!loaded && hasFontExtensionUtf8(fontSpec)) {
            const std::vector<std::string> candidates = buildFontPathCandidates(fontSpec, ctx.cwd);
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

    auto textFxChainForNode = [&](const UiLayoutNode* node, const IUiComponent* component) -> NodeTextFxChain {
        NodeTextFxChain chain{};
        if (!node || !ctx.visualFx) {
            return chain;
        }
        const auto selectEffects = [&](const UiLayoutNode& n, const IUiComponent* c)
            -> const std::vector<UiLayoutNode::EffectSpec>& {
            const UiLayoutNode::StateSpec* st = nodeStateStyle(&n, c);
            if (st && !st->effects.empty()) {
                return st->effects;
            }
            return n.effects;
        };
        const std::vector<UiLayoutNode::EffectSpec>& effectSpecs = selectEffects(*node, component);
        if (effectSpecs.empty()) {
            return chain;
        }

        VisualFxRequest base{};
        base.nodeId = node->id;
        base.instanceKey = component ? std::string(component->id()) : std::string{};
        base.nodeText = node->text;
        base.nowMs = nowMs;

        if (const auto* knob = dynamic_cast<const UiKnobComponent*>(component)) {
            base.hasValue01 = true;
            base.value01 = knob->value01;
            if (!knob->label.empty()) {
                base.nodeText = knob->label;
            }
        } else if (const auto* sw = dynamic_cast<const UiSwitchComponent*>(component)) {
            const uint16_t denom = std::max<uint16_t>(
                1U,
                static_cast<uint16_t>(sw->options.size() > 1U ? sw->options.size() - 1U : 1U));
            base.hasValue01 = true;
            base.value01 = static_cast<float>(sw->selectedIndex) / static_cast<float>(denom);
            if (!sw->label.empty()) {
                base.nodeText = sw->label;
            }
        } else if (const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component)) {
            base.hasValue01 = true;
            base.value01 = anim->intensity01;
            if (!anim->label.empty()) {
                base.nodeText = anim->label;
            }
        } else if (const auto* icon = dynamic_cast<const UiIconComponent*>(component)) {
            if (!icon->path.empty()) {
                // Для trigger=change на иконках считаем source-путь "значением" узла.
                // Тогда смена иконки (или ее динамического bind-пути) поднимет FX-триггер.
                base.hasValue01 = true;
                base.value01 = textHashToValue01(icon->path);
                base.nodeText = icon->path;
            }
        } else if (const auto* t = dynamic_cast<const UiTextComponent*>(component)) {
            if (!t->text.empty()) {
                base.hasValue01 = true;
                base.value01 = textHashToValue01(t->text);
                base.nodeText = t->text;
            }
        } else if (const auto* s = dynamic_cast<const UiStatusBarComponent*>(component)) {
            if (!s->text.empty()) {
                base.hasValue01 = true;
                base.value01 = textHashToValue01(s->text);
                base.nodeText = s->text;
            }
        } else if (!node->text.empty()) {
            base.hasValue01 = true;
            base.value01 = textHashToValue01(node->text);
            base.nodeText = node->text;
        }

        for (const UiLayoutNode::EffectSpec& spec : effectSpecs) {
            VisualFxRequest request = base;
            request.effect = spec.type;
            request.effectColor = spec.effectColor;
            request.effectTrigger = spec.effectTrigger;
            request.effectTransition = spec.effectTransition;
            request.effectTriggerOutMs = spec.effectTriggerOutMs;
            request.effectIntervalMs = spec.effectIntervalMs;
            request.effectAmount = spec.effectAmount;
            request.effectSpeed = spec.effectSpeed;
            if (!request.effect.empty()) {
                chain.requests.push_back(std::move(request));
            }
        }
        return chain;
    };

    const UiLayoutNode* rootNode = &prepared.layoutTemplate->root;
    NSColor* sceneDefaultTextColor = resolveColorSpec(rootNode ? rootNode->defaultTextColor : "", ctx.text);
    NSColor* frameBorderColor = nodeBorderColor(rootNode, nullptr, ctx.mid);
    NSColor* frameBackgroundColor = nodeBackgroundColor(rootNode, nullptr, [NSColor clearColor]);

    // Внешняя рамка кадра.
    CGRect frameRectPx = CGRectZero;
    {
        CAShapeLayer* frame = [CAShapeLayer layer];
        frame.frame = root.bounds;
        frame.fillColor = frameBackgroundColor.CGColor;
        frame.strokeColor = frameBorderColor.CGColor;
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
                         const NodeTextFxChain* fxChain,
                         CGFloat componentOpacity) {
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
            t.opacity = static_cast<float>(std::clamp(opacity * componentOpacity, 0.0, 1.0));
            return t;
        };
        auto pushTextLayer = [&](NSString* drawText,
                                 const CGRect& layerRect,
                                 NSColor* layerColor,
                                 CGFloat opacity) {
            [contentLayer addSublayer:makeTextLayer(drawText, layerRect, layerColor, opacity)];
        };
        auto buildImageWithRgbaFx = [&](NSString* drawText,
                                        const CGRect& drawRect,
                                        const std::vector<VisualFxRequest>& requests) -> CGImageRef {
            const std::size_t pixelW =
                static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(drawRect.size.width)));
            const std::size_t pixelH =
                static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(drawRect.size.height)));
            const std::size_t stride = pixelW * 4U;

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            const CGBitmapInfo bitmapInfo =
                static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
            CGContextRef bmp = CGBitmapContextCreate(nullptr,
                                                     static_cast<size_t>(pixelW),
                                                     static_cast<size_t>(pixelH),
                                                     8,
                                                     static_cast<size_t>(stride),
                                                     cs,
                                                     bitmapInfo);
            CGColorSpaceRelease(cs);
            if (!bmp) {
                return nullptr;
            }

            CATextLayer* source = makeTextLayer(drawText,
                                                CGRectMake(0.0, 0.0, static_cast<CGFloat>(pixelW), static_cast<CGFloat>(pixelH)),
                                                color,
                                                1.0);
            source.contentsScale = 1.0;
            [source renderInContext:bmp];

            auto* px = static_cast<uint8_t*>(CGBitmapContextGetData(bmp));
            if (!px) {
                CGContextRelease(bmp);
                return nullptr;
            }

            VisualFxRgbaView roi{};
            roi.pixels = px;
            roi.width = static_cast<uint16_t>(std::min<std::size_t>(pixelW, 65535U));
            roi.height = static_cast<uint16_t>(std::min<std::size_t>(pixelH, 65535U));
            roi.strideBytes = static_cast<uint32_t>(std::min<std::size_t>(stride, 0xFFFFFFFFU));

            if (!requests.empty()) {
                const std::size_t byteCount = static_cast<std::size_t>(roi.strideBytes) * roi.height;
                std::vector<uint8_t> original(byteCount, 0U);
                std::copy(roi.pixels, roi.pixels + byteCount, original.begin());
                std::vector<int32_t> accum(byteCount, 0);
                for (std::size_t i = 0; i < byteCount; ++i) {
                    accum[i] = static_cast<int32_t>(original[i]);
                }
                bool anyApplied = false;
                for (const VisualFxRequest& req : requests) {
                    std::vector<uint8_t> tmp = original;
                    VisualFxRgbaView tmpView{};
                    tmpView.pixels = tmp.data();
                    tmpView.width = roi.width;
                    tmpView.height = roi.height;
                    tmpView.strideBytes = roi.strideBytes;
                    if (!ctx.visualFx->applyRgba(tmpView, req)) {
                        continue;
                    }
                    anyApplied = true;
                    for (std::size_t i = 0; i < byteCount; ++i) {
                        accum[i] += static_cast<int32_t>(tmp[i]) - static_cast<int32_t>(original[i]);
                    }
                }
                if (anyApplied) {
                    for (std::size_t i = 0; i < byteCount; ++i) {
                        roi.pixels[i] = static_cast<uint8_t>(std::clamp(accum[i], 0, 255));
                    }
                }
            }

            CGImageRef image = CGBitmapContextCreateImage(bmp);
            CGContextRelease(bmp);
            return image;
        };

        auto drawSegment = [&](NSString* drawText, const CGRect& drawRect) {
            if (!fxChain || fxChain->requests.empty() || !ctx.visualFx) {
                pushTextLayer(drawText, drawRect, color, 1.0);
                return;
            }

            // Рендерер не знает имен эффектов.
            // Он только: 1) запрашивает геометрический style у FX-процессора,
            // 2) применяет универсальную геометрию, 3) для остальных эффектов
            // оставляет pixel-postprocess pipeline.
            std::vector<VisualFxRequest> rgbaRequests{};
            std::vector<render::TextGlitchPlan> geometryPlans{};
            bool hasReveal = false;
            float reveal01 = 1.0f;
            for (const VisualFxRequest& req : fxChain->requests) {
                const VisualFxTextRevealStyle rv = ctx.visualFx->resolveTextRevealStyle(req);
                if (rv.active) {
                    hasReveal = true;
                    reveal01 = std::min(reveal01, rv.reveal01);
                    continue;
                }
                const VisualFxTextGeometryStyle geo = ctx.visualFx->resolveTextGeometryStyle(req);
                if (geo.active) {
                    geometryPlans.push_back(
                        render::buildTextGlitchPlan(geo, static_cast<float>(drawRect.size.height)));
                    continue;
                }
                rgbaRequests.push_back(req);
            }

            NSString* effectiveText = drawText;
            if (hasReveal) {
                const NSUInteger len = [drawText length];
                const NSUInteger visible = std::min<NSUInteger>(
                    len,
                    static_cast<NSUInteger>(std::floor(static_cast<double>(len) *
                                                       static_cast<double>(std::clamp(reveal01, 0.0f, 1.0f)) +
                                                       1e-6)));
                effectiveText = [drawText substringToIndex:visible];
            }

            if (!geometryPlans.empty()) {
                CGImageRef processedImage = nullptr;
                if (!rgbaRequests.empty()) {
                    processedImage = buildImageWithRgbaFx(effectiveText, drawRect, rgbaRequests);
                }
                for (const render::TextGlitchPlan& plan : geometryPlans) {
                    if (!plan.active) {
                        continue;
                    }
                    const CGRect base = CGRectOffset(drawRect, static_cast<CGFloat>(plan.baseOffsetX), 0.0);
                    if (!processedImage) {
                        if (plan.splitPx > 0.01f) {
                            NSColor* splitA = [ctx.mid colorWithAlphaComponent:0.42];
                            NSColor* splitB = [ctx.text colorWithAlphaComponent:0.26];
                            pushTextLayer(effectiveText,
                                          CGRectOffset(base, static_cast<CGFloat>(-plan.splitPx), 0.0),
                                          splitA,
                                          1.0);
                            pushTextLayer(effectiveText,
                                          CGRectOffset(base, static_cast<CGFloat>(plan.splitPx), 0.0),
                                          splitB,
                                          1.0);
                        }
                        pushTextLayer(effectiveText, base, color, static_cast<CGFloat>(plan.alpha));
                    } else {
                        auto pushImageLayer = [&](const CGRect& lr, CGFloat opacity) {
                            CALayer* layer = [CALayer layer];
                            layer.frame = lr;
                            layer.contentsGravity = kCAGravityResize;
                            layer.contents = (__bridge id)processedImage;
                            layer.contentsScale = std::max<CGFloat>(scale, 1.0);
                            layer.opacity = static_cast<float>(std::clamp(opacity * componentOpacity, 0.0, 1.0));
                            [contentLayer addSublayer:layer];
                        };
                        if (plan.splitPx > 0.01f) {
                            pushImageLayer(CGRectOffset(base, static_cast<CGFloat>(-plan.splitPx), 0.0), 0.42);
                            pushImageLayer(CGRectOffset(base, static_cast<CGFloat>(plan.splitPx), 0.0), 0.26);
                        }
                        pushImageLayer(base, static_cast<CGFloat>(plan.alpha));
                    }

                    for (const render::TextSliceSpan& span : plan.slices) {
                        const CGRect shifted = CGRectOffset(base, static_cast<CGFloat>(span.offsetX), 0.0);
                        CALayer* layer = nil;
                        if (!processedImage) {
                            layer = makeTextLayer(effectiveText, shifted, color, 1.0);
                        } else {
                            layer = [CALayer layer];
                            layer.frame = shifted;
                            layer.contentsGravity = kCAGravityResize;
                            layer.contents = (__bridge id)processedImage;
                            layer.contentsScale = std::max<CGFloat>(scale, 1.0);
                        }
                        CAShapeLayer* sliceMask = [CAShapeLayer layer];
                        sliceMask.frame = layer.bounds;
                        CGMutablePathRef mp = CGPathCreateMutable();
                        CGPathAddRect(mp,
                                      nullptr,
                                      CGRectMake(0.0,
                                                 static_cast<CGFloat>(span.y),
                                                 drawRect.size.width,
                                                 std::max<CGFloat>(1.0, static_cast<CGFloat>(span.height))));
                        sliceMask.path = mp;
                        CGPathRelease(mp);
                        layer.mask = sliceMask;
                        [contentLayer addSublayer:layer];
                    }
                }
                if (processedImage) {
                    CGImageRelease(processedImage);
                }
                // Если геометрия активна, текст рисуем только ей (без bitmap-пайплайна).
                return;
            }

            if (rgbaRequests.empty()) {
                pushTextLayer(effectiveText, drawRect, color, 1.0);
                return;
            }
            CGImageRef image = buildImageWithRgbaFx(effectiveText, drawRect, rgbaRequests);
            if (!image) {
                pushTextLayer(effectiveText, drawRect, color, 1.0);
                return;
            }

            CALayer* layer = [CALayer layer];
            layer.frame = drawRect;
            layer.contentsGravity = kCAGravityResize;
            layer.contents = (__bridge id)image;
            layer.contentsScale = std::max<CGFloat>(scale, 1.0);
            layer.opacity = static_cast<float>(std::clamp(componentOpacity, 0.0, 1.0));
            [contentLayer addSublayer:layer];
            CGImageRelease(image);
        };

        // Для декоративных шрифтов с нулевым space-глифом рисуем слова по отдельности
        // и задаем межсловный интервал вручную.
        bool manualWordSpacing = false;
        if (font && [text rangeOfString:@" "].location != NSNotFound) {
            NSDictionary* attrs = @{NSFontAttributeName : font};
            const CGFloat spaceW = [@" " sizeWithAttributes:attrs].width;
            manualWordSpacing = (spaceW <= 0.5);
        }
        bool forceWholeSegment = false;
        if (fxChain && ctx.visualFx) {
            for (const VisualFxRequest& req : fxChain->requests) {
                if (ctx.visualFx->requiresWholeTextPass(req)) {
                    forceWholeSegment = true;
                    break;
                }
            }
        }

        if (!manualWordSpacing || wrapText || forceWholeSegment) {
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
        if (component && !component->isVisible()) {
            continue;
        }
        const CGFloat componentOpacity = static_cast<CGFloat>(
            std::clamp(component ? component->opacity() : 1.0f, 0.0f, 1.0f));

        const CGRect rect = pxRectForChars(static_cast<int16_t>(box.rect.x + 1),
                                           static_cast<int16_t>(box.rect.y + 1),
                                           box.rect.width,
                                           box.rect.height);

        switch (box.node->type) {
            case UiLayoutNodeType::StatusBar:
            case UiLayoutNodeType::Text: {
                const NodeTextFxChain fxChain = textFxChainForNode(box.node, component);
                NSString* text = nil;
                NSFont* textFont = fontForNode(box.node, component, ctx.bodyFont);
                NSColor* textColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
                CGRect textRect = rect;
                const bool wrapText = box.node->textWrap;
                if (const auto* s = dynamic_cast<const UiStatusBarComponent*>(component)) {
                    text = [NSString stringWithUTF8String:s->text.c_str()];
                    const bool isHeaderTitle = (box.node->id == "header_title");
                    const CGRect panelRect = rect;
                    CALayer* panel = [CALayer layer];
                    panel.frame = panelRect;
                    NSColor* panelBg = nil;
                    if (isHeaderTitle) {
                        panelBg = [ctx.mid colorWithAlphaComponent:0.22];
                    } else {
                        panelBg = ctx.panel;
                    }
                    panelBg = nodeBackgroundColor(box.node, component, panelBg);
                    panel.backgroundColor = panelBg.CGColor;
                    panel.opacity = static_cast<float>(componentOpacity);
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
                            topFill.backgroundColor = panelBg.CGColor;
                            topFill.opacity = static_cast<float>(componentOpacity);
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
                textLayer(textRect, text, textColor, textFont, kCAAlignmentLeft, wrapText, &fxChain, componentOpacity);
            } break;

            case UiLayoutNodeType::Separator: {
                CALayer* line = [CALayer layer];
                NSColor* lineColor = nodeBorderColor(box.node, component, ctx.mid);
                line.backgroundColor = lineColor.CGColor;
                line.frame = CGRectMake(rect.origin.x, rect.origin.y + rect.size.height * 0.5, rect.size.width, 1.0);
                line.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:line];
            } break;

            case UiLayoutNodeType::List:
            case UiLayoutNodeType::Spacer: {
                const auto* list = dynamic_cast<const UiListComponent*>(component);
                if (!list) {
                    break;
                }
                NSColor* listTextColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
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
                    textLayer(rr, rowText, listTextColor, ctx.bodyFont, kCAAlignmentLeft, false, nullptr, componentOpacity);
                }
            } break;

            case UiLayoutNodeType::Knob: {
                const auto* knob = dynamic_cast<const UiKnobComponent*>(component);
                if (!knob) {
                    break;
                }
                NSColor* knobTextColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
                NSColor* knobBorderColor = nodeBorderColor(box.node, component, knobTextColor);
                NSFont* labelFont = fontForNode(box.node, component, ctx.bodyFont);
                const CGFloat labelH = knob->label.empty()
                                           ? 0.0
                                           : std::max<CGFloat>(12.0, (labelFont ? labelFont.pointSize * 1.15 : 12.0));
                const CGFloat knobAreaY = rect.origin.y;
                const CGFloat knobAreaH = std::max<CGFloat>(12.0, rect.size.height - labelH - (labelH > 0.0 ? 2.0 : 0.0));
                const CGFloat maxSide = std::max<CGFloat>(8.0, std::min<CGFloat>(knobAreaH - 1.0, rect.size.width - 6.0));
                const CGFloat baseSide = std::max<CGFloat>(14.0, std::min<CGFloat>(30.0, maxSide));
                CGFloat knobScaleValue = (box.node->knobSize > 0.0f)
                                             ? static_cast<CGFloat>(box.node->knobSize)
                                             : 1.0;
                if (const UiLayoutNode::StateSpec* st = nodeStateStyle(box.node, component);
                    st && st->knobSize > 0.0f) {
                    knobScaleValue = static_cast<CGFloat>(st->knobSize);
                }
                const CGFloat knobScale = std::clamp<CGFloat>(knobScaleValue, 0.2, 4.0);
                const CGFloat sz = std::clamp(baseSide * knobScale, 8.0, maxSide);
                const CGFloat r = sz * 0.42;
                CGFloat cx = rect.origin.x + rect.size.width * 0.5;
                switch (box.node->align) {
                    case UiLayoutAlign::Start:
                        cx = rect.origin.x + r + 2.0;
                        break;
                    case UiLayoutAlign::End:
                        cx = rect.origin.x + rect.size.width - r - 2.0;
                        break;
                    case UiLayoutAlign::Center:
                    default:
                        break;
                }
                cx = std::clamp<CGFloat>(cx, rect.origin.x + r + 1.0, rect.origin.x + rect.size.width - r - 1.0);
                const CGFloat cy = knobAreaY + knobAreaH * 0.5;

                CAShapeLayer* ring = [CAShapeLayer layer];
                ring.frame = contentLayer.bounds;
                ring.fillColor = [NSColor clearColor].CGColor;
                ring.strokeColor = knobBorderColor.CGColor;
                ring.lineWidth = 1.7;
                CGMutablePathRef ringPath = CGPathCreateMutable();
                CGPathAddEllipseInRect(ringPath, nullptr, CGRectMake(cx - r, cy - r, r * 2.0, r * 2.0));
                ring.path = ringPath;
                CGPathRelease(ringPath);
                ring.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:ring];

                const CGFloat v = std::clamp(knob->value01, 0.0f, 1.0f);
                const CGFloat a = (225.0 - v * 270.0) * static_cast<CGFloat>(M_PI / 180.0);
                const CGFloat nx = cx + std::cos(a) * r * 0.85;
                const CGFloat ny = cy + std::sin(a) * r * 0.85;

                CAShapeLayer* needle = [CAShapeLayer layer];
                needle.frame = contentLayer.bounds;
                needle.fillColor = [NSColor clearColor].CGColor;
                needle.strokeColor = knobBorderColor.CGColor;
                needle.lineWidth = 1.8;
                CGMutablePathRef nPath = CGPathCreateMutable();
                CGPathMoveToPoint(nPath, nullptr, cx, cy);
                CGPathAddLineToPoint(nPath, nullptr, nx, ny);
                needle.path = nPath;
                CGPathRelease(nPath);
                needle.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:needle];

                if (!knob->label.empty()) {
                    const NodeTextFxChain fxChain = textFxChainForNode(box.node, component);
                    NSString* label = [NSString stringWithUTF8String:knob->label.c_str()];
                    NSString* alignMode = kCAAlignmentCenter;
                    switch (box.node->align) {
                        case UiLayoutAlign::Start: alignMode = kCAAlignmentLeft; break;
                        case UiLayoutAlign::End: alignMode = kCAAlignmentRight; break;
                        case UiLayoutAlign::Center:
                        default:
                            alignMode = kCAAlignmentCenter;
                            break;
                    }
                    const CGRect lr = CGRectMake(rect.origin.x + 1.0,
                                                 rect.origin.y + rect.size.height - labelH,
                                                 std::max<CGFloat>(8.0, rect.size.width - 2.0),
                                                 labelH);
                    textLayer(lr, label, knobTextColor, labelFont, alignMode, false, &fxChain, componentOpacity);
                }
            } break;

            case UiLayoutNodeType::Switch: {
                const auto* sw = dynamic_cast<const UiSwitchComponent*>(component);
                if (!sw) {
                    break;
                }
                NSColor* switchTextColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
                NSColor* switchBorderColor = nodeBorderColor(box.node, component, switchTextColor);
                NSColor* switchBgColor = nodeBackgroundColor(box.node, component, ctx.panel);
                NSFont* labelFont = fontForNode(box.node, component, ctx.gothicFont);
                const CGFloat labelH = sw->label.empty()
                                           ? 0.0
                                           : std::max<CGFloat>(14.0, (labelFont ? labelFont.pointSize * 1.2 : 14.0));
                if (!sw->label.empty()) {
                    const NodeTextFxChain fxChain = textFxChainForNode(box.node, component);
                    NSString* label = [NSString stringWithUTF8String:sw->label.c_str()];
                    const CGRect lr = CGRectMake(rect.origin.x, rect.origin.y, rect.size.width, labelH);
                    textLayer(lr, label, switchTextColor, labelFont, kCAAlignmentLeft, false, &fxChain, componentOpacity);
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
                body.fillColor = switchBgColor.CGColor;
                body.strokeColor = switchBorderColor.CGColor;
                body.lineWidth = 1.4;
                CGMutablePathRef bPath = CGPathCreateMutable();
                CGPathAddRoundedRect(bPath, nullptr, CGRectMake(x, y, w, h), radius, radius);
                body.path = bPath;
                CGPathRelease(bPath);
                body.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:body];

                const uint16_t total = std::max<uint16_t>(2U, static_cast<uint16_t>(sw->options.empty() ? 2U : sw->options.size()));
                const uint16_t idx = std::min<uint16_t>(sw->selectedIndex, static_cast<uint16_t>(total - 1U));
                const CGFloat t = static_cast<CGFloat>(idx) / static_cast<CGFloat>(total - 1U);
                const CGFloat thumbR = h * 0.38;
                const CGFloat thumbX = x + thumbR + (w - thumbR * 2.0) * t;
                const CGFloat thumbY = y + h * 0.5;

                CAShapeLayer* thumb = [CAShapeLayer layer];
                thumb.frame = contentLayer.bounds;
                thumb.fillColor = switchTextColor.CGColor;
                CGMutablePathRef tPath = CGPathCreateMutable();
                CGPathAddEllipseInRect(tPath, nullptr,
                                       CGRectMake(thumbX - thumbR, thumbY - thumbR, thumbR * 2.0, thumbR * 2.0));
                thumb.path = tPath;
                CGPathRelease(tPath);
                thumb.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:thumb];
            } break;

            case UiLayoutNodeType::Icon: {
                const auto* icon = dynamic_cast<const UiIconComponent*>(component);
                if (!icon || icon->path.empty()) {
                    break;
                }
                const CGRect drawRect = CGRectInset(rect, 1.0, 1.0);
                if (drawRect.size.width < 2.0 || drawRect.size.height < 2.0) {
                    break;
                }
                NSImage* img = loadIconImageCached(icon->path, ctx.cwd);
                if (!img) {
                    break;
                }
                CGImageRef cg = [img CGImageForProposedRect:nullptr context:nil hints:nil];
                if (!cg) {
                    break;
                }
                const NodeTextFxChain fxChain = textFxChainForNode(box.node, component);
                if (!fxChain.requests.empty() && ctx.visualFx) {
                    const std::size_t pixelW =
                        static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(drawRect.size.width)));
                    const std::size_t pixelH =
                        static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(drawRect.size.height)));
                    const std::size_t stride = pixelW * 4U;
                    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                    const CGBitmapInfo bitmapInfo =
                        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
                    CGContextRef bmp = CGBitmapContextCreate(nullptr,
                                                             static_cast<size_t>(pixelW),
                                                             static_cast<size_t>(pixelH),
                                                             8,
                                                             static_cast<size_t>(stride),
                                                             cs,
                                                             bitmapInfo);
                    CGColorSpaceRelease(cs);
                    if (bmp) {
                        CGContextSetInterpolationQuality(bmp, kCGInterpolationNone);
                        CALayer* source = [CALayer layer];
                        source.frame = CGRectMake(0.0, 0.0, static_cast<CGFloat>(pixelW), static_cast<CGFloat>(pixelH));
                        source.contents = (__bridge id)cg;
                        source.contentsGravity = kCAGravityResizeAspect;
                        source.minificationFilter = kCAFilterNearest;
                        source.magnificationFilter = kCAFilterNearest;
                        source.contentsScale = 1.0;
                        [source renderInContext:bmp];

                        auto* px = static_cast<uint8_t*>(CGBitmapContextGetData(bmp));
                        if (px) {
                            VisualFxRgbaView roi{};
                            roi.pixels = px;
                            roi.width = static_cast<uint16_t>(std::min<std::size_t>(pixelW, 65535U));
                            roi.height = static_cast<uint16_t>(std::min<std::size_t>(pixelH, 65535U));
                            roi.strideBytes = static_cast<uint32_t>(std::min<std::size_t>(stride, 0xFFFFFFFFU));

                            const std::size_t byteCount = static_cast<std::size_t>(roi.strideBytes) * roi.height;
                            std::vector<uint8_t> original(byteCount, 0U);
                            std::copy(roi.pixels, roi.pixels + byteCount, original.begin());
                            std::vector<int32_t> accum(byteCount, 0);
                            for (std::size_t i = 0; i < byteCount; ++i) {
                                accum[i] = static_cast<int32_t>(original[i]);
                            }
                            bool anyApplied = false;
                            for (const VisualFxRequest& req : fxChain.requests) {
                                std::vector<uint8_t> tmp = original;
                                VisualFxRgbaView tmpView{};
                                tmpView.pixels = tmp.data();
                                tmpView.width = roi.width;
                                tmpView.height = roi.height;
                                tmpView.strideBytes = roi.strideBytes;
                                if (!ctx.visualFx->applyRgba(tmpView, req)) {
                                    continue;
                                }
                                anyApplied = true;
                                for (std::size_t i = 0; i < byteCount; ++i) {
                                    accum[i] += static_cast<int32_t>(tmp[i]) - static_cast<int32_t>(original[i]);
                                }
                            }
                            if (anyApplied) {
                                for (std::size_t i = 0; i < byteCount; ++i) {
                                    roi.pixels[i] = static_cast<uint8_t>(std::clamp(accum[i], 0, 255));
                                }
                                CGImageRef processed = CGBitmapContextCreateImage(bmp);
                                if (processed) {
                                    CALayer* imageLayer = [CALayer layer];
                                    imageLayer.frame = drawRect;
                                    imageLayer.contents = (__bridge id)processed;
                                    imageLayer.contentsGravity = kCAGravityResizeAspect;
                                    imageLayer.minificationFilter = kCAFilterNearest;
                                    imageLayer.magnificationFilter = kCAFilterNearest;
                                    imageLayer.opacity = static_cast<float>(componentOpacity);
                                    [contentLayer addSublayer:imageLayer];
                                    CGImageRelease(processed);
                                    CGContextRelease(bmp);
                                    break;
                                }
                            }
                        }
                        CGContextRelease(bmp);
                    }
                }
                CALayer* imageLayer = [CALayer layer];
                imageLayer.frame = drawRect;
                imageLayer.contents = (__bridge id)cg;
                imageLayer.contentsGravity = kCAGravityResizeAspect;
                imageLayer.minificationFilter = kCAFilterNearest;
                imageLayer.magnificationFilter = kCAFilterNearest;
                imageLayer.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:imageLayer];
            } break;

            case UiLayoutNodeType::AnimSlot: {
                const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component);
                if (!anim) {
                    break;
                }
                NSColor* animBorderColor = nodeBorderColor(box.node, component, ctx.mid);
                NSColor* animTextColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
                // Анимационный слот рисуем квадратом 128x128 (или максимально возможным,
                // если контейнер меньше) и центрируем внутри выделенной зоны layout.
                const CGFloat availW = std::max<CGFloat>(0.0, rect.size.width - 8.0);
                const CGFloat availH = std::max<CGFloat>(0.0, rect.size.height - 8.0);
                const CGFloat side = std::max<CGFloat>(24.0, std::min<CGFloat>(128.0, std::min<CGFloat>(availW, availH)));
                CGFloat drawX = rect.origin.x + (rect.size.width - side) * 0.5;
                CGFloat drawY = rect.origin.y + (rect.size.height - side) * 0.5;
                switch (box.node->align) {
                    case UiLayoutAlign::Start:
                        drawX = rect.origin.x + 2.0;
                        drawY = rect.origin.y + std::max<CGFloat>(0.0, rect.size.height - side - 2.0);
                        break;
                    case UiLayoutAlign::End:
                        drawX = rect.origin.x + std::max<CGFloat>(2.0, rect.size.width - side - 2.0);
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
                frame.strokeColor = animBorderColor.CGColor;
                frame.lineWidth = 1.1;
                CGMutablePathRef p = CGPathCreateMutable();
                CGPathAddRoundedRect(p, nullptr, drawRect, 4.0, 4.0);
                frame.path = p;
                CGPathRelease(p);
                frame.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:frame];

                if (!anim->label.empty()) {
                    NSString* title = [NSString stringWithUTF8String:anim->label.c_str()];
                    textLayer(CGRectMake(drawRect.origin.x + 6.0, drawRect.origin.y + 4.0, drawRect.size.width - 8.0, 14.0),
                              title,
                              animTextColor,
                              ctx.bodyFont,
                              kCAAlignmentLeft,
                              false,
                              nullptr,
                              componentOpacity);
                }
            } break;

            case UiLayoutNodeType::Waveform: {
                const auto* wave = dynamic_cast<const UiWaveformComponent*>(component);
                if (!wave) {
                    break;
                }
                NSColor* waveTextColor = nodeTextColor(box.node, component, sceneDefaultTextColor);
                NSColor* waveBorderColor = nodeBorderColor(box.node, component, ctx.mid);
                NSColor* waveFillColor =
                    nodeBackgroundColor(box.node, component, [waveBorderColor colorWithAlphaComponent:0.10]);
                const CGRect drawRect = CGRectInset(rect, 2.0, 2.0);
                if (drawRect.size.width < 4.0 || drawRect.size.height < 4.0) {
                    break;
                }

                // Подсветка активного trim-региона.
                const float trimStart = std::clamp(wave->trimStart01, 0.0f, 0.99f);
                const float trimEnd = std::clamp(wave->trimEnd01, 0.01f, 1.0f);
                const CGFloat sx = drawRect.origin.x + drawRect.size.width * trimStart;
                const CGFloat ex = drawRect.origin.x + drawRect.size.width * std::max(trimStart, trimEnd);
                if (ex > sx + 0.5) {
                    CALayer* trimRegion = [CALayer layer];
                    trimRegion.frame = CGRectMake(sx, drawRect.origin.y, ex - sx, drawRect.size.height);
                    trimRegion.backgroundColor = waveFillColor.CGColor;
                    trimRegion.opacity = static_cast<float>(componentOpacity);
                    [contentLayer addSublayer:trimRegion];
                }

                // Центральная ось.
                const CGFloat centerY = drawRect.origin.y + drawRect.size.height * 0.5;
                CAShapeLayer* center = [CAShapeLayer layer];
                center.frame = contentLayer.bounds;
                center.fillColor = [NSColor clearColor].CGColor;
                center.strokeColor = [waveBorderColor colorWithAlphaComponent:0.55].CGColor;
                center.lineWidth = 1.0;
                CGMutablePathRef centerPath = CGPathCreateMutable();
                CGPathMoveToPoint(centerPath, nullptr, drawRect.origin.x, centerY);
                CGPathAddLineToPoint(centerPath, nullptr, drawRect.origin.x + drawRect.size.width, centerY);
                center.path = centerPath;
                CGPathRelease(centerPath);
                center.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:center];

                // Пиковая огибающая (вертикальные столбцы по X).
                if (!wave->peaks01.empty()) {
                    const std::size_t cols = std::max<std::size_t>(
                        1U,
                        static_cast<std::size_t>(std::floor(drawRect.size.width)));
                    const CGFloat halfRange = drawRect.size.height * 0.46;
                    CGMutablePathRef peaksPath = CGPathCreateMutable();
                    for (std::size_t x = 0; x < cols; ++x) {
                        const std::size_t idx = (x * wave->peaks01.size()) / cols;
                        const float v = std::clamp(wave->peaks01[std::min<std::size_t>(idx, wave->peaks01.size() - 1U)],
                                                   0.0f,
                                                   1.0f);
                        const CGFloat hh = std::max<CGFloat>(1.0, halfRange * static_cast<CGFloat>(v));
                        const CGFloat px = drawRect.origin.x + static_cast<CGFloat>(x) + 0.5;
                        CGPathMoveToPoint(peaksPath, nullptr, px, centerY - hh);
                        CGPathAddLineToPoint(peaksPath, nullptr, px, centerY + hh);
                    }
                    CAShapeLayer* peaksLayer = [CAShapeLayer layer];
                    peaksLayer.frame = contentLayer.bounds;
                    peaksLayer.fillColor = [NSColor clearColor].CGColor;
                    peaksLayer.strokeColor = [waveTextColor colorWithAlphaComponent:0.92].CGColor;
                    peaksLayer.lineWidth = 1.0;
                    peaksLayer.path = peaksPath;
                    CGPathRelease(peaksPath);
                    peaksLayer.opacity = static_cast<float>(componentOpacity);
                    [contentLayer addSublayer:peaksLayer];
                }

                // Маркеры trim start/end.
                CAShapeLayer* marks = [CAShapeLayer layer];
                marks.frame = contentLayer.bounds;
                marks.fillColor = [NSColor clearColor].CGColor;
                marks.strokeColor = waveTextColor.CGColor;
                marks.lineWidth = 1.1;
                CGMutablePathRef markPath = CGPathCreateMutable();
                CGPathMoveToPoint(markPath, nullptr, sx, drawRect.origin.y);
                CGPathAddLineToPoint(markPath, nullptr, sx, drawRect.origin.y + drawRect.size.height);
                CGPathMoveToPoint(markPath, nullptr, ex, drawRect.origin.y);
                CGPathAddLineToPoint(markPath, nullptr, ex, drawRect.origin.y + drawRect.size.height);
                marks.path = markPath;
                CGPathRelease(markPath);
                marks.opacity = static_cast<float>(componentOpacity);
                [contentLayer addSublayer:marks];
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
