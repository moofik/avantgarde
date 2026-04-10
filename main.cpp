#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "app/AppDiagnostics.h"
#include "app/SamplerApplication.h"
#include "app/SamplerIoLayer.h"
#include "contracts/IPlatform.h"
#include "contracts/UiTheme.h"

using namespace avantgarde;

int main(int argc, char** argv) {
    const char* logPathEnv = std::getenv("AVANTGARDE_LOG_PATH");
    (void)AppDiagnostics::init(logPathEnv ? logPathEnv : "logs/avantgarde.log");
    AppDiagnostics::installCrashHandlers();
    AppDiagnostics::logf(AppLogLevel::Info, "main start argc=%d", argc);

    SamplerUiMode uiMode = SamplerUiMode::GbWindow;
    UiTheme uiTheme = UiTheme::Default;
    bool uiThemeProvided = false;
    uint8_t trackCount = 4;

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
        if (arg.rfind("--tracks=", 0) == 0) {
            char* end = nullptr;
            const long parsed = std::strtol(arg.c_str() + 9, &end, 10);
            if (!end || *end != '\0' || parsed < 1 || parsed > 32) {
                std::printf("Invalid --tracks value: %s (expected 1..32)\n", arg.c_str());
                return 1;
            }
            trackCount = static_cast<uint8_t>(parsed);
            ++argi;
            continue;
        }
        if (arg == "--tracks" && (argi + 1) < argc) {
            char* end = nullptr;
            const long parsed = std::strtol(argv[argi + 1], &end, 10);
            if (!end || *end != '\0' || parsed < 1 || parsed > 32) {
                std::printf("Invalid --tracks value: %s (expected 1..32)\n", argv[argi + 1]);
                return 1;
            }
            trackCount = static_cast<uint8_t>(parsed);
            argi += 2;
            continue;
        }
        if (arg == "--ui") {
            std::printf("Missing value for --ui (expected: gb-window|window)\n");
            return 1;
        }
        if (arg == "--theme") {
            std::printf("Missing value for --theme (expected: default|gothic)\n");
            return 1;
        }
        if (arg == "--tracks") {
            std::printf("Missing value for --tracks (expected: 1..32)\n");
            return 1;
        }
        if (arg.rfind("--", 0) == 0) {
            std::printf("Unknown option: %s\n", arg.c_str());
            return 1;
        }
        break;
    }

    SamplerAppConfig config{};
    config.io.mode = uiMode;
    config.io.theme = uiTheme;
    config.io.themeProvided = uiThemeProvided;
    config.engine.trackCount = trackCount;
    config.audioHost = createDefaultAudioHost();
    if (!config.audioHost) {
        std::printf("Failed to create audio host for current platform\n");
        return 2;
    }

    for (int track = 0; argi < argc; ++argi, ++track) {
        if (track >= trackCount) {
            std::printf("Too many startup clips for --tracks=%u\n", static_cast<unsigned>(trackCount));
            return 1;
        }
        SamplerAppConfig::StartupClipLoad load{};
        load.track = static_cast<uint8_t>(track);
        load.path = argv[argi];
        config.startupClipLoads.push_back(load);
    }

    SamplerApplication app;
    const int rc = app.run(config);
    AppDiagnostics::logf(AppLogLevel::Info, "main exit rc=%d", rc);
    AppDiagnostics::shutdown();
    return rc;
}
