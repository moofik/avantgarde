#include "service/ui/layout/TracksSceneFrameBuilder.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace avantgarde {
namespace {

constexpr std::size_t kTracksPerPage = 2;

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
    if (maxLen <= 3U) {
        return clipName.substr(0, maxLen);
    }
    return clipName.substr(0, maxLen - 3U) + "...";
}

std::size_t utf8ColumnsApprox(const std::string& s) noexcept {
    std::size_t cols = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0u) != 0x80u) {
            ++cols;
        }
    }
    return cols;
}

std::string utf8PrefixByColumns(const std::string& s, std::size_t width) {
    if (width == 0U || s.empty()) {
        return {};
    }
    std::size_t i = 0;
    std::size_t cols = 0;
    while (i < s.size() && cols < width) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u) {
            ++i;
        }
        ++cols;
    }
    return s.substr(0, i);
}

std::string padRight(const std::string& s, std::size_t width) {
    const std::size_t cols = utf8ColumnsApprox(s);
    if (cols >= width) {
        return utf8PrefixByColumns(s, width);
    }
    std::string out = s;
    out.append(width - cols, ' ');
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

float speedTo01(float speed) noexcept {
    constexpr float kMin = 0.25f;
    constexpr float kMax = 4.0f;
    const float clamped = std::clamp(speed, kMin, kMax);
    return (clamped - kMin) / (kMax - kMin);
}

void pushLine(SceneFrame& frame, int y, const std::string& text) {
    SceneFrameText t{};
    t.x = 1;
    t.y = static_cast<int16_t>(y);
    t.text = text;
    frame.texts.push_back(std::move(t));
}

} // namespace

SceneFrame TracksSceneFrameBuilder::build(const UiState& state,
                                          const UiNavState& navState,
                                          uint16_t width,
                                          std::string_view headerTitle,
                                          std::string_view actionStatusLine,
                                          std::string_view keysHintLine) {
    SceneFrame frame{};

    const std::size_t w = std::max<std::size_t>(width, 34U);
    const std::size_t inner = w - 2U;
    const std::size_t clipWidth = (inner > 38U) ? (inner - 38U) : 16U;
    const std::size_t meterWidth = (inner > 40U) ? std::min<std::size_t>(inner - 40U, 24U) : 12U;

    const std::size_t totalTracks = state.tracks.size();
    const std::size_t totalPages = std::max<std::size_t>(1U, (totalTracks + kTracksPerPage - 1U) / kTracksPerPage);
    const std::size_t activeTrack = (totalTracks == 0U)
                                        ? 0U
                                        : std::min<std::size_t>(state.transport.activeTrack, totalTracks - 1U);
    std::size_t pageIndex = (totalTracks == 0U) ? 0U : (activeTrack / kTracksPerPage);
    pageIndex = std::min<std::size_t>(pageIndex, totalPages - 1U);
    if (totalTracks > 0U) {
        pageIndex = std::min<std::size_t>(navState.trackPage, totalPages - 1U);
    }
    const std::size_t pageStart = pageIndex * kTracksPerPage;
    const std::size_t pageEnd = std::min<std::size_t>(pageStart + kTracksPerPage, totalTracks);

    const std::size_t shownTrackCount = (pageEnd > pageStart) ? (pageEnd - pageStart) : 0U;
    const std::size_t sepInsideTracks = (shownTrackCount > 1U) ? (shownTrackCount - 1U) : 0U;

    const std::size_t fixedInnerRows = 1U /*empty*/ + 1U /*title*/ + 1U /*trn*/ + 1U /*active*/
        + 1U /*sep*/ + 1U /*sep before footer*/ + 1U /*action*/ + 1U /*keys*/;
    const std::size_t trackRows = (shownTrackCount == 0U) ? 1U : shownTrackCount * 4U + sepInsideTracks;
    const std::size_t innerRows = fixedInnerRows + trackRows;

    frame.width = static_cast<uint16_t>(w);
    frame.height = static_cast<uint16_t>(innerRows + 2U);
    frame.rects.push_back(SceneFrameRect{
        .x = 0,
        .y = 0,
        .width = static_cast<uint16_t>(w),
        .height = static_cast<uint16_t>(innerRows + 2U),
    });

    int y = 1;
    pushLine(frame, y++, padRight(" ", inner));

    std::string titleLine = " ";
    titleLine += headerTitle.empty() ? std::string("AVANTGARDE") : std::string(headerTitle);
    titleLine += " ";
    pushLine(frame, y++, padRight(titleLine, inner));

    char line[256]{};
    std::snprintf(line, sizeof(line), " TRN:%s BPM:%5.1f TS:%u/%u Q:%s OVF:%c ",
                  state.transport.playing ? "PLAY" : "STOP",
                  state.transport.bpm,
                  static_cast<unsigned>(state.transport.tsNum),
                  static_cast<unsigned>(state.transport.tsDen),
                  quantToStr(state.transport.quant),
                  state.telemetry.rtQueueOverflow ? 'Y' : 'N');
    pushLine(frame, y++, padRight(line, inner));

    std::snprintf(line, sizeof(line), " ACTIVE:T%u XRUN:%llu PG:%u/%u ",
                  static_cast<unsigned>(activeTrack + 1U),
                  static_cast<unsigned long long>(state.telemetry.xruns),
                  static_cast<unsigned>(pageIndex + 1U),
                  static_cast<unsigned>(totalPages));
    pushLine(frame, y++, padRight(line, inner));

    frame.hlines.push_back(SceneFrameHLine{.x = 1, .y = static_cast<int16_t>(y++), .length = static_cast<uint16_t>(inner), .glyph = "═"});

    if (totalTracks == 0U) {
        pushLine(frame, y++, padRight(" no tracks configured ", inner));
    } else {
        for (std::size_t i = pageStart; i < pageEnd; ++i) {
            const UiTrackStateView& tr = state.tracks[i];
            const bool active = (tr.id == activeTrack);
            const char* marker = active ? "▶" : " ";

            std::snprintf(line, sizeof(line), " %s T%u %-5s clip:%s",
                          marker,
                          static_cast<unsigned>(tr.id + 1U),
                          trackStateToStr(tr.state),
                          clipShort(tr.clipName, clipWidth).c_str());
            pushLine(frame, y++, padRight(line, inner));

            std::snprintf(line, sizeof(line), "   bars:%u  fx:%u  loop:%c  m:%c  a:%c",
                          static_cast<unsigned>(tr.bars),
                          static_cast<unsigned>(tr.fxCount),
                          tr.loop ? 'Y' : 'N',
                          tr.muted ? 'Y' : 'N',
                          tr.armed ? 'Y' : 'N');
            pushLine(frame, y++, padRight(line, inner));

            std::snprintf(line, sizeof(line), "   spd:%1.2f [%s]",
                          tr.stretchRatio,
                          makeBar(speedTo01(tr.stretchRatio), meterWidth).c_str());
            pushLine(frame, y++, padRight(line, inner));

            std::snprintf(line, sizeof(line), "   g  :%1.2f [%s]",
                          tr.gain01,
                          makeBar(tr.gain01, meterWidth).c_str());
            pushLine(frame, y++, padRight(line, inner));

            if (i + 1U < pageEnd) {
                frame.hlines.push_back(SceneFrameHLine{
                    .x = 1,
                    .y = static_cast<int16_t>(y++),
                    .length = static_cast<uint16_t>(inner),
                    .glyph = "─"});
            }
        }
    }

    frame.hlines.push_back(SceneFrameHLine{.x = 1, .y = static_cast<int16_t>(y++), .length = static_cast<uint16_t>(inner), .glyph = "═"});
    if (!actionStatusLine.empty()) {
        pushLine(frame, y++, padRight(std::string(actionStatusLine), inner));
    } else {
        pushLine(frame, y++, padRight(" action: - ", inner));
    }

    const std::string defaultKeys = " keys [j/k focus] [/? adj] [o apply] [F2 undo] [F9 redo] [p/s] [u/i/t/r] [q] ";
    const std::string keys = keysHintLine.empty() ? defaultKeys : std::string(keysHintLine);
    pushLine(frame, y++, padRight(keys, inner));

    return frame;
}

} // namespace avantgarde

