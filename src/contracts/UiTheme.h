#pragma once

#include <cstdint>
#include <string_view>

namespace avantgarde {

enum class UiTheme {
    Default = 0,
    Gothic = 1
};

struct UiThemeRgb {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
};

struct UiThemePalette {
    UiThemeRgb bg{};
    UiThemeRgb panel{};
    UiThemeRgb mid{};
    UiThemeRgb text{};
};

inline bool parseUiTheme(std::string_view raw, UiTheme& out) noexcept {
    if (raw == "default") {
        out = UiTheme::Default;
        return true;
    }
    if (raw == "gothic") {
        out = UiTheme::Gothic;
        return true;
    }
    return false;
}

inline UiThemePalette uiThemePalette(UiTheme theme) noexcept {
    switch (theme) {
        case UiTheme::Default:
            return UiThemePalette{
                UiThemeRgb{0x10, 0x10, 0x10},
                UiThemeRgb{0x1E, 0x1E, 0x1E},
                UiThemeRgb{0x58, 0x58, 0x58},
                UiThemeRgb{0xDE, 0xDE, 0xDE}
            };
        case UiTheme::Gothic:
        default:
            return UiThemePalette{
                UiThemeRgb{0x0B, 0x09, 0x0D},
                UiThemeRgb{0x21, 0x18, 0x24},
                UiThemeRgb{0x5C, 0x3F, 0x57},
                UiThemeRgb{0xB7, 0x9A, 0xAF}
            };
    }
}

} // namespace avantgarde
