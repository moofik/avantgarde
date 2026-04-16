#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "contracts/FxRegistry.h"
#include "contracts/IUiWidget.h"

namespace avantgarde {

struct UiWidgetFactoryOptions {
    // Базовая ширина для текстовых рамок виджетов.
    uint16_t frameWidth{60};
    // Заголовок экрана треков.
    std::string tracksHeaderTitle{"AVANTGARDE"};
    // Базовые шаги pointer-редактирования для TracksWidget.
    float tracksSpeedStep{0.05f};
    float tracksBpmStep{1.0f};
    // Шаг изменения параметров в FX редакторе.
    float fxParamStep{0.05f};
    // Каталог(и), где ищем шаблоны `*.json`.
    // Первый найденный валидный шаблон используется.
    std::vector<std::string> layoutSearchRoots{
        "assets/ui/layouts",
        "../assets/ui/layouts",
        "../../assets/ui/layouts"
    };
    // Базовый layout FX редактора (каркас + слот `fx_body`).
    std::string fxEditorBaseLayout{"fx_editor_base.json"};
    // Явная таблица профилей: fxId -> путь к layout-профилю внутри layoutSearchRoots.
    std::vector<std::pair<std::string, std::string>> fxEditorProfileLayouts{
        {std::string(FxRegistry::kReverbSchroederId), "fx/reverb.json"},
        {std::string(FxRegistry::kHpfOnePoleId), "fx/hpf.json"},
        {std::string(FxRegistry::kStutterId), "fx/stutter.json"},
        {std::string(FxRegistry::kBufferFxId), "fx/buffer.json"},
    };
};

// Простая фабрика виджетов сцены.
// Нужна для единых базовых опций (ширина, заголовок и т.п.)
// и чтобы main.cpp не знал детали ctor каждого виджета.
class UiWidgetFactory final {
public:
    // Сохраняет базовые опции, общие для всех создаваемых виджетов.
    explicit UiWidgetFactory(UiWidgetFactoryOptions options = {});

    // Создает экземпляр виджета под указанную сцену.
    std::unique_ptr<IUiWidget> create(UiScene scene) const;

private:
    // Конфиг по умолчанию для конструирования виджетов.
    UiWidgetFactoryOptions options_{};
};

} // namespace avantgarde
