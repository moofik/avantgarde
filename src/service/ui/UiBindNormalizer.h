#pragma once

#include <string>
#include <string_view>

namespace avantgarde {

/**
 * @brief Нормализатор bind-строк для UI-конфига.
 *
 * Отвечает только за техническую очистку ключа:
 * - trim по краям;
 * - lower-case;
 * - замена '_'/'-' на '.'
 *
 * Класс НЕ валидирует смысл bind и не знает про сцены/виджеты.
 */
class UiBindNormalizer final {
public:
    static std::string normalize(std::string_view raw);
};

} // namespace avantgarde
