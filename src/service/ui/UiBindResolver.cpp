#include "service/ui/UiBindResolver.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace avantgarde {
namespace {

std::string trimLower(std::string_view raw) {
    std::size_t b = 0;
    std::size_t e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1U])) != 0) {
        --e;
    }
    std::string out;
    out.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) {
        char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));
        if (ch == '_' || ch == '-') {
            ch = '.';
        }
        out.push_back(ch);
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::vector<UiBindOption> fxEditorKnobCatalog() {
    return {
        {"selected", "action.scene.fx.param.value.selected", "Текущий выбранный параметр FX"},
        {"scene.scenefxparamvalue.<index>", "action.scene.fx.param.value.<index>", "Параметр FX по индексу UiAction"},
        {"scene.fx.param.value.<index>", "action.scene.fx.param.value.<index>", "Короткая форма параметра FX по индексу"},
        {"action.scene.fx.param.value.<index>", "action.scene.fx.param.value.<index>", "Полная каноническая форма"},
    };
}

std::vector<UiBindOption> fxEditorAnimCatalog() {
    return {
        {"current", "fx.anim.current", "Анимация текущего типа FX"},
        {"reverb", "fx.anim.reverb", "Анимация для реверба"},
        {"hpf", "fx.anim.hpf", "Анимация для HPF"},
    };
}

std::vector<UiBindOption> tracksKnobCatalog() {
    return {
        {"bpm", "transport.bpm", "Темп транспорта"},
        {"speed", "track.selected.speed", "Скорость выбранного трека"},
        {"gain", "track.selected.gain", "Громкость выбранного трека"},
    };
}

std::vector<UiBindOption> statusBarCatalog() {
    return {
        {"scene", "status.scene", "Текущая сцена"},
        {"action", "status.action", "Активный pointer action"},
        {"transport", "status.transport", "Play/BPM/Quant"},
    };
}

bool resolveByCatalog(std::string_view key,
                      const std::vector<UiBindOption>& catalog,
                      std::string& canonicalOut) {
    for (const UiBindOption& opt : catalog) {
        if (key == opt.alias || key == opt.canonical) {
            canonicalOut = opt.canonical;
            return true;
        }
    }
    return false;
}

bool parseIndex(std::string_view raw, uint16_t& out) {
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

UiBindResolution resolveFxEditorKnob(std::string_view key) {
    UiBindResolution out{};
    out.actionId = UiAction::Id::SceneFxParamValue;

    if (key.empty() || key == "selected" || key == "fx.param.selected") {
        out.ok = true;
        out.canonical = "action.scene.fx.param.value.selected";
        out.paramIndex = -1;
        return out;
    }

    // Полная action-форма.
    static constexpr std::string_view kActionPrefix = "action.scene.fx.param.value.";
    if (startsWith(key, kActionPrefix)) {
        const std::string_view tail = key.substr(kActionPrefix.size());
        if (tail == "selected") {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value.selected";
            out.paramIndex = -1;
            return out;
        }
        uint16_t index = 0;
        if (parseIndex(tail, index)) {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value." + std::to_string(index);
            out.paramIndex = static_cast<int32_t>(index);
            return out;
        }
        out.ok = false;
        out.error = "Invalid FX param index in action.scene.fx.param.value.<index>.";
        return out;
    }

    // Короткая action-форма.
    static constexpr std::string_view kSceneActionPrefix = "scene.scenefxparamvalue.";
    if (startsWith(key, kSceneActionPrefix)) {
        const std::string_view tail = key.substr(kSceneActionPrefix.size());
        uint16_t index = 0;
        if (parseIndex(tail, index)) {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value." + std::to_string(index);
            out.paramIndex = static_cast<int32_t>(index);
            return out;
        }
        out.ok = false;
        out.error = "Invalid FX param index in scene.scenefxparamvalue.<index>.";
        return out;
    }

    // Читаемая точечная форма.
    static constexpr std::string_view kSceneDotPrefix = "scene.fx.param.value.";
    if (startsWith(key, kSceneDotPrefix)) {
        const std::string_view tail = key.substr(kSceneDotPrefix.size());
        uint16_t index = 0;
        if (parseIndex(tail, index)) {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value." + std::to_string(index);
            out.paramIndex = static_cast<int32_t>(index);
            return out;
        }
        out.ok = false;
        out.error = "Invalid FX param index in scene.fx.param.value.<index>.";
        return out;
    }

    // Legacy-форма fx.param.<index> пока держится для мягкой миграции.
    static constexpr std::string_view kLegacyPrefix = "fx.param.";
    if (startsWith(key, kLegacyPrefix)) {
        const std::string_view tail = key.substr(kLegacyPrefix.size());
        if (tail == "selected") {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value.selected";
            out.paramIndex = -1;
            return out;
        }
        uint16_t index = 0;
        if (parseIndex(tail, index)) {
            out.ok = true;
            out.canonical = "action.scene.fx.param.value." + std::to_string(index);
            out.paramIndex = static_cast<int32_t>(index);
            return out;
        }
        out.ok = false;
        out.error = "Legacy fx.param.<index> bind expects numeric index.";
        return out;
    }

    // Каталог оставляем только как подсказку формата.
    const auto cat = fxEditorKnobCatalog();
    if (resolveByCatalog(key, cat, out.canonical)) {
        out.ok = true;
        out.paramIndex = -1;
        return out;
    }

    out.ok = false;
    out.error =
        "Unknown FX knob bind. Use Scene.SceneFxParamValue.<index> or scene.fx.param.value.<index>.";
    return out;
}

UiBindResolution resolveFxEditorAnim(std::string_view key) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = "fx.anim.current";
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

    const auto cat = fxEditorAnimCatalog();
    if (resolveByCatalog(key, cat, out.canonical)) {
        out.ok = true;
        return out;
    }

    out.ok = false;
    out.error = "Unknown FX anim bind. Expected aliases like current/reverb/hpf.";
    return out;
}

UiBindResolution resolveTracksKnob(std::string_view key) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = "track.selected.speed";
        return out;
    }
    if (startsWith(key, "transport.") || startsWith(key, "track.selected.")) {
        out.ok = true;
        out.canonical = std::string(key);
        return out;
    }

    const auto cat = tracksKnobCatalog();
    if (resolveByCatalog(key, cat, out.canonical)) {
        out.ok = true;
        return out;
    }

    out.ok = false;
    out.error = "Unknown Tracks knob bind. Expected aliases like bpm/speed/gain.";
    return out;
}

UiBindResolution resolveStatusBar(std::string_view key) {
    UiBindResolution out{};
    if (key.empty()) {
        out.ok = true;
        out.canonical = "status.scene";
        return out;
    }
    if (startsWith(key, "status.")) {
        out.ok = true;
        out.canonical = std::string(key);
        return out;
    }

    const auto cat = statusBarCatalog();
    if (resolveByCatalog(key, cat, out.canonical)) {
        out.ok = true;
        return out;
    }

    out.ok = false;
    out.error = "Unknown statusbar bind. Expected aliases: scene/action/transport.";
    return out;
}

} // namespace

std::vector<UiBindOption> UiBindResolver::catalog(UiScene scene, UiLayoutNodeType nodeType) {
    if (nodeType == UiLayoutNodeType::StatusBar) {
        return statusBarCatalog();
    }
    if (scene == UiScene::FxEditor &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return fxEditorKnobCatalog();
    }
    if (scene == UiScene::FxEditor && nodeType == UiLayoutNodeType::AnimSlot) {
        return fxEditorAnimCatalog();
    }
    if (scene == UiScene::Tracks &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return tracksKnobCatalog();
    }
    return {};
}

UiBindResolution UiBindResolver::resolve(UiScene scene,
                                         UiLayoutNodeType nodeType,
                                         std::string_view rawBind) {
    const std::string key = trimLower(rawBind);

    if (nodeType == UiLayoutNodeType::StatusBar) {
        return resolveStatusBar(key);
    }
    if (scene == UiScene::FxEditor &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return resolveFxEditorKnob(key);
    }
    if (scene == UiScene::FxEditor && nodeType == UiLayoutNodeType::AnimSlot) {
        return resolveFxEditorAnim(key);
    }
    if (scene == UiScene::Tracks &&
        (nodeType == UiLayoutNodeType::Knob || nodeType == UiLayoutNodeType::Switch)) {
        return resolveTracksKnob(key);
    }

    // Для неподдерживаемых комбинаций пока ничего не ломаем:
    // если bind не задан — ок, иначе прокидываем как есть.
    UiBindResolution out{};
    out.ok = true;
    out.canonical = key;
    return out;
}

} // namespace avantgarde
