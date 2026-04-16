#include "service/ui/UiTokenResolver.h"

#include <algorithm>
#include <cctype>

namespace avantgarde {
namespace {

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

std::optional<std::string> UiTokenResolver::normalizeThemeRef(std::string_view raw) {
    std::string ref = toLowerAscii(std::string(raw));
    if (ref.size() > 3U && ref.front() == '{' && ref.back() == '}') {
        ref = ref.substr(1U, ref.size() - 2U);
    }
    if (ref.rfind("@theme.", 0) == 0U) {
        ref = ref.substr(std::string("@theme.").size());
    } else if (ref.rfind("@themes.", 0) == 0U) {
        ref = ref.substr(std::string("@themes.").size());
    } else if (ref.rfind("theme.", 0) == 0U) {
        ref = ref.substr(std::string("theme.").size());
    } else if (ref.rfind("themes.", 0) == 0U) {
        ref = ref.substr(std::string("themes.").size());
    } else {
        return std::nullopt;
    }
    if (ref.empty()) {
        return std::nullopt;
    }
    return ref;
}

} // namespace avantgarde

