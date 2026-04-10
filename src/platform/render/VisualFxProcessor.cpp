#include "platform/render/VisualFxProcessor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>
#include <vector>
#include <string_view>

#include "platform/render/GlitchVisualFx.h"
#include "platform/render/GlowVisualFx.h"
#include "platform/render/TypingVisualFx.h"
#include "platform/render/ColorFilterVisualFx.h"

namespace avantgarde {
namespace {

std::string toLowerAscii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string normalizeSingleEffectId(std::string_view raw) {
    std::size_t begin = 0;
    std::size_t end = raw.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1U])) != 0) {
        --end;
    }
    if (begin >= end) {
        return {};
    }
    const std::string_view token = raw.substr(begin, end - begin);
    if (token.find_first_of(",;|+") != std::string_view::npos) {
        return {};
    }
    return toLowerAscii(token);
}

} // namespace

VisualFxProcessor::VisualFxProcessor() {
    registerFx(std::make_unique<GlitchVisualFx>());
    registerFx(std::make_unique<GlowVisualFx>());
    registerFx(std::make_unique<TypingVisualFx>());
    registerFx(std::make_unique<ColorFilterVisualFx>());
}

void VisualFxProcessor::registerFx(std::unique_ptr<IVisualFx> fx) {
    if (!fx) {
        return;
    }
    const std::string key = toLowerAscii(fx->id());
    if (key.empty()) {
        return;
    }
    fxById_[key] = std::move(fx);
}

VisualFxBlockStyle VisualFxProcessor::resolveBlockStyle(const VisualFxRequest& request) {
    const std::string effectId = normalizeSingleEffectId(request.effect);
    if (effectId.empty()) {
        return {};
    }
    const auto it = fxById_.find(effectId);
    if (it == fxById_.end() || !it->second) {
        return {};
    }
    VisualFxRequest one = request;
    one.effect = effectId;
    return it->second->resolve(one);
}

bool VisualFxProcessor::applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) {
    const std::string effectId = normalizeSingleEffectId(request.effect);
    if (effectId.empty()) {
        return false;
    }
    const auto it = fxById_.find(effectId);
    if (it == fxById_.end() || !it->second) {
        return false;
    }
    VisualFxRequest one = request;
    one.effect = effectId;
    return it->second->applyRgba(view, one);
}

VisualFxTextStyle VisualFxProcessor::resolveTextStyle(const VisualFxRequest& request) {
    VisualFxTextStyle style{};
    const VisualFxBlockStyle block = resolveBlockStyle(request);
    if (!block.active) {
        return style;
    }
    style.active = true;
    if (const auto* glitch = std::get_if<GlitchVisualFxPayload>(&block.payload)) {
        style.jitterX = glitch->offsetX;
        style.splitPx = glitch->splitPx;
        style.alpha = glitch->alpha;
        style.bandCount = glitch->sliceCount;
        style.alternatePhase = glitch->alternatePhase;
        style.phase01 = glitch->phase01;
        style.crumble01 = glitch->crumble01;
        return style;
    }
    if (const auto* glow = std::get_if<GlowVisualFxPayload>(&block.payload)) {
        style.jitterX = glow->offsetX;
        style.splitPx = glow->radiusPx;
        style.alpha = glow->alpha;
        style.bandCount = 0U;
        style.alternatePhase = false;
        style.phase01 = glow->phase01;
        style.crumble01 = 0.0f;
        style.glowNearRadiusPx = glow->nearRadiusPx;
        style.glowFarRadiusPx = glow->farRadiusPx;
        style.glowNearAlpha = glow->nearAlpha;
        style.glowFarAlpha = glow->farAlpha;
        style.hasTint = glow->hasTint;
        style.tintR = glow->tintR;
        style.tintG = glow->tintG;
        style.tintB = glow->tintB;
        style.tintA = glow->tintA;
        return style;
    }
    style.active = false;
    return style;
}

VisualFxTextGeometryStyle VisualFxProcessor::resolveTextGeometryStyle(const VisualFxRequest& request) {
    VisualFxTextGeometryStyle geom{};
    const VisualFxTextStyle style = resolveTextStyle(request);
    if (!style.active) {
        return geom;
    }
    // Геометрию включаем только для эффектов, которые экспонируют bandCount >= 2.
    // Сейчас это glitch, но рендерер про конкретный id не знает.
    if (style.bandCount < 2U) {
        return geom;
    }
    geom.active = true;
    geom.sliceCount = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(style.bandCount), 2, 3));
    geom.jitterX = style.jitterX;
    geom.splitPx = style.splitPx;
    geom.alpha = style.alpha;
    geom.alternatePhase = style.alternatePhase;
    return geom;
}

VisualFxTextRevealStyle VisualFxProcessor::resolveTextRevealStyle(const VisualFxRequest& request) {
    VisualFxTextRevealStyle reveal{};
    const VisualFxBlockStyle block = resolveBlockStyle(request);
    if (!block.active) {
        return reveal;
    }
    if (const auto* typing = std::get_if<TypingVisualFxPayload>(&block.payload)) {
        reveal.active = true;
        reveal.reveal01 = std::clamp(typing->reveal01, 0.0f, 1.0f);
    }
    return reveal;
}

bool VisualFxProcessor::requiresWholeTextPass(const VisualFxRequest& request) const {
    const std::string effectId = normalizeSingleEffectId(request.effect);
    if (effectId.empty()) {
        return false;
    }
    // Typing должен видеть всю строку целиком, иначе печать идет по словам.
    return effectId == "typing";
}

VisualFxTextStyle VisualFxProcessor::resolveGlitchTextStyle(const VisualFxRequest& request) {
    VisualFxRequest req = request;
    req.effect = "glitch";
    return resolveTextStyle(req);
}

} // namespace avantgarde
