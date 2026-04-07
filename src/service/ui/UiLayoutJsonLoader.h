#pragma once

#include <string>
#include <string_view>

#include "contracts/UiLayout.h"

namespace avantgarde {

// Минимальный JSON-загрузчик UI-шаблонов.
// Поддерживает тот же контракт UiLayoutTemplate, что и TOML-loader.
class UiLayoutJsonLoader final {
public:
    // Загрузить шаблон из файла.
    static bool loadFromFile(const std::string& path,
                             UiLayoutTemplate& out,
                             std::string& errorOut);

    // Загрузить шаблон из JSON-строки.
    static bool loadFromString(std::string_view content,
                               UiLayoutTemplate& out,
                               std::string& errorOut);
};

} // namespace avantgarde

