#include "service/ui/UiBindNormalizer.h"

#include <cctype>

namespace avantgarde {

std::string UiBindNormalizer::normalize(std::string_view raw) {
    std::size_t b = 0;
    std::size_t e = raw.size();

    while (b < e && std::isspace(static_cast<unsigned char>(raw[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1U])) != 0) {
        --e;
    }

    std::string out{};
    out.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) {
        char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));
        if (ch == '_' || ch == '-') {
            ch = '.';
        }
        out.push_back(ch);
    }
    return out;
}

} // namespace avantgarde
