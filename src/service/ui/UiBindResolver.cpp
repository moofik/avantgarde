#include "service/ui/UiBindResolver.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "service/ui/UiBindNormalizer.h"
#include "service/ui/UiBindParser.h"
#include "service/ui/UiBindRegistry.h"

namespace avantgarde {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool parseIndex(std::string_view raw, uint16_t& out) noexcept {
    if (raw.empty()) {
        return false;
    }
    uint32_t value = 0;
    for (char ch : raw) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(ch - '0');
        if (value > 65535U) {
            return false;
        }
    }
    out = static_cast<uint16_t>(value);
    return true;
}

UiBindResolution resolveFxEditorParamBind(std::string_view key, const UiBindRegistry& registry) {
    UiBindResolution out{};
    out.actionId = UiAction::Id::SceneFxParamValue;

    if (key.empty()) {
        out.ok = true;
        out.canonical = registry.defaultCanonical(UiScene::FxEditor, UiLayoutNodeType::Knob);
        out.paramIndex = -1;
        return out;
    }

    // Каноническая FX-форма.
    static constexpr std::string_view kFxPrefix = "fx.selected.param.";
    if (startsWith(key, kFxPrefix)) {
        const std::string_view tail = key.substr(kFxPrefix.size());
        if (tail == "selected") {
            out.ok = true;
            out.canonical = "fx.selected.param.selected";
            out.paramIndex = -1;
            return out;
        }
        uint16_t index = 0;
        if (!parseIndex(tail, index)) {
            out.error = "fx.selected.param.<index> bind expects numeric index.";
            return out;
        }
        out.ok = true;
        out.canonical = "fx.selected.param." + std::to_string(index);
        out.paramIndex = static_cast<int32_t>(index);
        return out;
    }

    // Алиасы каталога (например "selected").
    std::string canonical{};
    if (registry.tryResolveAlias(UiScene::FxEditor, UiLayoutNodeType::Knob, key, canonical)) {
        out.ok = true;
        out.canonical = std::move(canonical);
        out.paramIndex = -1;
        return out;
    }

    out.error = "Unknown FX knob bind. Use fx.selected.param.<index|selected>.";
    return out;
}

UiBindResolution resolveFxEditorAnimBind(std::string_view key, const UiBindRegistry& registry) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = registry.defaultCanonical(UiScene::FxEditor, UiLayoutNodeType::AnimSlot);
        return out;
    }

    std::string canonical{};
    if (registry.tryResolveAlias(UiScene::FxEditor, UiLayoutNodeType::AnimSlot, key, canonical)) {
        out.ok = true;
        out.canonical = std::move(canonical);
        return out;
    }

    if (startsWith(key, "fx.anim.") && key.size() > std::string_view("fx.anim.").size()) {
        out.ok = true;
        out.canonical = std::string(key);
        return out;
    }
    if (startsWith(key, "anim.") && key.size() > std::string_view("anim.").size()) {
        out.ok = true;
        out.canonical = "fx.anim." + std::string(key.substr(std::string_view("anim.").size()));
        return out;
    }

    out.error = "Unknown FX anim bind. Expected aliases like current/reverb/hpf/buffer.";
    return out;
}

UiBindResolution resolveTracksBind(std::string_view key, UiLayoutNodeType nodeType, const UiBindRegistry& registry) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = registry.defaultCanonical(UiScene::Tracks, nodeType);
        return out;
    }

    std::string canonical{};
    if (registry.tryResolveAlias(UiScene::Tracks, nodeType, key, canonical)) {
        out.ok = true;
        out.canonical = std::move(canonical);
        return out;
    }

    if (startsWith(key, "transport.") || startsWith(key, "track.selected.")) {
        out.ok = true;
        out.canonical = std::string(key);
        return out;
    }

    out.error = "Unknown Tracks bind. Expected aliases like bpm/speed/gain/looper.";
    return out;
}

UiBindResolution resolveStatusBarBind(std::string_view key, UiScene scene, const UiBindRegistry& registry) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = registry.defaultCanonical(scene, UiLayoutNodeType::StatusBar);
        return out;
    }

    std::string canonical{};
    if (registry.tryResolveAlias(scene, UiLayoutNodeType::StatusBar, key, canonical)) {
        out.ok = true;
        out.canonical = std::move(canonical);
        return out;
    }

    if (startsWith(key, "status.")) {
        out.ok = true;
        out.canonical = std::string(key);
        return out;
    }

    out.error = "Unknown statusbar bind. Expected aliases: scene/action/transport.";
    return out;
}

void enrichByParser(UiBindResolution& out) {
    const UiBindParsed parsed = UiBindParser::parse(out.canonical);
    if (!parsed.ok) {
        return;
    }
    out.actionId = parsed.actionId;
    out.paramIndex = parsed.paramIndex;
}

bool isStrictContext(UiScene scene, UiLayoutNodeType nodeType) noexcept {
    if (nodeType == UiLayoutNodeType::StatusBar) {
        return true;
    }
    if (scene == UiScene::FxEditor &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch ||
         nodeType == UiLayoutNodeType::AnimSlot)) {
        return true;
    }
    if (scene == UiScene::Tracks &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return true;
    }
    if (scene == UiScene::SampleEdit &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return true;
    }
    return false;
}

} // namespace

std::vector<UiBindOption> UiBindResolver::catalog(UiScene scene, UiLayoutNodeType nodeType) {
    return UiBindRegistry::instance().catalog(scene, nodeType);
}

UiBindResolution UiBindResolver::resolve(UiScene scene,
                                         UiLayoutNodeType nodeType,
                                         std::string_view rawBind) {
    const UiBindRegistry& registry = UiBindRegistry::instance();
    const std::string key = UiBindNormalizer::normalize(rawBind);

    UiBindResolution out{};
    out.paramIndex = -1;

    if (nodeType == UiLayoutNodeType::StatusBar) {
        out = resolveStatusBarBind(key, scene, registry);
    } else if (scene == UiScene::FxEditor &&
               (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        out = resolveFxEditorParamBind(key, registry);
    } else if (scene == UiScene::FxEditor && nodeType == UiLayoutNodeType::AnimSlot) {
        out = resolveFxEditorAnimBind(key, registry);
    } else if (scene == UiScene::Tracks &&
               (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        out = resolveTracksBind(key, nodeType, registry);
    } else if (scene == UiScene::SampleEdit &&
               (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        out = resolveTracksBind(key, nodeType, registry);
    } else {
        // Нестрогий контекст: bind остается как есть, чтобы не ломать
        // кастомные/новые сцены, которые еще не занесены в registry.
        out.ok = true;
        out.canonical = key;
    }

    if (!out.ok) {
        return out;
    }

    // Для строгих контекстов bind должен быть валидным canonical-ключом.
    if (isStrictContext(scene, nodeType) && !out.canonical.empty()) {
        std::string supportError{};
        if (!registry.isCanonicalSupported(out.canonical, supportError)) {
            out.ok = false;
            out.error = supportError;
            return out;
        }
    }

    enrichByParser(out);
    return out;
}

} // namespace avantgarde
