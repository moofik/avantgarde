#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/SamplerApplication.h"
#include "app/SamplerIoLayer.h"
#include "contracts/UiTheme.h"

using namespace avantgarde;

int main(int argc, char** argv) {
    SamplerUiMode uiMode = SamplerUiMode::Ansi;
    UiTheme uiTheme = UiTheme::Default;
    bool uiThemeProvided = false;

    int argi = 1;
    while (argi < argc) {
        const std::string arg = argv[argi];
        if (arg.rfind("--ui=", 0) == 0) {
            if (!parseSamplerUiMode(std::string_view(arg).substr(5), uiMode)) {
                std::printf("Unsupported UI mode: %s\n", arg.c_str());
                return 1;
            }
            ++argi;
            continue;
        }
        if (arg == "--ui" && (argi + 1) < argc) {
            if (!parseSamplerUiMode(argv[argi + 1], uiMode)) {
                std::printf("Unsupported UI mode: %s\n", argv[argi + 1]);
                return 1;
            }
            argi += 2;
            continue;
        }
        if (arg.rfind("--theme=", 0) == 0) {
            if (!parseUiTheme(std::string_view(arg).substr(8), uiTheme)) {
                std::printf("Unsupported theme: %s\n", arg.c_str());
                return 1;
            }
            uiThemeProvided = true;
            ++argi;
            continue;
        }
        if (arg == "--theme" && (argi + 1) < argc) {
            if (!parseUiTheme(argv[argi + 1], uiTheme)) {
                std::printf("Unsupported theme: %s\n", argv[argi + 1]);
                return 1;
            }
            uiThemeProvided = true;
            argi += 2;
            continue;
        }
        if (arg == "--ui") {
            std::printf("Missing value for --ui (expected: ansi|lowres|gb|gb-window)\n");
            return 1;
        }
        if (arg == "--theme") {
            std::printf("Missing value for --theme (expected: default|gothic)\n");
            return 1;
        }
        if (arg.rfind("--", 0) == 0) {
            std::printf("Unknown option: %s\n", arg.c_str());
            return 1;
        }
        break;
    }

    if (argi >= argc) {
        std::printf("Usage: %s [--ui=ansi|lowres|gb|gb-window] [--theme=default|gothic] "
                    "/path/to/track1.wav [/path/to/track2.wav]\n", argv[0]);
        return 1;
    }

    static constexpr uint16_t kGbTextWidth = 60;

    SamplerAppConfig config{};
    config.io.mode = uiMode;
    config.io.theme = uiTheme;
    config.io.themeProvided = uiThemeProvided;
    config.io.gbTextWidth = kGbTextWidth;
    config.gbTextWidth = kGbTextWidth;

    config.engine.track0Path = argv[argi];
    config.engine.hasTrack1 = (argi + 1 < argc);
    if (config.engine.hasTrack1) {
        config.engine.track1Path = argv[argi + 1];
    }

    SamplerApplication app;
    return app.run(config);
}
