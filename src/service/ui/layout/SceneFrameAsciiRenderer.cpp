#include "service/ui/layout/SceneFrameAsciiRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace avantgarde {
namespace {

struct BorderGlyphs {
    const char* topLeft;
    const char* topRight;
    const char* bottomLeft;
    const char* bottomRight;
    const char* horizontal;
    const char* vertical;
};

constexpr BorderGlyphs kFrameBorder{
    .topLeft = "╔",
    .topRight = "╗",
    .bottomLeft = "╚",
    .bottomRight = "╝",
    .horizontal = "═",
    .vertical = "║",
};

constexpr BorderGlyphs kAsciiBorder{
    .topLeft = "+",
    .topRight = "+",
    .bottomLeft = "+",
    .bottomRight = "+",
    .horizontal = "-",
    .vertical = "|",
};

std::vector<std::string> splitUtf8Cells(std::string_view text) {
    std::vector<std::string> out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if ((c & 0x80u) == 0u) {
            len = 1;
        } else if ((c & 0xE0u) == 0xC0u) {
            len = 2;
        } else if ((c & 0xF0u) == 0xE0u) {
            len = 3;
        } else if ((c & 0xF8u) == 0xF0u) {
            len = 4;
        }
        if (i + len > text.size()) {
            len = 1;
        }
        out.emplace_back(text.substr(i, len));
        i += len;
    }
    return out;
}

std::size_t utf8ColumnsApprox(std::string_view s) noexcept {
    std::size_t cols = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0u) != 0x80u) {
            ++cols;
        }
    }
    return cols;
}

std::string utf8PrefixByColumns(std::string_view s, std::size_t width) {
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
    return std::string(s.substr(0, i));
}

std::string fitUtf8Text(std::string_view s, std::size_t width) {
    if (width == 0U) {
        return std::string(s);
    }
    const std::size_t cols = utf8ColumnsApprox(s);
    if (cols >= width) {
        return utf8PrefixByColumns(s, width);
    }
    std::string out(s);
    out.append(width - cols, ' ');
    return out;
}

void putCell(std::vector<std::vector<std::string>>& canvas,
             int x,
             int y,
             const std::string& glyph) {
    if (y < 0 || y >= static_cast<int>(canvas.size())) {
        return;
    }
    if (x < 0 || x >= static_cast<int>(canvas[y].size())) {
        return;
    }
    canvas[y][x] = glyph;
}

void drawText(std::vector<std::vector<std::string>>& canvas,
              const SceneFrameText& text) {
    if (text.text.empty() && text.width == 0U) {
        return;
    }
    const std::string fitted = (text.width > 0U)
                                   ? fitUtf8Text(text.text, text.width)
                                   : text.text;
    const auto cells = splitUtf8Cells(fitted);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        putCell(canvas, text.x + static_cast<int>(i), text.y, cells[i]);
    }
}

void drawRectWithGlyphs(std::vector<std::vector<std::string>>& canvas,
                        const SceneFrameRect& rect,
                        const BorderGlyphs& glyphs) {
    if (rect.width < 2U || rect.height < 2U) {
        return;
    }
    const int x0 = rect.x;
    const int y0 = rect.y;
    const int x1 = rect.x + static_cast<int>(rect.width) - 1;
    const int y1 = rect.y + static_cast<int>(rect.height) - 1;

    putCell(canvas, x0, y0, glyphs.topLeft);
    putCell(canvas, x1, y0, glyphs.topRight);
    putCell(canvas, x0, y1, glyphs.bottomLeft);
    putCell(canvas, x1, y1, glyphs.bottomRight);
    for (int x = x0 + 1; x < x1; ++x) {
        putCell(canvas, x, y0, glyphs.horizontal);
        putCell(canvas, x, y1, glyphs.horizontal);
    }
    for (int y = y0 + 1; y < y1; ++y) {
        putCell(canvas, x0, y, glyphs.vertical);
        putCell(canvas, x1, y, glyphs.vertical);
    }
}

void drawRect(std::vector<std::vector<std::string>>& canvas,
              const SceneFrameRect& rect) {
    drawRectWithGlyphs(canvas, rect, kFrameBorder);
}

void drawHLine(std::vector<std::vector<std::string>>& canvas,
               const SceneFrameHLine& line) {
    if (line.length == 0U || line.glyph.empty()) {
        return;
    }
    for (uint16_t i = 0; i < line.length; ++i) {
        putCell(canvas, line.x + static_cast<int>(i), line.y, line.glyph);
    }
}

std::string knobBar(float value01, std::size_t width) {
    const float clamped = std::clamp(value01, 0.0f, 1.0f);
    const std::size_t filled = static_cast<std::size_t>(std::lround(clamped * static_cast<float>(width)));
    std::string out;
    out.reserve(width + 2U);
    out.push_back('[');
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(i < filled ? '#' : '.');
    }
    out.push_back(']');
    return out;
}

void drawKnob(std::vector<std::vector<std::string>>& canvas,
              const SceneFrameKnob& knob) {
    const std::string marker = knob.selected ? ">" : " ";
    SceneFrameText t{};
    t.x = knob.x;
    t.y = knob.y;
    t.text = marker + " " + knob.label + " " + knobBar(knob.value01, 8);
    drawText(canvas, t);
}

void drawAnimSlot(std::vector<std::vector<std::string>>& canvas,
                  const SceneFrameAnimSlot& slot) {
    if (slot.width < 4U || slot.height < 3U) {
        return;
    }
    SceneFrameRect r{};
    r.x = slot.x;
    r.y = slot.y;
    r.width = slot.width;
    r.height = slot.height;
    drawRectWithGlyphs(canvas, r, kAsciiBorder);
    if (!slot.label.empty()) {
        SceneFrameText t{};
        t.x = slot.x + 1;
        t.y = slot.y + 1;
        t.text = slot.label;
        drawText(canvas, t);
    }
}

} // namespace

std::vector<std::string> SceneFrameAsciiRenderer::render(const SceneFrame& frame) {
    const std::size_t width = std::max<std::size_t>(frame.width, 1U);
    const std::size_t height = std::max<std::size_t>(frame.height, 1U);

    std::vector<std::vector<std::string>> canvas(
        height,
        std::vector<std::string>(width, " "));

    for (const SceneFrameRect& rect : frame.rects) {
        drawRect(canvas, rect);
    }
    for (const SceneFrameHLine& line : frame.hlines) {
        drawHLine(canvas, line);
    }
    for (const SceneFrameText& text : frame.texts) {
        drawText(canvas, text);
    }
    for (const SceneFrameKnob& knob : frame.knobs) {
        drawKnob(canvas, knob);
    }
    for (const SceneFrameAnimSlot& slot : frame.animSlots) {
        drawAnimSlot(canvas, slot);
    }

    std::vector<std::string> out;
    out.reserve(height);
    for (std::size_t y = 0; y < height; ++y) {
        std::string row;
        for (std::size_t x = 0; x < width; ++x) {
            row += canvas[y][x];
        }
        out.push_back(std::move(row));
    }
    return out;
}

} // namespace avantgarde
