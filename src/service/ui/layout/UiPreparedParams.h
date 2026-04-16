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
    std::unordered_map<std::string, std::vector<float>> waves{};

    std::optional<std::string> findText(std::string_view key) const {
        return findMapValueCompat_(text, key);
    }

    std::optional<float> findNumber(std::string_view key) const {
        return findMapValueCompat_(number, key);
    }

    std::optional<int32_t> findInteger(std::string_view key) const {
        return findMapValueCompat_(integer, key);
    }

    std::optional<bool> findFlag(std::string_view key) const {
        return findMapValueCompat_(flag, key);
    }

    std::optional<std::vector<std::string>> findRows(std::string_view key) const {
        return findMapValueCompat_(rows, key);
    }

    std::optional<std::vector<float>> findWave(std::string_view key) const {
        return findMapValueCompat_(waves, key);
    }

private:
    template <typename MapT>
    static std::optional<typename MapT::mapped_type> findMapValueCompat_(const MapT& map,
                                                                          std::string_view key) {
        const std::string direct(key);
        if (auto it = map.find(direct); it != map.end()) {
            return it->second;
        }

        // Совместимость bind-форм:
        // bind normalizer может менять '_' <-> '.', но не всегда "полностью".
        // Поэтому пробуем:
        // 1) глобальную замену во всей строке,
        // 2) точечную замену одного символа-разделителя.
        auto lookupVariant = [&map](const std::string& candidate)
            -> std::optional<typename MapT::mapped_type> {
            if (auto it = map.find(candidate); it != map.end()) {
                return it->second;
            }
            return std::nullopt;
        };

        {
            std::string v = direct;
            bool changed = false;
            for (char& ch : v) {
                if (ch == '.') {
                    ch = '_';
                    changed = true;
                }
            }
            if (changed) {
                if (auto found = lookupVariant(v); found.has_value()) {
                    return found;
                }
            }
        }
        {
            std::string v = direct;
            bool changed = false;
            for (char& ch : v) {
                if (ch == '_') {
                    ch = '.';
                    changed = true;
                }
            }
            if (changed) {
                if (auto found = lookupVariant(v); found.has_value()) {
                    return found;
                }
            }
        }
        for (std::size_t i = 0; i < direct.size(); ++i) {
            if (direct[i] != '.' && direct[i] != '_') {
                continue;
            }
            std::string v = direct;
            v[i] = (v[i] == '.') ? '_' : '.';
            if (auto found = lookupVariant(v); found.has_value()) {
                return found;
            }
        }

        return std::nullopt;
    }
};

} // namespace avantgarde
