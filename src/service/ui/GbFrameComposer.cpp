#include "service/ui/GbFrameComposer.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string_view>

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

} // namespace

std::string GbFrameComposer::buildMonochromeFrame(const UiState& state,
                                                  uint16_t width,
                                                  std::string_view headerTitle,
                                                  std::optional<std::size_t> pageOverride,
                                                  std::string_view actionStatusLine,
                                                  std::string_view keysHintLine) {
    if (width < 4) {
        return {};
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t inner = w - 2;
    const std::size_t clipWidth = (inner > 38) ? (inner - 38) : 16;
    const std::size_t meterWidth = (inner > 40) ? std::min<std::size_t>(inner - 40, 24) : 12;

    std::ostringstream out;
    out << "╔" << repeatUtf8("═", inner) << "╗\n";
    out << "║" << padRight(" ", inner) << "║\n";
    std::string titleLine = " ";
    titleLine += headerTitle;
    titleLine += " ";
    out << "║" << padRight(titleLine, inner) << "║\n";

    char line[256]{};
    std::snprintf(line, sizeof(line), " TRN:%s BPM:%5.1f TS:%u/%u Q:%s OVF:%c ",
                  state.transport.playing ? "PLAY" : "STOP",
                  state.transport.bpm,
                  static_cast<unsigned>(state.transport.tsNum),
                  static_cast<unsigned>(state.transport.tsDen),
                  quantToStr(state.transport.quant),
                  state.telemetry.rtQueueOverflow ? 'Y' : 'N');
    out << "║" << padRight(line, inner) << "║\n";

    const std::size_t totalTracks = state.tracks.size();
    const std::size_t pageSize = 1;
    const std::size_t totalPages = std::max<std::size_t>(1, (totalTracks + pageSize - 1U) / pageSize);
    const std::size_t activeTrack = (totalTracks == 0)
                                        ? 0U
                                        : std::min<std::size_t>(state.transport.activeTrack, totalTracks - 1U);
    std::size_t pageIndex = (totalTracks == 0) ? 0U : (activeTrack / pageSize);
    if (pageOverride.has_value() && totalTracks > 0) {
        pageIndex = std::min<std::size_t>(pageOverride.value(), totalPages - 1U);
    }
    const std::size_t pageStart = pageIndex * pageSize;
    const std::size_t pageEnd = std::min<std::size_t>(pageStart + pageSize, totalTracks);

    std::snprintf(line, sizeof(line), " ACTIVE:T%u XRUN:%llu PG:%u/%u ",
                  static_cast<unsigned>(activeTrack + 1U),
                  static_cast<unsigned long long>(state.telemetry.xruns),
                  static_cast<unsigned>(pageIndex + 1U),
                  static_cast<unsigned>(totalPages));
    out << "║" << padRight(line, inner) << "║\n";
    out << "╠" << repeatUtf8("═", inner) << "╣\n";

    if (totalTracks == 0) {
        out << "║" << padRight(" no tracks configured ", inner) << "║\n";
    }

    for (std::size_t i = pageStart; i < pageEnd; ++i) {
        const auto& tr = state.tracks[i];
        const uint8_t uiTrackIndex = static_cast<uint8_t>(i);
        const bool active = (uiTrackIndex == activeTrack);
        const char* marker = active ? "▶" : " ";

        std::snprintf(line, sizeof(line), " %s T%u %-5s clip:%s",
                      marker,
                      static_cast<unsigned>(uiTrackIndex + 1),
                      trackStateToStr(tr.state),
                      clipShort(tr.clipName, clipWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   bars:%u  fx:%u  loop:%c  m:%c  a:%c",
                      static_cast<unsigned>(tr.bars),
                      static_cast<unsigned>(tr.fxCount),
                      tr.loop ? 'Y' : 'N',
                      tr.muted ? 'Y' : 'N',
                      tr.armed ? 'Y' : 'N');
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   spd:%1.2f [%s]",
                      tr.stretchRatio,
                      makeBar(speedTo01(tr.stretchRatio), meterWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        std::snprintf(line, sizeof(line), "   g  :%1.2f [%s]",
                      tr.gain01,
                      makeBar(tr.gain01, meterWidth).c_str());
        out << "║" << padRight(line, inner) << "║\n";

        if (i + 1U < pageEnd) {
            out << "╟" << repeatUtf8("─", inner) << "╢\n";
        }
    }

    out << "╠" << repeatUtf8("═", inner) << "╣\n";
    if (!actionStatusLine.empty()) {
        out << "║" << padRight(std::string(actionStatusLine), inner) << "║\n";
    }
    const std::string defaultKeys = " keys [j/k focus] [/? adj] [o apply] [F2 undo] [F9 redo] [p/s] [u/i/t/r] [q] ";
    const std::string keys = keysHintLine.empty() ? defaultKeys : std::string(keysHintLine);
    out << "║" << padRight(keys, inner)
        << "║\n";
    out << "╚" << repeatUtf8("═", inner) << "╝\n";
    return out.str();
}

} // namespace avantgarde
