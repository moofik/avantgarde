#pragma once

#include <string>
#include <string_view>

#include "contracts/UiLayout.h"

namespace avantgarde {

// Минимальный загрузчик TOML-шаблонов UI.
// Важно: это осознанно "узкий" парсер под наш формат,
// а не полный TOML-интерпретатор.
class UiLayoutTomlLoader final {
public:
    // Загрузить шаблон из файла.
    // Возвращает true при успешном парсинге.
    static bool loadFromFile(const std::string& path,
                             UiLayoutTemplate& out,
                             std::string& errorOut);

    // Загрузить шаблон из строки.
    // Удобно для unit-тестов и dev-инструментов.
    static bool loadFromString(std::string_view content,
                               UiLayoutTemplate& out,
                               std::string& errorOut);
};

} // namespace avantgarde

