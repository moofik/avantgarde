#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "contracts/UiAction.h"

namespace avantgarde {

/**
 * @brief Результат синтаксического разбора bind-ключа.
 *
 * Это "чистый" parse-слой:
 * - не знает про сцены и виджеты;
 * - не маппит alias;
 * - не лезет в runtime state.
 *
 * Задача: превратить canonical bind в структурированный ключ.
 */
struct UiBindParsed {
    bool ok{false};
    std::string canonical{};
    std::string ns{};
    std::string field{};
    std::string error{};
    UiAction::Id actionId{UiAction::Id::None};
    int32_t paramIndex{-1};
};

class UiBindParser final {
public:
    static UiBindParsed parse(std::string_view canonicalBind);
};

} // namespace avantgarde
