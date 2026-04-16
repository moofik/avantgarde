#include "service/ui/UiBindRegistry.h"

#include <algorithm>
#include <array>
#include <string>

#include "service/ui/UiBindParser.h"

namespace avantgarde {
namespace {

std::vector<UiBindOption> fxEditorKnobCatalog() {
    return {
        {"selected", "fx.selected.param.selected", "Текущий выбранный параметр FX"},
        {"fx.selected.param.<index>", "fx.selected.param.<index>", "Параметр FX по индексу"},
    };
}

std::vector<UiBindOption> fxEditorAnimCatalog() {
    return {
        {"current", "fx.anim.current", "Анимация текущего типа FX"},
        {"reverb", "fx.anim.reverb", "Анимация для реверба"},
        {"hpf", "fx.anim.hpf", "Анимация для HPF"},
        {"buffer", "fx.anim.buffer", "Анимация для Buffer FX"},
    };
}

std::vector<UiBindOption> tracksKnobCatalog() {
    return {
        {"bpm", "transport.bpm", "Темп транспорта"},
        {"speed", "track.selected.speed", "Скорость выбранного трека"},
        {"gain", "track.selected.gain", "Громкость выбранного трека"},
        {"profile", "track.selected.playback_profile", "Playback-профиль выбранного трека"},
        {"looper", "track.selected.looper_mode", "Режим LOOPER выбранного трека (0/1)"},
    };
}

std::vector<UiBindOption> sampleEditControlCatalog() {
    return {
        {"profile", "track.selected.playback_profile", "Playback-профиль выбранного трека"},
        {"speed", "track.selected.speed", "Скорость выбранного трека"},
        {"gain", "track.selected.gain", "Громкость выбранного трека"},
        {"start", "track.selected.start", "Старт playback-региона выбранного трека"},
        {"end", "track.selected.end", "Конец playback-региона выбранного трека"},
        {"tempo_sync", "track.selected.tempo_sync", "Tempo Sync выбранного трека"},
        {"sync", "track.selected.tempo_sync", "Tempo Sync выбранного трека"},
    };
}

std::vector<UiBindOption> statusBarCatalog() {
    return {
        {"scene", "status.scene", "Текущая сцена"},
        {"action", "status.action", "Активный pointer action"},
        {"transport", "status.transport", "Play/BPM/Quant"},
    };
}

bool namespaceSupported(std::string_view ns) noexcept {
    static constexpr std::array<std::string_view, 6> kNamespaces = {
        "status",
        "transport",
        "track",
        "fx",
        "param",
        "ui",
    };
    return std::find(kNamespaces.begin(), kNamespaces.end(), ns) != kNamespaces.end();
}

} // namespace

UiBindRegistry::UiBindRegistry() {
    entries_.push_back({
        UiScene::FxEditor,
        UiLayoutNodeType::Knob,
        "fx.selected.param.selected",
        fxEditorKnobCatalog(),
    });
    entries_.push_back({
        UiScene::FxEditor,
        UiLayoutNodeType::Switch,
        "fx.selected.param.selected",
        fxEditorKnobCatalog(),
    });
    entries_.push_back({
        UiScene::FxEditor,
        UiLayoutNodeType::AnimSlot,
        "fx.anim.current",
        fxEditorAnimCatalog(),
    });
    entries_.push_back({
        UiScene::Tracks,
        UiLayoutNodeType::Knob,
        "track.selected.speed",
        tracksKnobCatalog(),
    });
    entries_.push_back({
        UiScene::Tracks,
        UiLayoutNodeType::Switch,
        "track.selected.playback_profile",
        tracksKnobCatalog(),
    });
    entries_.push_back({
        UiScene::Tracks,
        UiLayoutNodeType::StatusBar,
        "status.scene",
        statusBarCatalog(),
    });
    entries_.push_back({
        UiScene::SampleEdit,
        UiLayoutNodeType::Knob,
        "track.selected.speed",
        sampleEditControlCatalog(),
    });
    entries_.push_back({
        UiScene::SampleEdit,
        UiLayoutNodeType::Switch,
        "track.selected.playback_profile",
        sampleEditControlCatalog(),
    });
    entries_.push_back({
        UiScene::SampleEdit,
        UiLayoutNodeType::StatusBar,
        "status.scene",
        statusBarCatalog(),
    });
    entries_.push_back({
        UiScene::FxEditor,
        UiLayoutNodeType::StatusBar,
        "status.scene",
        statusBarCatalog(),
    });
}

const UiBindRegistry& UiBindRegistry::instance() {
    static const UiBindRegistry registry{};
    return registry;
}

const UiBindRegistry::CatalogEntry* UiBindRegistry::findEntry_(UiScene scene,
                                                               UiLayoutNodeType nodeType) const noexcept {
    for (const CatalogEntry& e : entries_) {
        if (e.scene == scene && e.nodeType == nodeType) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<UiBindOption> UiBindRegistry::catalog(UiScene scene, UiLayoutNodeType nodeType) const {
    const CatalogEntry* entry = findEntry_(scene, nodeType);
    return (entry == nullptr) ? std::vector<UiBindOption>{} : entry->options;
}

bool UiBindRegistry::tryResolveAlias(UiScene scene,
                                     UiLayoutNodeType nodeType,
                                     std::string_view normalizedKey,
                                     std::string& canonicalOut) const {
    const CatalogEntry* entry = findEntry_(scene, nodeType);
    if (entry == nullptr) {
        return false;
    }

    for (const UiBindOption& option : entry->options) {
        if (normalizedKey == option.alias || normalizedKey == option.canonical) {
            canonicalOut = option.canonical;
            return true;
        }
    }
    return false;
}

std::string UiBindRegistry::defaultCanonical(UiScene scene, UiLayoutNodeType nodeType) const {
    const CatalogEntry* entry = findEntry_(scene, nodeType);
    return (entry == nullptr) ? std::string{} : entry->defaultCanonical;
}

bool UiBindRegistry::isCanonicalSupported(std::string_view canonical, std::string& errorOut) const {
    const UiBindParsed parsed = UiBindParser::parse(canonical);
    if (!parsed.ok) {
        errorOut = parsed.error;
        return false;
    }

    if (!namespaceSupported(parsed.ns)) {
        errorOut = "Unsupported bind namespace: '" + parsed.ns + "'";
        return false;
    }
    return true;
}

} // namespace avantgarde
