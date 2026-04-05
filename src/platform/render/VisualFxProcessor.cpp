#include "platform/render/VisualFxProcessor.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <string_view>

#include "platform/render/GlitchVisualFx.h"

namespace avantgarde {
namespace {

std::string toLowerAscii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

VisualFxProcessor::VisualFxProcessor() {
    registerFx(std::make_unique<GlitchVisualFx>());
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
    const std::string effectId = toLowerAscii(request.effect);
    if (effectId.empty()) {
        return {};
    }
    const auto it = fxById_.find(effectId);
    if (it == fxById_.end() || !it->second) {
        return {};
    }
    return it->second->resolve(request);
}

VisualFxTextStyle VisualFxProcessor::resolveTextStyle(const VisualFxRequest& request) {
    VisualFxTextStyle style{};
    const VisualFxBlockStyle block = resolveBlockStyle(request);
    if (!block.active) {
        return style;
    }
    style.active = block.active;
    style.jitterX = block.offsetX;
    style.splitPx = block.splitPx;
    style.alpha = block.alpha;
    style.bandCount = block.sliceCount;
    style.alternatePhase = block.alternatePhase;
    return style;
}

VisualFxTextStyle VisualFxProcessor::resolveGlitchTextStyle(const VisualFxRequest& request) {
    VisualFxRequest req = request;
    req.effect = "glitch";
    return resolveTextStyle(req);
}

} // namespace avantgarde
