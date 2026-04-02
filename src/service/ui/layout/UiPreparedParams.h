#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avantgarde {

// Универсальный контейнер "подготовленных параметров кадра":
// виджет заполняет данные из UiState, а composer сам выбирает нужные
// значения для конкретной ноды layout.
struct UiPreparedParams {
    std::unordered_map<std::string, std::string> text{};
    std::unordered_map<std::string, float> number{};
    std::unordered_map<std::string, int32_t> integer{};
    std::unordered_map<std::string, bool> flag{};
    std::unordered_map<std::string, std::vector<std::string>> rows{};

    std::optional<std::string> findText(std::string_view key) const {
        auto it = text.find(std::string(key));
        if (it == text.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<float> findNumber(std::string_view key) const {
        auto it = number.find(std::string(key));
        if (it == number.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<int32_t> findInteger(std::string_view key) const {
        auto it = integer.find(std::string(key));
        if (it == integer.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<bool> findFlag(std::string_view key) const {
        auto it = flag.find(std::string(key));
        if (it == flag.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::vector<std::string>> findRows(std::string_view key) const {
        auto it = rows.find(std::string(key));
        if (it == rows.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

} // namespace avantgarde

