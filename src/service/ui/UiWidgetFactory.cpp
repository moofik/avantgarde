#include "service/ui/UiWidgetFactory.h"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "service/ui/FxEditorWidget.h"
#include "service/ui/FxListWidget.h"
#include "service/ui/ManagerWidget.h"
#include "service/ui/TrackContextMenuWidget.h"
#include "service/ui/TracksWidget.h"
#include "service/ui/UiLayoutTomlLoader.h"

namespace avantgarde {
namespace {

UiLayoutTemplate loadTemplateOrThrow(const UiWidgetFactoryOptions& options,
                                     std::string_view fileName) {
    if (fileName.empty()) {
        throw std::runtime_error("UiWidgetFactory: empty layout file name");
    }
    if (!options.enableTomlLayouts) {
        const std::string msg = "UiWidgetFactory: TOML layouts are disabled, strict UI requires enabled layouts";
        std::fprintf(stderr, "[UI][LAYOUT][ERROR] %s\n", msg.c_str());
        throw std::runtime_error(msg);
    }

    std::string diagnostics{};
    for (const std::string& root : options.layoutSearchRoots) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path path = std::filesystem::path(root) / std::string(fileName);
        UiLayoutTemplate tpl{};
        std::string err{};
        if (UiLayoutTomlLoader::loadFromFile(path.string(), tpl, err)) {
            return tpl;
        }
        diagnostics += "  - " + path.string();
        if (!err.empty()) {
            diagnostics += " : " + err;
        } else {
            diagnostics += " : not found or invalid";
        }
        diagnostics.push_back('\n');
    }

    std::string msg = "UiWidgetFactory: required layout '" + std::string(fileName) + "' is missing or invalid.\n";
    msg += diagnostics;
    std::fprintf(stderr, "[UI][LAYOUT][ERROR] %s", msg.c_str());
    if (!msg.empty() && msg.back() != '\n') {
        std::fprintf(stderr, "\n");
    }
    throw std::runtime_error(msg);
}

} // namespace

UiWidgetFactory::UiWidgetFactory(UiWidgetFactoryOptions options)
    : options_(std::move(options)) {}

std::unique_ptr<IUiWidget> UiWidgetFactory::create(UiScene scene) const {
    switch (scene) {
        case UiScene::Tracks:
            // Основной экран: прокидываем общие параметры кадра и заголовка.
            // Теперь сюда же подключаем TOML-layout (если найден).
            return std::make_unique<TracksWidget>(
                TracksWidget::Options{
                    .frameWidth = options_.frameWidth,
                    .headerTitle = options_.tracksHeaderTitle,
                    .speedStep = options_.tracksSpeedStep,
                    .bpmStep = options_.tracksBpmStep,
                    .layoutTemplate = loadTemplateOrThrow(options_, "tracks.toml"),
                });
        case UiScene::TrackContext:
            return std::make_unique<TrackContextMenuWidget>(
                options_.frameWidth,
                loadTemplateOrThrow(options_, "track_menu.toml"));
        case UiScene::Manager:
            // Файловый менеджер использует только ширину рамки.
            return std::make_unique<ManagerWidget>(
                options_.frameWidth,
                loadTemplateOrThrow(options_, "manager.toml"));
        case UiScene::FxList:
            return std::make_unique<FxListWidget>(
                options_.frameWidth,
                loadTemplateOrThrow(options_, "fx_list.toml"));
        case UiScene::FxEditor: {
            // Строгая модель: base + отдельный профиль для каждого FX (без fallback).
            auto baseLayout = loadTemplateOrThrow(options_, options_.fxEditorBaseLayout);
            std::unordered_map<std::string, UiLayoutTemplate> profileLayouts{};
            profileLayouts.reserve(options_.fxEditorProfileLayouts.size());
            for (const auto& [fxId, filePath] : options_.fxEditorProfileLayouts) {
                if (fxId.empty() || filePath.empty()) {
                    throw std::runtime_error("UiWidgetFactory: fx profile mapping has empty fxId or file path");
                }
                profileLayouts.emplace(fxId, loadTemplateOrThrow(options_, filePath));
            }
            return std::make_unique<FxEditorWidget>(
                options_.frameWidth,
                options_.fxParamStep,
                std::move(baseLayout),
                std::move(profileLayouts));
        }
        case UiScene::Count:
        default:
            return nullptr;
    }
}

} // namespace avantgarde
