#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace avantgarde {

/**
 * @brief Единый нормализатор ссылочных токенов UI-конфигов.
 *
 * Задача класса:
 * - не смешивать логику распознавания "@theme.*" в разных парсерах;
 * - держать правила синтаксиса токенов в одном месте;
 * - возвращать уже канонический ключ, пригодный для lookup в catalog/root object.
 */
class UiTokenResolver final {
public:
    /**
     * @brief Нормализовать ссылку на theme-токен.
     *
     * Поддерживаемые формы:
     * - "@theme.colors.text.primary"
     * - "@themes.colors.text.primary"
     * - "theme.colors.text.primary"
     * - "themes.colors.text.primary"
     * - "{@theme.colors.text.primary}"
     * - "{@themes.colors.text.primary}"
     *
     * @return canonical key без префикса (например "colors.text.primary")
     *         или std::nullopt, если строка не является theme-ссылкой.
     */
    static std::optional<std::string> normalizeThemeRef(std::string_view raw);
};

} // namespace avantgarde

