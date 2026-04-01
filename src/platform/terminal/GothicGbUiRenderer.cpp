#include "platform/terminal/GothicGbUiRenderer.h"
#include "service/ui/GbFrameComposer.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string_view>

#include <unistd.h>

namespace avantgarde {
namespace {

std::string fgRgb(uint8_t r, uint8_t g, uint8_t b) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%u;%u;%um",
                  static_cast<unsigned>(r),
                  static_cast<unsigned>(g),
                  static_cast<unsigned>(b));
    return buf;
}

std::string bgRgb(uint8_t r, uint8_t g, uint8_t b) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%u;%u;%um",
                  static_cast<unsigned>(r),
                  static_cast<unsigned>(g),
                  static_cast<unsigned>(b));
    return buf;
}

std::string withStyle(const std::string& line, const GothicGbUiRenderer::Rgb& fg, const GothicGbUiRenderer::Rgb& bg) {
    return fgRgb(fg.r, fg.g, fg.b) + bgRgb(bg.r, bg.g, bg.b) + line + "\x1b[0m";
}

} // namespace

GothicGbUiRenderer::GothicGbUiRenderer(UiTheme theme, uint16_t width) noexcept
    : palette_(uiThemePalette(theme)),
      width_(width) {}

GothicGbUiRenderer::~GothicGbUiRenderer() {
    if (enteredAltScreen_) {
        std::printf("\x1b[?25h\x1b[?1049l");
        std::fflush(stdout);
    }
}

void GothicGbUiRenderer::ensureTerminalReady_() noexcept {
    if (ansiCapable_ || enteredAltScreen_) {
        return;
    }
    const char* term = std::getenv("TERM");
    ansiCapable_ = (isatty(fileno(stdout)) != 0) && term && std::string_view(term) != "dumb";
    if (ansiCapable_) {
        std::printf("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
        std::fflush(stdout);
        enteredAltScreen_ = true;
    }
}

std::string GothicGbUiRenderer::buildMonochromeFrame(const UiState& state, uint16_t width) {
    return GbFrameComposer::buildMonochromeFrame(state, width);
}

std::string GothicGbUiRenderer::colorizeFrame_(const UiState& state, const std::string& monoFrame) const {
    (void)state;
    std::istringstream in(monoFrame);
    std::string line;
    std::ostringstream out;
    while (std::getline(in, line)) {
        const bool isBorder = line.find("╔") == 0 || line.find("╚") == 0 ||
                              line.find("╠") == 0 || line.find("╟") == 0;
        const bool isHeader = line.find(" AVANTGARDE ") != std::string::npos;
        const bool isTransport = line.find(" TRN:") != std::string::npos || line.find(" ACTIVE:") != std::string::npos;
        const bool isActiveTrackLine = line.find("▶ T") != std::string::npos;

        if (isBorder) {
            out << withStyle(line, palette_.mid, palette_.bg) << '\n';
        } else if (isHeader || isTransport || isActiveTrackLine) {
            out << withStyle(line, palette_.text, palette_.panel) << '\n';
        } else {
            out << withStyle(line, palette_.text, palette_.bg) << '\n';
        }
    }
    return out.str();
}

void GothicGbUiRenderer::render(const UiState& state) {
    ensureTerminalReady_();

    const std::string mono = buildMonochromeFrame(state, width_);
    renderCustomFrame(mono);
}

void GothicGbUiRenderer::renderCustomFrame(const std::string& monoFrame) {
    ensureTerminalReady_();

    const std::string frame = ansiCapable_ ? colorizeFrame_(UiState{}, monoFrame) : monoFrame;
    if (frame == lastFrame_) {
        return;
    }
    lastFrame_ = frame;

    if (ansiCapable_) {
        std::printf("\x1b[H%s", frame.c_str());
    } else {
        std::printf("\n----- GOTHIC UI FRAME -----\n%s", frame.c_str());
    }
    std::fflush(stdout);
}

} // namespace avantgarde
