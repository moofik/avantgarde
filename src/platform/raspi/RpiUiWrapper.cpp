#include "platform/raspi/RpiUiWrapper.h"

#include "app/AppDiagnostics.h"
#include "platform/render/PreparedLayoutUtils.h"
#include "platform/render/RenderGeometry.h"
#include "service/ui/layout/UiLayoutEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace avantgarde::raspi {
namespace {

struct Rgba {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

constexpr Rgba kBgMain{11, 10, 15, 255};
constexpr Rgba kBgPanel{19, 13, 22, 255};
constexpr Rgba kBorder{143, 110, 149, 255};
constexpr Rgba kText{214, 209, 230, 255};
constexpr Rgba kAccent{200, 28, 99, 255};

uint8_t clampU8(int value) noexcept {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool parseHexColor(std::string_view raw, Rgba& out) {
    if (raw.empty()) {
        return false;
    }
    std::string s(raw);
    if (!s.empty() && s.front() == '#') {
        s.erase(s.begin());
    }
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    auto hexByte = [&](char hi, char lo) -> int {
        const int h = hexValue(hi);
        const int l = hexValue(lo);
        if (h < 0 || l < 0) {
            return -1;
        }
        return (h << 4) | l;
    };

    int rr = 0, gg = 0, bb = 0, aa = 255;
    if (s.size() == 3U || s.size() == 4U) {
        const int r = hexValue(s[0]);
        const int g = hexValue(s[1]);
        const int b = hexValue(s[2]);
        if (r < 0 || g < 0 || b < 0) {
            return false;
        }
        rr = (r << 4) | r;
        gg = (g << 4) | g;
        bb = (b << 4) | b;
        if (s.size() == 4U) {
            const int a = hexValue(s[3]);
            if (a < 0) {
                return false;
            }
            aa = (a << 4) | a;
        }
    } else if (s.size() == 6U || s.size() == 8U) {
        rr = hexByte(s[0], s[1]);
        gg = hexByte(s[2], s[3]);
        bb = hexByte(s[4], s[5]);
        if (rr < 0 || gg < 0 || bb < 0) {
            return false;
        }
        if (s.size() == 8U) {
            aa = hexByte(s[6], s[7]);
            if (aa < 0) {
                return false;
            }
        }
    } else {
        return false;
    }

    out = Rgba{
        static_cast<uint8_t>(rr),
        static_cast<uint8_t>(gg),
        static_cast<uint8_t>(bb),
        static_cast<uint8_t>(aa)};
    return true;
}

Rgba resolveColor(std::string_view spec, Rgba fallback) {
    Rgba out{};
    if (parseHexColor(spec, out)) {
        return out;
    }
    return fallback;
}

Rgba blendOver(Rgba src, Rgba dst, float alpha01) noexcept {
    const float a = std::clamp(alpha01, 0.0f, 1.0f) * (static_cast<float>(src.a) / 255.0f);
    const float ia = 1.0f - a;
    return Rgba{
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.r) * a + static_cast<float>(dst.r) * ia))),
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.g) * a + static_cast<float>(dst.g) * ia))),
        clampU8(static_cast<int>(std::lround(static_cast<float>(src.b) * a + static_cast<float>(dst.b) * ia))),
        255};
}

struct RectI {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

RectI toRectI(const render::UiRectPx& r) noexcept {
    return RectI{
        static_cast<int>(std::lround(r.x)),
        static_cast<int>(std::lround(r.y)),
        std::max(0, static_cast<int>(std::lround(r.w))),
        std::max(0, static_cast<int>(std::lround(r.h)))};
}

class Canvas final {
public:
    void resize(uint16_t w, uint16_t h) {
        width_ = std::max<uint16_t>(1U, w);
        height_ = std::max<uint16_t>(1U, h);
        pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), pack(kBgMain));
    }

    [[nodiscard]] uint16_t width() const noexcept { return width_; }
    [[nodiscard]] uint16_t height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<uint32_t>& pixels() const noexcept { return pixels_; }

    void clear(Rgba color) {
        std::fill(pixels_.begin(), pixels_.end(), pack(color));
    }

    void fillRect(const RectI& rect, Rgba color, float alpha01 = 1.0f) {
        const RectI r = clip(rect);
        if (r.w <= 0 || r.h <= 0) {
            return;
        }
        for (int y = r.y; y < r.y + r.h; ++y) {
            const std::size_t row =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            for (int x = r.x; x < r.x + r.w; ++x) {
                blendPixel(row + static_cast<std::size_t>(x), color, alpha01);
            }
        }
    }

    void strokeRect(const RectI& rect, Rgba color, int thickness = 1, float alpha01 = 1.0f) {
        if (thickness <= 0) {
            return;
        }
        const int t = std::max(1, thickness);
        fillRect(RectI{rect.x, rect.y, rect.w, t}, color, alpha01);
        fillRect(RectI{rect.x, rect.y + rect.h - t, rect.w, t}, color, alpha01);
        fillRect(RectI{rect.x, rect.y, t, rect.h}, color, alpha01);
        fillRect(RectI{rect.x + rect.w - t, rect.y, t, rect.h}, color, alpha01);
    }

    void line(int x0, int y0, int x1, int y1, Rgba color, float alpha01 = 1.0f) {
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            setPixel(x0, y0, color, alpha01);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = err * 2;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void circle(int cx, int cy, int r, Rgba color, float alpha01 = 1.0f) {
        if (r <= 0) {
            return;
        }
        int x = r;
        int y = 0;
        int d = 1 - x;
        while (y <= x) {
            setPixel(cx + x, cy + y, color, alpha01);
            setPixel(cx + y, cy + x, color, alpha01);
            setPixel(cx - y, cy + x, color, alpha01);
            setPixel(cx - x, cy + y, color, alpha01);
            setPixel(cx - x, cy - y, color, alpha01);
            setPixel(cx - y, cy - x, color, alpha01);
            setPixel(cx + y, cy - x, color, alpha01);
            setPixel(cx + x, cy - y, color, alpha01);
            ++y;
            if (d <= 0) {
                d += 2 * y + 1;
            } else {
                --x;
                d += 2 * (y - x) + 1;
            }
        }
    }

private:
    static uint32_t pack(Rgba c) noexcept {
        return (static_cast<uint32_t>(c.r) << 24U) |
               (static_cast<uint32_t>(c.g) << 16U) |
               (static_cast<uint32_t>(c.b) << 8U) |
               static_cast<uint32_t>(c.a);
    }

    static Rgba unpack(uint32_t v) noexcept {
        return Rgba{
            static_cast<uint8_t>((v >> 24U) & 0xFFU),
            static_cast<uint8_t>((v >> 16U) & 0xFFU),
            static_cast<uint8_t>((v >> 8U) & 0xFFU),
            static_cast<uint8_t>(v & 0xFFU)};
    }

    RectI clip(const RectI& in) const noexcept {
        RectI out = in;
        if (out.x < 0) {
            out.w += out.x;
            out.x = 0;
        }
        if (out.y < 0) {
            out.h += out.y;
            out.y = 0;
        }
        const int maxW = static_cast<int>(width_);
        const int maxH = static_cast<int>(height_);
        if (out.x + out.w > maxW) {
            out.w = maxW - out.x;
        }
        if (out.y + out.h > maxH) {
            out.h = maxH - out.y;
        }
        out.w = std::max(0, out.w);
        out.h = std::max(0, out.h);
        return out;
    }

    void setPixel(int x, int y, Rgba color, float alpha01) {
        if (x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) {
            return;
        }
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
        blendPixel(index, color, alpha01);
    }

    void blendPixel(std::size_t index, Rgba color, float alpha01) {
        Rgba dst = unpack(pixels_[index]);
        pixels_[index] = pack(blendOver(color, dst, alpha01));
    }

    uint16_t width_{1};
    uint16_t height_{1};
    std::vector<uint32_t> pixels_{};
};

#if defined(__linux__)
class Framebuffer final {
public:
    ~Framebuffer() { shutdown(); }

    bool init(const RpiUiWrapperConfig& cfg) {
        shutdown();
        const char* fbPath = "/dev/fb0";
        fd_ = ::open(fbPath, O_RDWR);
        if (fd_ < 0) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiUiRenderer: cannot open %s: %s",
                                 fbPath,
                                 std::strerror(errno));
            return false;
        }

        if (::ioctl(fd_, FBIOGET_FSCREENINFO, &fix_) < 0 ||
            ::ioctl(fd_, FBIOGET_VSCREENINFO, &var_) < 0) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiUiRenderer: ioctl(FBIOGET_*) failed: %s",
                                 std::strerror(errno));
            shutdown();
            return false;
        }

        if (var_.xres == 0U || var_.yres == 0U || fix_.line_length == 0U) {
            AppDiagnostics::logf(AppLogLevel::Warn, "RpiUiRenderer: invalid fb geometry");
            shutdown();
            return false;
        }

        width_ = static_cast<uint16_t>(std::min<uint32_t>(var_.xres, 65535U));
        height_ = static_cast<uint16_t>(std::min<uint32_t>(var_.yres, 65535U));
        if (cfg.width > 0 && cfg.height > 0) {
            width_ = static_cast<uint16_t>(std::min<uint16_t>(width_, cfg.width));
            height_ = static_cast<uint16_t>(std::min<uint16_t>(height_, cfg.height));
        }

        mapSize_ = static_cast<std::size_t>(fix_.line_length) * static_cast<std::size_t>(var_.yres);
        fbMem_ = static_cast<uint8_t*>(::mmap(nullptr, mapSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (!fbMem_ || fbMem_ == MAP_FAILED) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiUiRenderer: mmap failed: %s",
                                 std::strerror(errno));
            fbMem_ = nullptr;
            shutdown();
            return false;
        }

        AppDiagnostics::logf(AppLogLevel::Info,
                             "RpiUiRenderer: framebuffer ready (%ux%u, bpp=%u)",
                             static_cast<unsigned>(width_),
                             static_cast<unsigned>(height_),
                             static_cast<unsigned>(var_.bits_per_pixel));
        return true;
    }

    void shutdown() noexcept {
        if (fbMem_) {
            ::munmap(fbMem_, mapSize_);
            fbMem_ = nullptr;
        }
        mapSize_ = 0;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        width_ = 0;
        height_ = 0;
        std::memset(&fix_, 0, sizeof(fix_));
        std::memset(&var_, 0, sizeof(var_));
    }

    [[nodiscard]] bool ready() const noexcept {
        return (fd_ >= 0) && (fbMem_ != nullptr) && width_ > 0 && height_ > 0;
    }

    [[nodiscard]] uint16_t width() const noexcept { return width_; }
    [[nodiscard]] uint16_t height() const noexcept { return height_; }

    void present(const Canvas& canvas) noexcept {
        if (!ready()) {
            return;
        }
        const uint16_t w = std::min<uint16_t>(canvas.width(), width_);
        const uint16_t h = std::min<uint16_t>(canvas.height(), height_);
        const auto& src = canvas.pixels();
        for (uint16_t y = 0; y < h; ++y) {
            uint8_t* dstRow = fbMem_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(fix_.line_length);
            const std::size_t srcRow = static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width());
            for (uint16_t x = 0; x < w; ++x) {
                const uint32_t px = src[srcRow + static_cast<std::size_t>(x)];
                const uint8_t r = static_cast<uint8_t>((px >> 24U) & 0xFFU);
                const uint8_t g = static_cast<uint8_t>((px >> 16U) & 0xFFU);
                const uint8_t b = static_cast<uint8_t>((px >> 8U) & 0xFFU);

                if (var_.bits_per_pixel == 16) {
                    const uint16_t rr =
                        static_cast<uint16_t>((static_cast<uint32_t>(r) * ((1U << var_.red.length) - 1U)) / 255U);
                    const uint16_t gg =
                        static_cast<uint16_t>((static_cast<uint32_t>(g) * ((1U << var_.green.length) - 1U)) / 255U);
                    const uint16_t bb =
                        static_cast<uint16_t>((static_cast<uint32_t>(b) * ((1U << var_.blue.length) - 1U)) / 255U);
                    const uint16_t packed = static_cast<uint16_t>(
                        (rr << var_.red.offset) |
                        (gg << var_.green.offset) |
                        (bb << var_.blue.offset));
                    reinterpret_cast<uint16_t*>(dstRow)[x] = packed;
                } else {
                    const uint32_t rr =
                        (static_cast<uint32_t>(r) * ((1U << var_.red.length) - 1U) / 255U) << var_.red.offset;
                    const uint32_t gg =
                        (static_cast<uint32_t>(g) * ((1U << var_.green.length) - 1U) / 255U) << var_.green.offset;
                    const uint32_t bb =
                        (static_cast<uint32_t>(b) * ((1U << var_.blue.length) - 1U) / 255U) << var_.blue.offset;
                    const uint32_t aa =
                        (var_.transp.length > 0U)
                            ? (((1U << var_.transp.length) - 1U) << var_.transp.offset)
                            : 0U;
                    reinterpret_cast<uint32_t*>(dstRow)[x] = rr | gg | bb | aa;
                }
            }
        }
    }

private:
    int fd_{-1};
    uint8_t* fbMem_{nullptr};
    std::size_t mapSize_{0};
    fb_fix_screeninfo fix_{};
    fb_var_screeninfo var_{};
    uint16_t width_{0};
    uint16_t height_{0};
};
#endif

struct Glyph3x5 {
    // 5 rows, 3 bits per row.
    std::array<uint8_t, 5> rows{};
};

const Glyph3x5* glyphFor(char c) {
    static const Glyph3x5 kUnknown{{0b111, 0b101, 0b010, 0b000, 0b010}};
    static const Glyph3x5 kSpace{{0, 0, 0, 0, 0}};
    static const Glyph3x5 kDot{{0, 0, 0, 0, 0b010}};
    static const Glyph3x5 kColon{{0, 0b010, 0, 0b010, 0}};
    static const Glyph3x5 kDash{{0, 0, 0b111, 0, 0}};
    static const Glyph3x5 kUnderscore{{0, 0, 0, 0, 0b111}};
    static const Glyph3x5 kSlash{{0b001, 0b001, 0b010, 0b100, 0b100}};
    static const Glyph3x5 kLBracket{{0b110, 0b100, 0b100, 0b100, 0b110}};
    static const Glyph3x5 kRBracket{{0b011, 0b001, 0b001, 0b001, 0b011}};
    static const Glyph3x5 kLParen{{0b010, 0b100, 0b100, 0b100, 0b010}};
    static const Glyph3x5 kRParen{{0b010, 0b001, 0b001, 0b001, 0b010}};
    static const Glyph3x5 kLess{{0b001, 0b010, 0b100, 0b010, 0b001}};
    static const Glyph3x5 kGreater{{0b100, 0b010, 0b001, 0b010, 0b100}};
    static const Glyph3x5 kPipe{{0b010, 0b010, 0b010, 0b010, 0b010}};
    static const Glyph3x5 kEq{{0b000, 0b111, 0b000, 0b111, 0b000}};
    static const Glyph3x5 kPlus{{0b000, 0b010, 0b111, 0b010, 0b000}};

    static const std::array<Glyph3x5, 10> kDigits{{
        {{0b111, 0b101, 0b101, 0b101, 0b111}}, // 0
        {{0b010, 0b110, 0b010, 0b010, 0b111}}, // 1
        {{0b111, 0b001, 0b111, 0b100, 0b111}}, // 2
        {{0b111, 0b001, 0b111, 0b001, 0b111}}, // 3
        {{0b101, 0b101, 0b111, 0b001, 0b001}}, // 4
        {{0b111, 0b100, 0b111, 0b001, 0b111}}, // 5
        {{0b111, 0b100, 0b111, 0b101, 0b111}}, // 6
        {{0b111, 0b001, 0b001, 0b001, 0b001}}, // 7
        {{0b111, 0b101, 0b111, 0b101, 0b111}}, // 8
        {{0b111, 0b101, 0b111, 0b001, 0b111}}, // 9
    }};

    static const std::array<Glyph3x5, 26> kLetters{{
        {{0b010, 0b101, 0b111, 0b101, 0b101}}, // A
        {{0b110, 0b101, 0b110, 0b101, 0b110}}, // B
        {{0b011, 0b100, 0b100, 0b100, 0b011}}, // C
        {{0b110, 0b101, 0b101, 0b101, 0b110}}, // D
        {{0b111, 0b100, 0b110, 0b100, 0b111}}, // E
        {{0b111, 0b100, 0b110, 0b100, 0b100}}, // F
        {{0b011, 0b100, 0b101, 0b101, 0b011}}, // G
        {{0b101, 0b101, 0b111, 0b101, 0b101}}, // H
        {{0b111, 0b010, 0b010, 0b010, 0b111}}, // I
        {{0b001, 0b001, 0b001, 0b101, 0b010}}, // J
        {{0b101, 0b101, 0b110, 0b101, 0b101}}, // K
        {{0b100, 0b100, 0b100, 0b100, 0b111}}, // L
        {{0b101, 0b111, 0b111, 0b101, 0b101}}, // M
        {{0b101, 0b111, 0b111, 0b111, 0b101}}, // N
        {{0b111, 0b101, 0b101, 0b101, 0b111}}, // O
        {{0b111, 0b101, 0b111, 0b100, 0b100}}, // P
        {{0b111, 0b101, 0b101, 0b111, 0b011}}, // Q
        {{0b111, 0b101, 0b111, 0b110, 0b101}}, // R
        {{0b011, 0b100, 0b111, 0b001, 0b110}}, // S
        {{0b111, 0b010, 0b010, 0b010, 0b010}}, // T
        {{0b101, 0b101, 0b101, 0b101, 0b111}}, // U
        {{0b101, 0b101, 0b101, 0b101, 0b010}}, // V
        {{0b101, 0b101, 0b111, 0b111, 0b101}}, // W
        {{0b101, 0b101, 0b010, 0b101, 0b101}}, // X
        {{0b101, 0b101, 0b010, 0b010, 0b010}}, // Y
        {{0b111, 0b001, 0b010, 0b100, 0b111}}, // Z
    }};

    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return &kLetters[static_cast<std::size_t>(c - 'A')];
    }
    if (c >= '0' && c <= '9') {
        return &kDigits[static_cast<std::size_t>(c - '0')];
    }
    switch (c) {
        case ' ': return &kSpace;
        case '.': return &kDot;
        case ':': return &kColon;
        case '-': return &kDash;
        case '_': return &kUnderscore;
        case '/': return &kSlash;
        case '[': return &kLBracket;
        case ']': return &kRBracket;
        case '(': return &kLParen;
        case ')': return &kRParen;
        case '<': return &kLess;
        case '>': return &kGreater;
        case '|': return &kPipe;
        case '=': return &kEq;
        case '+': return &kPlus;
        default:
            return &kUnknown;
    }
}

class TinyFontPainter final {
public:
    static void drawText(Canvas& canvas,
                         int x,
                         int y,
                         std::string_view text,
                         int scale,
                         Rgba color,
                         float alpha01) {
        const int s = std::max(1, scale);
        int cursorX = x;
        for (char c : text) {
            if (c == '\n') {
                cursorX = x;
                y += lineHeight(s);
                continue;
            }
            drawGlyph(canvas, cursorX, y, c, s, color, alpha01);
            cursorX += advance(s);
        }
    }

    static int textWidth(std::string_view text, int scale) noexcept {
        const int s = std::max(1, scale);
        int maxLine = 0;
        int line = 0;
        for (char c : text) {
            if (c == '\n') {
                maxLine = std::max(maxLine, line);
                line = 0;
                continue;
            }
            (void)c;
            line += advance(s);
        }
        maxLine = std::max(maxLine, line);
        return maxLine;
    }

    static int lineHeight(int scale) noexcept {
        return 5 * std::max(1, scale) + std::max(1, scale);
    }

private:
    static constexpr int glyphWidth() noexcept { return 3; }
    static constexpr int glyphHeight() noexcept { return 5; }

    static int advance(int scale) noexcept {
        return glyphWidth() * std::max(1, scale) + std::max(1, scale);
    }

    static void drawGlyph(Canvas& canvas,
                          int x,
                          int y,
                          char c,
                          int scale,
                          Rgba color,
                          float alpha01) {
        const Glyph3x5* glyph = glyphFor(c);
        if (!glyph) {
            return;
        }
        for (int row = 0; row < glyphHeight(); ++row) {
            const uint8_t bits = glyph->rows[static_cast<std::size_t>(row)];
            for (int col = 0; col < glyphWidth(); ++col) {
                const uint8_t mask = static_cast<uint8_t>(1U << (glyphWidth() - 1 - col));
                if ((bits & mask) == 0U) {
                    continue;
                }
                canvas.fillRect(
                    RectI{
                        x + col * scale,
                        y + row * scale,
                        scale,
                        scale},
                    color,
                    alpha01);
            }
        }
    }
};

const UiLayoutNode::StateSpec* resolveNodeState(const UiLayoutNode* node,
                                                const IUiComponent* component) {
    if (!node || !component) {
        return nullptr;
    }
    switch (component->visualState()) {
        case IUiComponent::VisualState::Disabled:
            return &node->disabled;
        case IUiComponent::VisualState::Inactive:
            return &node->inactive;
        case IUiComponent::VisualState::Active:
        default:
            return &node->active;
    }
}

Rgba nodeTextColor(const UiLayoutNode* node, const IUiComponent* c, Rgba fallback) {
    if (!node) {
        return fallback;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, c); st && !st->textColor.empty()) {
        return resolveColor(st->textColor, fallback);
    }
    return resolveColor(node->textColor, fallback);
}

Rgba nodeBorderColor(const UiLayoutNode* node, const IUiComponent* c, Rgba fallback) {
    if (!node) {
        return fallback;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, c); st && !st->borderColor.empty()) {
        return resolveColor(st->borderColor, fallback);
    }
    return resolveColor(node->borderColor, fallback);
}

std::optional<Rgba> nodeBackgroundColor(const UiLayoutNode* node, const IUiComponent* c) {
    if (!node) {
        return std::nullopt;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, c); st && !st->backgroundColor.empty()) {
        return resolveColor(st->backgroundColor, kBgPanel);
    }
    if (!node->backgroundColor.empty()) {
        return resolveColor(node->backgroundColor, kBgPanel);
    }
    return std::nullopt;
}

float resolveNodeOpacity(const UiLayoutNode* node, const IUiComponent* component) {
    float out = 1.0f;
    if (node) {
        out *= std::clamp(node->opacity, 0.0f, 1.0f);
        if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, component)) {
            out *= std::clamp(st->opacity, 0.0f, 1.0f);
        }
    }
    if (component) {
        out *= std::clamp(component->opacity(), 0.0f, 1.0f);
    }
    if (!std::isfinite(out)) {
        return 1.0f;
    }
    return std::clamp(out, 0.0f, 1.0f);
}

int fontScaleFromNode(const UiLayoutNode* node) noexcept {
    if (!node || !(node->fontSize > 0.0f)) {
        return 2;
    }
    const int scale = static_cast<int>(std::lround(node->fontSize / 7.5f));
    return std::clamp(scale, 1, 5);
}

std::string composeSwitchText(const UiSwitchComponent& sw) {
    std::string out = sw.label;
    if (!out.empty()) {
        out.push_back(' ');
    }
    out.push_back('[');
    for (std::size_t i = 0; i < sw.options.size(); ++i) {
        if (i > 0) {
            out.push_back('|');
        }
        if (static_cast<uint16_t>(i) == sw.selectedIndex) {
            out.push_back('>');
        }
        out += sw.options[i];
        if (static_cast<uint16_t>(i) == sw.selectedIndex) {
            out.push_back('<');
        }
    }
    out.push_back(']');
    return out;
}

std::string formatAnimFrameLabel(const UiAnimSlotComponent& anim, uint64_t tick) {
    if (anim.frames.empty()) {
        return "anim";
    }
    std::size_t idx = 0;
    if (anim.playbackMode == UiAnimSlotComponent::PlaybackMode::Scrub) {
        const float clamped = std::clamp(anim.intensity01, 0.0f, 1.0f);
        const std::size_t last = anim.frames.size() - 1U;
        idx = std::min<std::size_t>(
            static_cast<std::size_t>(std::floor(clamped * static_cast<float>(last + 1U))),
            last);
    } else {
        idx = static_cast<std::size_t>(tick % static_cast<uint64_t>(anim.frames.size()));
    }
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "frame:%u", static_cast<unsigned>(idx + 1U));
    return std::string(buf);
}

} // namespace

struct RpiUiRenderer::Impl {
#if defined(__linux__)
    Framebuffer framebuffer{};
#endif
    Canvas canvas{};
    bool ready{false};
    bool warnedUnavailable{false};
    std::string cwd{};
    uint64_t frameTick{0U};
    std::chrono::steady_clock::time_point startTs{std::chrono::steady_clock::now()};
};

RpiUiRenderer::RpiUiRenderer(RpiUiWrapperConfig config) noexcept
    : config_(std::move(config)),
      impl_(std::make_unique<Impl>()) {
    try {
        impl_->cwd = std::filesystem::current_path().string();
    } catch (...) {
        impl_->cwd.clear();
    }
}

RpiUiRenderer::~RpiUiRenderer() = default;

void RpiUiRenderer::render(const UiState& /*state*/) {
    if (!warned_) {
        AppDiagnostics::logf(AppLogLevel::Info,
                             "RpiUiRenderer: waiting for prepared-layout frames");
        warned_ = true;
    }
}

void RpiUiRenderer::renderPreparedLayout(const UiPreparedLayout& prepared) noexcept {
    if (!impl_) {
        return;
    }
#if defined(__linux__)
    if (!impl_->ready && !config_.headless) {
        impl_->ready = impl_->framebuffer.init(config_);
        if (!impl_->ready && !impl_->warnedUnavailable) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiUiRenderer: framebuffer unavailable; running without visible output");
            impl_->warnedUnavailable = true;
        }
        if (impl_->ready) {
            impl_->canvas.resize(impl_->framebuffer.width(), impl_->framebuffer.height());
        }
    }
    if (!impl_->ready) {
        return;
    }
#else
    if (!impl_->warnedUnavailable) {
        AppDiagnostics::logf(AppLogLevel::Warn,
                             "RpiUiRenderer: framebuffer output is supported only on Linux");
        impl_->warnedUnavailable = true;
    }
    (void)prepared;
    return;
#endif

    impl_->canvas.clear(kBgMain);
    ++impl_->frameTick;

    const UiLayoutTemplate* tmpl = prepared.layoutTemplate;
    if (!tmpl) {
#if defined(__linux__)
        impl_->framebuffer.present(impl_->canvas);
#endif
        return;
    }

    const render::UiFrameMetrics metrics = render::computeFrameMetrics(
        prepared,
        static_cast<float>(impl_->canvas.width()),
        static_cast<float>(impl_->canvas.height()),
        10.0f,
        16.0f,
        10.0f,
        80.0f,
        4.0f,
        8.0f);

    const UiLayoutNode* rootNode = &tmpl->root;
    const Rgba defaultText = resolveColor(rootNode ? rootNode->defaultTextColor : "", kText);
    const Rgba frameBorder = nodeBorderColor(rootNode, nullptr, kBorder);
    const Rgba frameBg = nodeBackgroundColor(rootNode, nullptr).value_or(kBgPanel);

    const RectI frameRect =
        toRectI(render::charsToPixelsTopDown(metrics, 0, 0, metrics.frameWidthChars, metrics.frameHeightChars));
    impl_->canvas.fillRect(frameRect, frameBg, 1.0f);
    impl_->canvas.strokeRect(frameRect, frameBorder, 2, 1.0f);

    const uint16_t innerW = static_cast<uint16_t>(std::max<int>(1, static_cast<int>(metrics.frameWidthChars) - 2));
    const UiLayoutEngine::Result layout = UiLayoutEngine::arrange(tmpl->root, innerW, metrics.innerHeightChars);
    const render::UiComponentIndex byId = render::buildComponentIndex(prepared);

    auto drawTextInRect = [&](const RectI& rect,
                              std::string_view text,
                              int scale,
                              UiLayoutAlign align,
                              Rgba color,
                              float opacity01,
                              bool wrap) {
        if (rect.w <= 0 || rect.h <= 0 || text.empty()) {
            return;
        }
        const int lineH = TinyFontPainter::lineHeight(scale);
        const int y = rect.y + std::max(0, (rect.h - lineH) / 2);
        if (!wrap) {
            const int textW = TinyFontPainter::textWidth(text, scale);
            int x = rect.x;
            switch (align) {
                case UiLayoutAlign::Center:
                    x = rect.x + std::max(0, (rect.w - textW) / 2);
                    break;
                case UiLayoutAlign::End:
                    x = rect.x + std::max(0, rect.w - textW - 1);
                    break;
                case UiLayoutAlign::Start:
                default:
                    break;
            }
            TinyFontPainter::drawText(impl_->canvas, x, y, text, scale, color, opacity01);
            return;
        }

        std::size_t start = 0U;
        int lineY = y;
        while (start < text.size() && lineY + lineH <= rect.y + rect.h) {
            std::size_t end = start;
            std::size_t fitEnd = start;
            int fitWidth = 0;
            while (end < text.size()) {
                std::size_t next = text.find(' ', end);
                if (next == std::string_view::npos) {
                    next = text.size();
                }
                const std::string_view chunk = text.substr(start, next - start);
                const int chunkW = TinyFontPainter::textWidth(chunk, scale);
                if (chunkW > rect.w && fitEnd > start) {
                    break;
                }
                if (chunkW > rect.w && fitEnd == start) {
                    fitEnd = next;
                    fitWidth = chunkW;
                    break;
                }
                fitEnd = next;
                fitWidth = chunkW;
                if (next >= text.size()) {
                    break;
                }
                end = next + 1U;
                if (fitWidth >= rect.w) {
                    break;
                }
            }
            if (fitEnd <= start) {
                break;
            }
            const std::string_view line = text.substr(start, fitEnd - start);
            int x = rect.x;
            switch (align) {
                case UiLayoutAlign::Center:
                    x = rect.x + std::max(0, (rect.w - fitWidth) / 2);
                    break;
                case UiLayoutAlign::End:
                    x = rect.x + std::max(0, rect.w - fitWidth - 1);
                    break;
                case UiLayoutAlign::Start:
                default:
                    break;
            }
            TinyFontPainter::drawText(impl_->canvas, x, lineY, line, scale, color, opacity01);
            start = fitEnd;
            while (start < text.size() && text[start] == ' ') {
                ++start;
            }
            lineY += lineH;
        }
    };

    for (const UiLayoutBox& box : layout.boxes) {
        if (!box.node) {
            continue;
        }
        const UiLayoutNode* node = box.node;
        const IUiComponent* component = nullptr;
        if (!node->id.empty()) {
            if (const auto it = byId.find(node->id); it != byId.end()) {
                component = it->second;
            }
        }
        if (component && !component->isVisible()) {
            continue;
        }

        const float nodeOpacity = resolveNodeOpacity(node, component);
        if (nodeOpacity <= 0.001f) {
            continue;
        }

        const RectI rect = toRectI(render::charsToPixelsTopDown(
            metrics,
            static_cast<int16_t>(box.rect.x + 1),
            static_cast<int16_t>(box.rect.y + 1),
            box.rect.width,
            box.rect.height));
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        if (const auto bg = nodeBackgroundColor(node, component); bg.has_value()) {
            impl_->canvas.fillRect(rect, *bg, nodeOpacity);
        }

        const Rgba textColor = nodeTextColor(node, component, defaultText);
        const Rgba borderColor = nodeBorderColor(node, component, frameBorder);
        const int textScale = fontScaleFromNode(node);

        switch (node->type) {
            case UiLayoutNodeType::StatusBar:
            case UiLayoutNodeType::Text: {
                std::string text{};
                UiLayoutAlign align = node->align;
                bool wrap = node->textWrap;
                if (const auto* t = dynamic_cast<const UiTextComponent*>(component)) {
                    text = t->text;
                    if (t->align == UiTextComponent::Align::Center) {
                        align = UiLayoutAlign::Center;
                    } else if (t->align == UiTextComponent::Align::Right) {
                        align = UiLayoutAlign::End;
                    } else {
                        align = UiLayoutAlign::Start;
                    }
                } else if (const auto* s = dynamic_cast<const UiStatusBarComponent*>(component)) {
                    text = s->text;
                } else {
                    text = node->text;
                }
                drawTextInRect(RectI{rect.x + 2, rect.y, std::max(0, rect.w - 4), rect.h},
                               text,
                               textScale,
                               align,
                               textColor,
                               nodeOpacity,
                               wrap);
            } break;

            case UiLayoutNodeType::Separator: {
                const int y = rect.y + rect.h / 2;
                impl_->canvas.line(rect.x, y, rect.x + rect.w - 1, y, borderColor, nodeOpacity);
            } break;

            case UiLayoutNodeType::List: {
                const auto* list = dynamic_cast<const UiListComponent*>(component);
                if (!list) {
                    break;
                }
                const int lineH = TinyFontPainter::lineHeight(std::max(1, textScale - 1));
                int y = rect.y + 1;
                for (std::size_t i = 0; i < list->rows.size(); ++i) {
                    if (y + lineH > rect.y + rect.h) {
                        break;
                    }
                    const bool selected = (list->selectedRow >= 0) &&
                                          static_cast<std::size_t>(list->selectedRow) == i;
                    std::string row = render::markerPrefix(list->marker, selected) + list->rows[i];
                    const Rgba rowColor = selected ? kAccent : textColor;
                    drawTextInRect(RectI{rect.x + 2, y, std::max(0, rect.w - 4), lineH},
                                   row,
                                   std::max(1, textScale - 1),
                                   UiLayoutAlign::Start,
                                   rowColor,
                                   nodeOpacity,
                                   false);
                    y += lineH;
                }
            } break;

            case UiLayoutNodeType::Knob: {
                const auto* knob = dynamic_cast<const UiKnobComponent*>(component);
                if (!knob) {
                    break;
                }
                const int labelH = TinyFontPainter::lineHeight(std::max(1, textScale - 1));
                if (!knob->label.empty()) {
                    drawTextInRect(RectI{rect.x, rect.y, rect.w, labelH},
                                   knob->label,
                                   std::max(1, textScale - 1),
                                   node->align,
                                   textColor,
                                   nodeOpacity,
                                   false);
                }
                const int cy = rect.y + labelH + std::max(8, (rect.h - labelH) / 2);
                const int cx = rect.x + rect.w / 2;
                const int radius = std::max(6, std::min(rect.w, rect.h - labelH) / 3);
                impl_->canvas.circle(cx, cy, radius, borderColor, nodeOpacity);

                const float v = std::clamp(knob->value01, 0.0f, 1.0f);
                const float a = (225.0f - v * 270.0f) * static_cast<float>(M_PI / 180.0);
                const int nx = static_cast<int>(std::lround(static_cast<float>(cx) + std::cos(a) * radius * 0.8f));
                const int ny = static_cast<int>(std::lround(static_cast<float>(cy) + std::sin(a) * radius * 0.8f));
                impl_->canvas.line(cx, cy, nx, ny, borderColor, nodeOpacity);
                if (knob->selected) {
                    impl_->canvas.strokeRect(RectI{rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, kAccent, 1, nodeOpacity);
                }
            } break;

            case UiLayoutNodeType::Switch: {
                const auto* sw = dynamic_cast<const UiSwitchComponent*>(component);
                if (!sw) {
                    break;
                }
                const std::string text = composeSwitchText(*sw);
                drawTextInRect(RectI{rect.x + 1, rect.y + 1, std::max(0, rect.w - 2), std::max(0, rect.h - 2)},
                               text,
                               std::max(1, textScale - 1),
                               UiLayoutAlign::Start,
                               textColor,
                               nodeOpacity,
                               true);
                impl_->canvas.strokeRect(rect, borderColor, 1, nodeOpacity);
            } break;

            case UiLayoutNodeType::Icon: {
                const auto* icon = dynamic_cast<const UiIconComponent*>(component);
                const RectI iconRect{rect.x + 2, rect.y + 2, std::max(4, rect.w - 4), std::max(4, rect.h - 4)};
                impl_->canvas.strokeRect(iconRect, borderColor, 1, nodeOpacity);
                impl_->canvas.line(iconRect.x, iconRect.y, iconRect.x + iconRect.w - 1, iconRect.y + iconRect.h - 1, borderColor, nodeOpacity);
                impl_->canvas.line(iconRect.x + iconRect.w - 1, iconRect.y, iconRect.x, iconRect.y + iconRect.h - 1, borderColor, nodeOpacity);
                if (icon && !icon->path.empty()) {
                    drawTextInRect(RectI{iconRect.x, iconRect.y + iconRect.h + 1, iconRect.w, TinyFontPainter::lineHeight(1)},
                                   "ICON",
                                   1,
                                   UiLayoutAlign::Center,
                                   textColor,
                                   nodeOpacity,
                                   false);
                }
            } break;

            case UiLayoutNodeType::AnimSlot: {
                const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component);
                if (!anim) {
                    break;
                }
                const RectI slot{
                    rect.x + 2,
                    rect.y + 2,
                    std::max(10, std::min(128, rect.w - 4)),
                    std::max(10, std::min(128, rect.h - 4))};
                impl_->canvas.strokeRect(slot, borderColor, 1, nodeOpacity);
                const uint64_t ms = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - impl_->startTs)
                        .count());
                const float fps = std::max(0.1f, anim->fps);
                const uint64_t tick = static_cast<uint64_t>(std::floor(static_cast<double>(ms) * static_cast<double>(fps) / 1000.0));
                const std::string animLabel = formatAnimFrameLabel(*anim, tick);
                drawTextInRect(RectI{slot.x, slot.y + slot.h / 2 - TinyFontPainter::lineHeight(1) / 2, slot.w, TinyFontPainter::lineHeight(1)},
                               animLabel,
                               1,
                               UiLayoutAlign::Center,
                               textColor,
                               nodeOpacity,
                               false);
            } break;

            case UiLayoutNodeType::Waveform: {
                const auto* wave = dynamic_cast<const UiWaveformComponent*>(component);
                if (!wave) {
                    break;
                }
                const RectI area{rect.x + 2, rect.y + 2, std::max(8, rect.w - 4), std::max(6, rect.h - 4)};
                impl_->canvas.strokeRect(area, borderColor, 1, nodeOpacity);
                if (!wave->peaks01.empty()) {
                    if (wave->curveMode) {
                        int prevX = area.x;
                        int prevY = area.y + area.h / 2;
                        for (std::size_t i = 0; i < wave->peaks01.size(); ++i) {
                            const float x01 = (wave->peaks01.size() > 1U)
                                                  ? static_cast<float>(i) / static_cast<float>(wave->peaks01.size() - 1U)
                                                  : 0.0f;
                            const float y01 = std::clamp(wave->peaks01[i], 0.0f, 1.0f);
                            const int x = area.x + static_cast<int>(std::lround(x01 * static_cast<float>(std::max(1, area.w - 1))));
                            const int y = area.y + static_cast<int>(std::lround((1.0f - y01) * static_cast<float>(std::max(1, area.h - 1))));
                            if (i > 0U) {
                                impl_->canvas.line(prevX, prevY, x, y, textColor, nodeOpacity);
                            }
                            prevX = x;
                            prevY = y;
                        }
                    } else {
                        const std::size_t cols = std::min<std::size_t>(wave->peaks01.size(), static_cast<std::size_t>(area.w));
                        for (std::size_t i = 0; i < cols; ++i) {
                            const float v = std::clamp(wave->peaks01[i], 0.0f, 1.0f);
                            const int x = area.x + static_cast<int>(i);
                            const int h = std::max(1, static_cast<int>(std::lround(v * static_cast<float>(area.h))));
                            const int y0 = area.y + (area.h - h) / 2;
                            impl_->canvas.line(x, y0, x, y0 + h - 1, textColor, nodeOpacity);
                        }
                    }
                }
                const int px = area.x + static_cast<int>(std::lround(
                                            std::clamp(wave->playhead01, 0.0f, 1.0f) *
                                            static_cast<float>(std::max(1, area.w - 1))));
                impl_->canvas.line(px, area.y, px, area.y + area.h - 1, kAccent, nodeOpacity);
            } break;

            case UiLayoutNodeType::TrackView:
            case UiLayoutNodeType::ManagerView:
            case UiLayoutNodeType::FxListView:
            case UiLayoutNodeType::FxEditorView:
            case UiLayoutNodeType::Column:
            case UiLayoutNodeType::Row:
            case UiLayoutNodeType::Spacer:
            case UiLayoutNodeType::Unknown:
            default:
                break;
        }
    }

    if (prepared.hud.visible && !prepared.hud.text.empty()) {
        const UiHudOverlayView& hud = prepared.hud;
        const uint16_t maxW = (metrics.frameWidthChars > 6U) ? static_cast<uint16_t>(metrics.frameWidthChars - 4U) : metrics.frameWidthChars;
        const uint16_t maxH = (metrics.frameHeightChars > 6U) ? static_cast<uint16_t>(metrics.frameHeightChars - 4U) : metrics.frameHeightChars;
        const uint16_t hudW = static_cast<uint16_t>(std::clamp<uint16_t>(hud.width, 8U, std::max<uint16_t>(8U, maxW)));
        const uint16_t hudH = static_cast<uint16_t>(std::clamp<uint16_t>(hud.height, 2U, std::max<uint16_t>(2U, maxH)));

        int16_t hudX = static_cast<int16_t>(std::max<int>(1, (static_cast<int>(metrics.frameWidthChars) - static_cast<int>(hudW)) / 2));
        int16_t hudY = 1;
        if (hud.position == UiHudPosition::Center) {
            hudY = static_cast<int16_t>(std::max<int>(1, (static_cast<int>(metrics.frameHeightChars) - static_cast<int>(hudH)) / 2));
        }
        hudX = static_cast<int16_t>(std::clamp<int>(hudX, 1, std::max<int>(1, static_cast<int>(metrics.frameWidthChars - hudW - 1))));
        hudY = static_cast<int16_t>(std::clamp<int>(hudY, 1, std::max<int>(1, static_cast<int>(metrics.frameHeightChars - hudH - 1))));

        RectI hudRect = toRectI(render::charsToPixelsTopDown(metrics, hudX, hudY, hudW, hudH));
        const float scale = std::clamp(hud.scale, 0.80f, 1.20f);
        if (std::fabs(scale - 1.0f) > 0.0001f) {
            const int cx = hudRect.x + hudRect.w / 2;
            const int cy = hudRect.y + hudRect.h / 2;
            hudRect.w = static_cast<int>(std::lround(static_cast<float>(hudRect.w) * scale));
            hudRect.h = static_cast<int>(std::lround(static_cast<float>(hudRect.h) * scale));
            hudRect.x = cx - hudRect.w / 2;
            hudRect.y = cy - hudRect.h / 2;
        }

        const Rgba hudText = resolveColor(hud.textColor, defaultText);
        const Rgba hudBorder = resolveColor(hud.borderColor, frameBorder);
        const Rgba hudBg = resolveColor(hud.backgroundColor, kBgPanel);
        const float baseOpacity = std::clamp(hud.opacity, 0.0f, 1.0f);

        impl_->canvas.fillRect(hudRect, hudBg, baseOpacity);
        impl_->canvas.strokeRect(hudRect, hudBorder, 1, baseOpacity);

        const int hudScale = std::clamp(static_cast<int>(std::lround((hud.fontSize > 0.0f ? hud.fontSize : 14.0f) / 7.0f)), 1, 5);
        drawTextInRect(RectI{hudRect.x + static_cast<int>(hud.padding),
                             hudRect.y + static_cast<int>(hud.padding),
                             std::max(1, hudRect.w - static_cast<int>(hud.padding) * 2),
                             std::max(1, hudRect.h - static_cast<int>(hud.padding) * 2)},
                       hud.text,
                       hudScale,
                       hud.align,
                       hudText,
                       baseOpacity,
                       hud.textWrap);
    }

#if defined(__linux__)
    impl_->framebuffer.present(impl_->canvas);
#endif
}

RpiPrimitiveInput::~RpiPrimitiveInput() {
    shutdown();
}

bool RpiPrimitiveInput::init(const RpiUiWrapperConfig& config, std::string& errorOut) {
    errorOut.clear();
#if defined(__linux__)
    shutdown();
    shiftHeld_ = false;

    if (config.inputDevice.empty()) {
        AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveInput: input disabled (empty device path)");
        return true;
    }

    const int fd = ::open(config.inputDevice.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        AppDiagnostics::logf(AppLogLevel::Warn,
                             "RpiPrimitiveInput: cannot open input device '%s': %s",
                             config.inputDevice.c_str(),
                             std::strerror(errno));
        // Не валим init приложения: UI может работать без физического input.
        return true;
    }
    inputFd_ = fd;
    AppDiagnostics::logf(AppLogLevel::Info,
                         "RpiPrimitiveInput: attached to '%s'",
                         config.inputDevice.c_str());
    return true;
#else
    (void)config;
    AppDiagnostics::logf(AppLogLevel::Info, "RpiPrimitiveInput: non-linux platform, stub input backend");
    return true;
#endif
}

void RpiPrimitiveInput::shutdown() noexcept {
#if defined(__linux__)
    if (inputFd_ >= 0) {
        ::close(inputFd_);
        inputFd_ = -1;
    }
    shiftHeld_ = false;
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

bool RpiPrimitiveInput::pollPlatformEvents() noexcept {
#if defined(__linux__)
    if (inputFd_ < 0) {
        return false;
    }
    bool anyEvents = false;
    for (;;) {
        input_event ev{};
        const ssize_t n = ::read(inputFd_, &ev, sizeof(ev));
        if (n == static_cast<ssize_t>(sizeof(ev))) {
            if (ev.type != EV_KEY) {
                continue;
            }
            const uint64_t tsMs = static_cast<uint64_t>(ev.time.tv_sec) * 1000ULL +
                                  static_cast<uint64_t>(ev.time.tv_usec / 1000ULL);
            anyEvents = handleLinuxKeyEvent_(ev.code, ev.value, tsMs) || anyEvents;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RpiPrimitiveInput: read error: %s",
                                 std::strerror(errno));
        }
        break;
    }
    return anyEvents;
#else
    return false;
#endif
}

bool RpiPrimitiveInput::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out = PrimitiveInputEvent{};
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void RpiPrimitiveInput::pushSynthetic(const PrimitiveInputEvent& ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
}

#if defined(__linux__)
bool RpiPrimitiveInput::handleLinuxKeyEvent_(uint16_t code, int32_t value, uint64_t timestampMs) noexcept {
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        shiftHeld_ = (value != 0);
        return false;
    }

    PrimitivePhase phase = PrimitivePhase::Down;
    switch (value) {
        case 0: phase = PrimitivePhase::Up; break;
        case 1: phase = PrimitivePhase::Down; break;
        case 2: phase = PrimitivePhase::Repeat; break;
        default: return false;
    }

    PrimitiveControl control = PrimitiveControl::None;
    switch (code) {
        case KEY_1: control = shiftHeld_ ? PrimitiveControl::SelectPattern1 : PrimitiveControl::SelectTrack1; break;
        case KEY_2: control = shiftHeld_ ? PrimitiveControl::SelectPattern2 : PrimitiveControl::SelectTrack2; break;
        case KEY_3: control = shiftHeld_ ? PrimitiveControl::SelectPattern3 : PrimitiveControl::SelectTrack3; break;
        case KEY_4: control = shiftHeld_ ? PrimitiveControl::SelectPattern4 : PrimitiveControl::SelectTrack4; break;
        case KEY_SLASH:
            control = shiftHeld_ ? PrimitiveControl::ActionAdjustNext : PrimitiveControl::ActionAdjustPrev;
            break;
        case KEY_N:
            control = shiftHeld_ ? PrimitiveControl::OpenPatternEdit : PrimitiveControl::OpenSequencer;
            break;
        default:
            control = mapLinuxKeyCode_(code);
            break;
    }
    if (control == PrimitiveControl::None) {
        return false;
    }

    PrimitiveInputEvent ev{};
    ev.control = control;
    ev.phase = phase;
    ev.timestampMs = timestampMs;

    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
    return true;
}

PrimitiveControl RpiPrimitiveInput::mapLinuxKeyCode_(uint16_t code) const noexcept {
    switch (code) {
        case KEY_ESC: return PrimitiveControl::BackScene;
        case KEY_Q: return PrimitiveControl::Quit;
        case KEY_COMMA: return PrimitiveControl::TrackPagePrev;
        case KEY_DOT: return PrimitiveControl::TrackPageNext;
        case KEY_M: return PrimitiveControl::ToggleMetronome;
        case KEY_V: return PrimitiveControl::Record;
        case KEY_J: return PrimitiveControl::ListDown;
        case KEY_K: return PrimitiveControl::ListUp;
        case KEY_ENTER: return PrimitiveControl::ListEnter;
        case KEY_H: return PrimitiveControl::ListParent;
        case KEY_BACKSPACE: return PrimitiveControl::DeleteObject;
        case KEY_SPACE: return PrimitiveControl::PreviewPlay;
        case KEY_A: return PrimitiveControl::PreviewAutoToggle;
        case KEY_P: return PrimitiveControl::PlayActiveTrack;
        case KEY_S: return PrimitiveControl::StopActiveTrack;
        case KEY_D: return PrimitiveControl::DeleteObject;
        case KEY_U: return PrimitiveControl::UnmuteActiveTrack;
        case KEY_I: return PrimitiveControl::MuteActiveTrack;
        case KEY_E: return PrimitiveControl::Snapshot1;
        case KEY_R: return PrimitiveControl::Snapshot2;
        case KEY_T: return PrimitiveControl::Snapshot3;
        case KEY_Y: return PrimitiveControl::Snapshot4;
        case KEY_SEMICOLON: return PrimitiveControl::ActionFocusPrev;
        case KEY_APOSTROPHE: return PrimitiveControl::ActionFocusNext;
        case KEY_O: return PrimitiveControl::ActionApply;
        case KEY_EQUAL: return PrimitiveControl::TrackSpeedUp;
        case KEY_MINUS: return PrimitiveControl::TrackSpeedDown;
        case KEY_Z: return PrimitiveControl::QuantNone;
        case KEY_X: return PrimitiveControl::QuantBeat;
        case KEY_C: return PrimitiveControl::QuantBar;
        case KEY_RIGHTBRACE: return PrimitiveControl::BpmUp;
        case KEY_LEFTBRACE: return PrimitiveControl::BpmDown;
        case KEY_F1: return PrimitiveControl::F1;
        case KEY_F2: return PrimitiveControl::F2;
        case KEY_F3: return PrimitiveControl::F3;
        case KEY_F4: return PrimitiveControl::F4;
        case KEY_F5: return PrimitiveControl::F5;
        case KEY_F6: return PrimitiveControl::F6;
        case KEY_F7: return PrimitiveControl::F7;
        case KEY_F8: return PrimitiveControl::F8;
        case KEY_F9: return PrimitiveControl::F9;
        case KEY_F10: return PrimitiveControl::F10;
        case KEY_F11: return PrimitiveControl::F11;
        case KEY_F12: return PrimitiveControl::F12;
        default:
            return PrimitiveControl::None;
    }
}
#endif

bool RpiUiWrapper::init(const RpiUiWrapperConfig& config, std::string& errorOut) {
    config_ = config;
    renderer_ = RpiUiRenderer{config_};
    return input_.init(config_, errorOut);
}

bool RpiUiWrapper::pollEvents() noexcept {
    return input_.pollPlatformEvents();
}

bool RpiUiWrapper::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    return input_.readNextInputEvent(out);
}

void RpiUiWrapper::render(const UiState& state, const UiPreparedLayout* preparedLayout) {
    if (preparedLayout) {
        renderer_.renderPreparedLayout(*preparedLayout);
        return;
    }
    renderer_.render(state);
}

} // namespace avantgarde::raspi
