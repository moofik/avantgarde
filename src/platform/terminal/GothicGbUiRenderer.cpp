#include "platform/terminal/GothicGbUiRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string_view>

#include <unistd.h>

namespace avantgarde {
namespace {

const char* quantToStr(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return "NONE";
        case QuantizeMode::Beat: return "BEAT";
        case QuantizeMode::Bar: return "BAR ";
        default: return "UNK ";
    }
}

const char* trackStateToStr(UiTrackState s) noexcept {
    switch (s) {
        case UiTrackState::Empty: return "EMPTY";
        case UiTrackState::Stopped: return "STOP ";
        case UiTrackState::Playing: return "PLAY ";
        case UiTrackState::Recording: return "REC  ";
        default: return "UNK  ";
    }
}

std::string clipShort(const std::string& clipName, std::size_t maxLen) {
    if (clipName.empty()) {
        return "-";
    }
    if (clipName.size() <= maxLen) {
        return clipName;
    }
    if (maxLen <= 3) {
        return clipName.substr(0, maxLen);
    }
    return clipName.substr(0, maxLen - 3) + "...";
}

bool isUtf8ContinuationByte(unsigned char c) noexcept {
    return (c & 0xC0u) == 0x80u;
}

std::size_t utf8ColumnsApprox(const std::string& s) noexcept {
    std::size_t columns = 0;
    for (unsigned char c : s) {
        if (!isUtf8ContinuationByte(c)) {
            ++columns;
        }
    }
    return columns;
}

std::string utf8PrefixByColumns(const std::string& s, std::size_t columns) {
    if (columns == 0 || s.empty()) {
        return {};
    }
    std::size_t i = 0;
    std::size_t used = 0;
    while (i < s.size() && used < columns) {
        ++i;
        while (i < s.size() && isUtf8ContinuationByte(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        ++used;
    }
    return s.substr(0, i);
}

std::string padRight(const std::string& s, std::size_t width) {
    const std::size_t columns = utf8ColumnsApprox(s);
    if (columns >= width) {
        return utf8PrefixByColumns(s, width);
    }
    std::string out = s;
    out.append(width - columns, ' ');
    return out;
}

std::string makeBar(float value01, std::size_t width) {
    const float v = std::clamp(value01, 0.0f, 1.0f);
    const std::size_t filled = static_cast<std::size_t>(v * static_cast<float>(width));
    std::string out;
    out.reserve(width);
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(i < filled ? '#' : '.');
    }
    return out;
}

std::string repeatUtf8(std::string_view token, std::size_t count) {
    std::string out;
    out.reserve(token.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += token;
    }
    return out;
}

float speedTo01(float speed) noexcept {
    constexpr float kMin = 0.25f;
    constexpr float kMax = 4.0f;
    const float clamped = std::clamp(speed, kMin, kMax);
    return (clamped - kMin) / (kMax - kMin);
}

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
    if (width < 4) {
        return {};
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t inner = w - 2;
    const std::size_t clipWidth = (inner > 38) ? (inner - 38) : 16;
    const std::size_t meterWidth = (inner > 40) ? std::min<std::size_t>(inner - 40, 24) : 12;

    std::ostringstream out;
    out << "╔" << repeatUtf8("═", inner) << "╗\n";
    out << "║" << padRight(" AVANTGARDE ", inner) << "║\n";

    char line[256]{};
    std::snprintf(line, sizeof(line), " TRN:%s BPM:%5.1f TS:%u/%u Q:%s OVF:%c ",
                  state.transport.playing ? "PLAY" : "STOP",
                  state.transport.bpm,
                  static_cast<unsigned>(state.transport.tsNum),
                  static_cast<unsigned>(state.transport.tsDen),
                  quantToStr(state.transport.quant),
                  state.telemetry.rtQueueOverflow ? 'Y' : 'N');
    out << "║" << padRight(line, inner) << "║\n";

    std::snprintf(line, sizeof(line), " ACTIVE:T%u XRUN:%llu ",
                  static_cast<unsigned>(state.transport.activeTrack + 1),
                  static_cast<unsigned long long>(state.telemetry.xruns));
    out << "║" << padRight(line, inner) << "║\n";
    out << "╠" << repeatUtf8("═", inner) << "╣\n";

    for (std::size_t i = 0; i < state.tracks.size(); ++i) {
        const auto& tr = state.tracks[i];
        const bool active = (tr.id == state.transport.activeTrack);
        const char* marker = active ? "▶" : " ";

        std::snprintf(line, sizeof(line), " %s T%u %-5s clip:%s",
                      marker,
                      static_cast<unsigned>(tr.id + 1),
                      trackStateToStr(tr.state),
                      clipShort(tr.clipName, clipWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   bars:%u  fx:%u  loop:%c",
                      static_cast<unsigned>(tr.bars),
                      static_cast<unsigned>(tr.fxCount),
                      tr.loop ? 'Y' : 'N');
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   spd:%1.2f [%s]",
                      tr.stretchRatio,
                      makeBar(speedTo01(tr.stretchRatio), meterWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   g  :%1.2f [%s]",
                      tr.gain01,
                      makeBar(tr.gain01, meterWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        if (i + 1U < state.tracks.size()) {
            out << "╟" << repeatUtf8("─", inner) << "╢\n";
        }
    }

    out << "╠" << repeatUtf8("═", inner) << "╣\n";
    out << "║" << padRight(" keys [1/2] [p/s] [-/=] [z/x/c] [[/]] [q] ", inner) << "║\n";
    out << "╚" << repeatUtf8("═", inner) << "╝\n";
    return out.str();
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
        const bool isActiveTrackLine = line.find("▶ T1") != std::string::npos || line.find("▶ T2") != std::string::npos;

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
    const std::string frame = ansiCapable_ ? colorizeFrame_(state, mono) : mono;

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
