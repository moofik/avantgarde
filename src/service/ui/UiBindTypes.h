#pragma once

#include <string>

namespace avantgarde {

// Один вариант bind-а для UI-подсказок/каталога.
struct UiBindOption {
    // Короткий алиас, который пользователь может написать в шаблоне.
    std::string alias{};
    // Канонический bind-ключ, в который резолвится алиас.
    std::string canonical{};
    // Короткое объяснение назначения bind-а.
    std::string description{};
};

} // namespace avantgarde
