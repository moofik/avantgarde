#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "contracts/IUiWidget.h"

namespace avantgarde {

struct UiWidgetFactoryOptions {
    // Базовая ширина для текстовых рамок виджетов.
    uint16_t frameWidth{60};
    // Заголовок экрана треков.
    std::string tracksHeaderTitle{"AVANTGARDE"};
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
