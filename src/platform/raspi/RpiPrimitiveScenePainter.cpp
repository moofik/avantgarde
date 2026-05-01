#include "platform/raspi/RpiPrimitiveScenePainter.h"

#include "platform/render/PreparedLayoutUtils.h"
#include "platform/render/RenderGeometry.h"
#include "service/ui/layout/UiLayoutEngine.h"
#include "app/AppDiagnostics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(AVANTGARDE_HAS_LIBPNG)
#include <png.h>
#endif

#if defined(AVANTGARDE_HAS_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace avantgarde::raspi {
namespace {

constexpr RpiRgba kBgMain{11, 10, 15, 255};
constexpr RpiRgba kBgPanel{19, 13, 22, 255};
constexpr RpiRgba kBorder{143, 110, 149, 255};
constexpr RpiRgba kText{214, 209, 230, 255};
constexpr RpiRgba kAccent{200, 28, 99, 255};

RpiRectI toRectI(const render::UiRectPx& r) noexcept {
    return RpiRectI{
        static_cast<int>(std::lround(r.x)),
        static_cast<int>(std::lround(r.y)),
        std::max(0, static_cast<int>(std::lround(r.w))),
        std::max(0, static_cast<int>(std::lround(r.h)))};
}

bool parseHexColor(std::string_view raw, RpiRgba& out) {
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

    out = RpiRgba{
        static_cast<uint8_t>(rr),
        static_cast<uint8_t>(gg),
        static_cast<uint8_t>(bb),
        static_cast<uint8_t>(aa)};
    return true;
}

RpiRgba resolveColor(std::string_view spec, RpiRgba fallback) {
    RpiRgba out{};
    if (parseHexColor(spec, out)) {
        return out;
    }
    return fallback;
}

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

RpiRgba nodeTextColor(const UiLayoutNode* node, const IUiComponent* c, RpiRgba fallback) {
    if (!node) {
        return fallback;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, c); st && !st->textColor.empty()) {
        return resolveColor(st->textColor, fallback);
    }
    return resolveColor(node->textColor, fallback);
}

RpiRgba nodeBorderColor(const UiLayoutNode* node, const IUiComponent* c, RpiRgba fallback) {
    if (!node) {
        return fallback;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, c); st && !st->borderColor.empty()) {
        return resolveColor(st->borderColor, fallback);
    }
    return resolveColor(node->borderColor, fallback);
}

std::optional<RpiRgba> nodeBackgroundColor(const UiLayoutNode* node, const IUiComponent* c) {
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

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

[[maybe_unused]] bool hasImageExt(std::string_view path) {
    namespace fs = std::filesystem;
    std::string ext = toLowerAscii(fs::path(path).extension().string());
    return ext == ".png";
}

[[maybe_unused]] bool hasFontExt(std::string_view path) {
    namespace fs = std::filesystem;
    std::string ext = toLowerAscii(fs::path(path).extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
}

[[maybe_unused]] std::vector<std::string> buildAssetPathCandidates(std::string_view spec, std::string_view cwd, bool imageMode) {
    namespace fs = std::filesystem;
    std::vector<std::string> out{};
    if (spec.empty()) {
        return out;
    }
    auto pushUnique = [&out](const fs::path& p) {
        const std::string s = p.lexically_normal().string();
        if (s.empty()) {
            return;
        }
        if (std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    };

    const fs::path specPath(spec);
    const fs::path cwdPath(cwd);

    if (specPath.is_absolute()) {
        pushUnique(specPath);
    } else {
        pushUnique(cwdPath / specPath);
        pushUnique(cwdPath / ".." / specPath);
        pushUnique(cwdPath / ".." / ".." / specPath);
    }

    if (specPath.parent_path().empty()) {
        pushUnique(cwdPath / "assets" / (imageMode ? "images" : "fonts") / specPath);
        if (imageMode) {
            pushUnique(cwdPath / "assets" / "sprites" / specPath);
        }
    }

    if (imageMode && std::string(spec).rfind("images/", 0) == 0U) {
        pushUnique(cwdPath / "assets" / specPath);
    }
    if (imageMode && std::string(spec).rfind("sprites/", 0) == 0U) {
        pushUnique(cwdPath / "assets" / specPath);
    }
    if (!imageMode && std::string(spec).rfind("fonts/", 0) == 0U) {
        pushUnique(cwdPath / "assets" / specPath);
    }
    return out;
}

std::string resolveFontSpec(std::string_view raw) {
    const std::string low = toLowerAscii(std::string(raw));
    if (low == "gothic") {
        return "assets/fonts/gothic-pixel-font.ttf";
    }
    if (low == "medieval" || low == "medieval_ui") {
        return "assets/fonts/MedievalTimes-AL7l6.ttf";
    }
    if (low == "default" || low == "body" || low == "mono") {
        return "assets/fonts/MedievalTimes-AL7l6.ttf";
    }
    return std::string(raw);
}

std::string nodeFontSpec(const UiLayoutNode* node, const IUiComponent* component) {
    if (!node) {
        return {};
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, component); st && !st->font.empty()) {
        return st->font;
    }
    return node->font;
}

float nodeFontSize(const UiLayoutNode* node, const IUiComponent* component, float fallbackPx) {
    if (!node) {
        return fallbackPx;
    }
    if (const UiLayoutNode::StateSpec* st = resolveNodeState(node, component); st && st->fontSize > 0.0f) {
        return st->fontSize;
    }
    if (node->fontSize > 0.0f) {
        return node->fontSize;
    }
    return fallbackPx;
}

struct RpiImage {
    int w{0};
    int h{0};
    std::vector<RpiRgba> pixels{};
};

#if defined(AVANTGARDE_HAS_LIBPNG)
bool loadPngRgba(const std::string& path, RpiImage& out) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bitDepth = 0;
    int colorType = 0;
    png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB ||
        colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);
    const png_size_t rowBytes = png_get_rowbytes(png, info);
    std::vector<uint8_t> raw(rowBytes * static_cast<std::size_t>(height));
    std::vector<png_bytep> rows(static_cast<std::size_t>(height));
    for (std::size_t y = 0; y < static_cast<std::size_t>(height); ++y) {
        rows[y] = raw.data() + y * rowBytes;
    }
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);

    out.w = static_cast<int>(width);
    out.h = static_cast<int>(height);
    out.pixels.resize(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h));
    for (int y = 0; y < out.h; ++y) {
        const uint8_t* row = raw.data() + static_cast<std::size_t>(y) * rowBytes;
        for (int x = 0; x < out.w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(x) * 4U;
            out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) + static_cast<std::size_t>(x)] =
                RpiRgba{row[idx + 0], row[idx + 1], row[idx + 2], row[idx + 3]};
        }
    }
    return true;
}
#endif

const RpiImage* loadImageCached(std::string_view spec, std::string_view cwd) {
    static std::unordered_map<std::string, RpiImage> cache{};
    static std::unordered_map<std::string, bool> failed{};
    (void)cwd;
    const std::string raw(spec);
    if (raw.empty()) {
        return nullptr;
    }
    if (auto it = cache.find(raw); it != cache.end()) {
        return &it->second;
    }
    if (failed.find(raw) != failed.end()) {
        return nullptr;
    }
#if defined(AVANTGARDE_HAS_LIBPNG)
    const std::vector<std::string> candidates = buildAssetPathCandidates(raw, cwd, true);
    for (const std::string& c : candidates) {
        if (!hasImageExt(c)) {
            continue;
        }
        RpiImage img{};
        if (loadPngRgba(c, img) && img.w > 0 && img.h > 0) {
            cache.emplace(raw, std::move(img));
            return &cache.find(raw)->second;
        }
    }
#endif
    failed.emplace(raw, true);
    return nullptr;
}

void drawImageFit(RpiPixelCanvas& canvas, const RpiRectI& target, const RpiImage& img, float alpha01) {
    if (target.w <= 0 || target.h <= 0 || img.w <= 0 || img.h <= 0) {
        return;
    }
    const float sx = static_cast<float>(target.w) / static_cast<float>(img.w);
    const float sy = static_cast<float>(target.h) / static_cast<float>(img.h);
    const float scale = std::min(sx, sy);
    const int drawW = std::max(1, static_cast<int>(std::floor(static_cast<float>(img.w) * scale)));
    const int drawH = std::max(1, static_cast<int>(std::floor(static_cast<float>(img.h) * scale)));
    const int offX = target.x + (target.w - drawW) / 2;
    const int offY = target.y + (target.h - drawH) / 2;

    for (int y = 0; y < drawH; ++y) {
        const int srcY = std::clamp(static_cast<int>((static_cast<float>(y) / static_cast<float>(drawH)) * img.h), 0, img.h - 1);
        for (int x = 0; x < drawW; ++x) {
            const int srcX = std::clamp(static_cast<int>((static_cast<float>(x) / static_cast<float>(drawW)) * img.w), 0, img.w - 1);
            const RpiRgba px = img.pixels[static_cast<std::size_t>(srcY) * static_cast<std::size_t>(img.w) + static_cast<std::size_t>(srcX)];
            canvas.fillRect(RpiRectI{offX + x, offY + y, 1, 1}, px, alpha01);
        }
    }
}

#if defined(AVANTGARDE_HAS_FREETYPE)
class FreetypeRuntime final {
public:
    FreetypeRuntime() {
        if (FT_Init_FreeType(&lib_) == 0) {
            ok_ = true;
        }
    }

    ~FreetypeRuntime() {
        for (auto& kv : faces_) {
            if (kv.second) {
                FT_Done_Face(kv.second);
            }
        }
        faces_.clear();
        if (ok_) {
            FT_Done_FreeType(lib_);
        }
    }

    bool ok() const noexcept { return ok_; }

    FT_Face face(const std::string& path, int pixelSize) {
        if (!ok_ || path.empty() || pixelSize <= 0) {
            return nullptr;
        }
        const std::string key = path + "#" + std::to_string(pixelSize);
        if (auto it = faces_.find(key); it != faces_.end()) {
            return it->second;
        }
        FT_Face face = nullptr;
        if (FT_New_Face(lib_, path.c_str(), 0, &face) != 0 || !face) {
            return nullptr;
        }
        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0) {
            FT_Done_Face(face);
            return nullptr;
        }
        faces_.emplace(key, face);
        return face;
    }

private:
    FT_Library lib_{};
    bool ok_{false};
    std::unordered_map<std::string, FT_Face> faces_{};
};

FreetypeRuntime& freetypeRuntime() {
    static FreetypeRuntime rt{};
    return rt;
}

bool resolveFontFilePath(std::string_view spec, std::string_view cwd, std::string& outPath) {
    namespace fs = std::filesystem;

    auto pathExists = [](const std::string& p) -> bool {
        if (p.empty()) {
            return false;
        }
        std::error_code ec{};
        return fs::exists(fs::path(p), ec) && !ec;
    };

    auto pushUnique = [](std::vector<std::string>& out, std::string p) {
        if (p.empty()) {
            return;
        }
        if (std::find(out.begin(), out.end(), p) == out.end()) {
            out.push_back(std::move(p));
        }
    };

    outPath.clear();
    const std::string resolved = resolveFontSpec(spec);
    if (resolved.empty()) {
        return false;
    }

    std::vector<std::string> candidates = buildAssetPathCandidates(resolved, cwd, false);
    const std::string resolvedLow = toLowerAscii(resolved);
    if (resolvedLow.find("gothic-pixel-font.ttf") != std::string::npos) {
        const std::vector<std::string> alt = buildAssetPathCandidates("assets/fonts/gothic_pixel.ttf", cwd, false);
        for (const auto& c : alt) {
            pushUnique(candidates, c);
        }
    }
    const std::vector<std::string> fallbackLocal = buildAssetPathCandidates("assets/fonts/MedievalTimes-AL7l6.ttf", cwd, false);
    for (const auto& c : fallbackLocal) {
        pushUnique(candidates, c);
    }

    // Системный fallback на Linux (если локальные fonts недоступны/битые).
    pushUnique(candidates, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    pushUnique(candidates, "/usr/share/fonts/truetype/freefont/FreeSans.ttf");

    for (const std::string& c : candidates) {
        if (hasFontExt(c) && pathExists(c)) {
            outPath = c;
            return true;
        }
    }
    return false;
}

int measureTextFt(FT_Face face, std::string_view text) {
    if (!face) {
        return 0;
    }
    int penX = 0;
    for (char c : text) {
        if (c == '\n') {
            break;
        }
        if (FT_Load_Char(face, static_cast<unsigned char>(c), FT_LOAD_RENDER) != 0) {
            continue;
        }
        penX += static_cast<int>(face->glyph->advance.x >> 6);
    }
    return penX;
}

bool drawTextFt(RpiPixelCanvas& canvas,
                int x,
                int yTop,
                std::string_view text,
                std::string_view fontSpec,
                int pixelSize,
                RpiRgba color,
                float alpha01,
                std::string_view cwd) {
    FreetypeRuntime& rt = freetypeRuntime();
    if (!rt.ok()) {
        static bool warnedNoFt = false;
        if (!warnedNoFt) {
            warnedNoFt = true;
            AppDiagnostics::logf(AppLogLevel::Warn, "RPi/FT: runtime is not initialized");
        }
        return false;
    }
    std::string path{};
    if (!resolveFontFilePath(fontSpec, cwd, path)) {
        static std::unordered_map<std::string, bool> warnedResolve{};
        const std::string key(fontSpec);
        if (!key.empty() && warnedResolve.find(key) == warnedResolve.end()) {
            warnedResolve.emplace(key, true);
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RPi/FT: cannot resolve font spec='%s' cwd='%s'",
                                 key.c_str(),
                                 std::string(cwd).c_str());
        }
        return false;
    }
    FT_Face face = rt.face(path, std::max(6, pixelSize));
    if (!face) {
        static std::unordered_map<std::string, bool> warnedFace{};
        const std::string key = path + "#" + std::to_string(std::max(6, pixelSize));
        if (warnedFace.find(key) == warnedFace.end()) {
            warnedFace.emplace(key, true);
            AppDiagnostics::logf(AppLogLevel::Warn,
                                 "RPi/FT: cannot load face path='%s' size=%d",
                                 path.c_str(),
                                 std::max(6, pixelSize));
        }
        return false;
    }

    const int ascent = static_cast<int>(face->size->metrics.ascender >> 6);
    int penX = x;
    const int baselineY = yTop + ascent;
    for (char c : text) {
        if (c == '\n') {
            break;
        }
        if (FT_Load_Char(face, static_cast<unsigned char>(c), FT_LOAD_RENDER) != 0) {
            continue;
        }
        FT_GlyphSlot g = face->glyph;
        const int gx = penX + g->bitmap_left;
        const int gy = baselineY - g->bitmap_top;
        for (int row = 0; row < static_cast<int>(g->bitmap.rows); ++row) {
            for (int col = 0; col < static_cast<int>(g->bitmap.width); ++col) {
                const uint8_t a = g->bitmap.buffer[row * g->bitmap.pitch + col];
                if (a == 0) {
                    continue;
                }
                const float a01 = (static_cast<float>(a) / 255.0f) * alpha01;
                canvas.fillRect(RpiRectI{gx + col, gy + row, 1, 1}, color, a01);
            }
        }
        penX += static_cast<int>(g->advance.x >> 6);
    }
    return true;
}
#endif

struct Glyph3x5 {
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
        {{0b111, 0b101, 0b101, 0b101, 0b111}},
        {{0b010, 0b110, 0b010, 0b010, 0b111}},
        {{0b111, 0b001, 0b111, 0b100, 0b111}},
        {{0b111, 0b001, 0b111, 0b001, 0b111}},
        {{0b101, 0b101, 0b111, 0b001, 0b001}},
        {{0b111, 0b100, 0b111, 0b001, 0b111}},
        {{0b111, 0b100, 0b111, 0b101, 0b111}},
        {{0b111, 0b001, 0b001, 0b001, 0b001}},
        {{0b111, 0b101, 0b111, 0b101, 0b111}},
        {{0b111, 0b101, 0b111, 0b001, 0b111}},
    }};

    static const std::array<Glyph3x5, 26> kLetters{{
        {{0b010, 0b101, 0b111, 0b101, 0b101}},
        {{0b110, 0b101, 0b110, 0b101, 0b110}},
        {{0b011, 0b100, 0b100, 0b100, 0b011}},
        {{0b110, 0b101, 0b101, 0b101, 0b110}},
        {{0b111, 0b100, 0b110, 0b100, 0b111}},
        {{0b111, 0b100, 0b110, 0b100, 0b100}},
        {{0b011, 0b100, 0b101, 0b101, 0b011}},
        {{0b101, 0b101, 0b111, 0b101, 0b101}},
        {{0b111, 0b010, 0b010, 0b010, 0b111}},
        {{0b001, 0b001, 0b001, 0b101, 0b010}},
        {{0b101, 0b101, 0b110, 0b101, 0b101}},
        {{0b100, 0b100, 0b100, 0b100, 0b111}},
        {{0b101, 0b111, 0b111, 0b101, 0b101}},
        {{0b101, 0b111, 0b111, 0b111, 0b101}},
        {{0b111, 0b101, 0b101, 0b101, 0b111}},
        {{0b111, 0b101, 0b111, 0b100, 0b100}},
        {{0b111, 0b101, 0b101, 0b111, 0b011}},
        {{0b111, 0b101, 0b111, 0b110, 0b101}},
        {{0b011, 0b100, 0b111, 0b001, 0b110}},
        {{0b111, 0b010, 0b010, 0b010, 0b010}},
        {{0b101, 0b101, 0b101, 0b101, 0b111}},
        {{0b101, 0b101, 0b101, 0b101, 0b010}},
        {{0b101, 0b101, 0b111, 0b111, 0b101}},
        {{0b101, 0b101, 0b010, 0b101, 0b101}},
        {{0b101, 0b101, 0b010, 0b010, 0b010}},
        {{0b111, 0b001, 0b010, 0b100, 0b111}},
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
    static void drawText(RpiPixelCanvas& canvas,
                         int x,
                         int y,
                         std::string_view text,
                         int scale,
                         RpiRgba color,
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

    static void drawGlyph(RpiPixelCanvas& canvas,
                          int x,
                          int y,
                          char c,
                          int scale,
                          RpiRgba color,
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
                    RpiRectI{
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

std::size_t selectAnimFrameIndex(const UiAnimSlotComponent& anim, uint64_t tick) noexcept {
    if (anim.frames.empty()) {
        return 0U;
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
    return idx;
}

std::string formatAnimFrameLabel(const UiAnimSlotComponent& anim, uint64_t tick) {
    if (anim.frames.empty()) {
        return "anim";
    }
    const std::size_t idx = selectAnimFrameIndex(anim, tick);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "frame:%u", static_cast<unsigned>(idx + 1U));
    return std::string(buf);
}

} // namespace

void renderPreparedLayoutScene(const RpiPrimitiveScenePaintContext& ctx,
                               const UiPreparedLayout& prepared) {
    if (!ctx.canvas || !prepared.layoutTemplate) {
        return;
    }
    RpiPixelCanvas& canvas = *ctx.canvas;
    canvas.clear(kBgMain);

    const render::UiFrameMetrics metrics = render::computeFrameMetrics(
        prepared,
        static_cast<float>(canvas.width()),
        static_cast<float>(canvas.height()),
        10.0f,
        16.0f,
        10.0f,
        80.0f,
        4.0f,
        8.0f);

    const UiLayoutNode* rootNode = &prepared.layoutTemplate->root;
    const RpiRgba defaultText = resolveColor(rootNode ? rootNode->defaultTextColor : "", kText);
    const RpiRgba frameBorder = nodeBorderColor(rootNode, nullptr, kBorder);
    const RpiRgba frameBg = nodeBackgroundColor(rootNode, nullptr).value_or(kBgPanel);

    const RpiRectI frameRect =
        toRectI(render::charsToPixelsTopDown(metrics, 0, 0, metrics.frameWidthChars, metrics.frameHeightChars));
    canvas.fillRect(frameRect, frameBg, 1.0f);
    canvas.strokeRect(frameRect, frameBorder, 2, 1.0f);

    const uint16_t innerW = static_cast<uint16_t>(std::max<int>(1, static_cast<int>(metrics.frameWidthChars) - 2));
    const UiLayoutEngine::Result layout = UiLayoutEngine::arrange(prepared.layoutTemplate->root, innerW, metrics.innerHeightChars);
    const render::UiComponentIndex byId = render::buildComponentIndex(prepared);

    auto measureTextWidth = [&](std::string_view text,
                                std::string_view fontSpec,
                                int pixelSize,
                                int fallbackScale) -> int {
        if (text.empty()) {
            return 0;
        }
#if defined(AVANTGARDE_HAS_FREETYPE)
        if (!fontSpec.empty()) {
            FreetypeRuntime& rt = freetypeRuntime();
            if (rt.ok()) {
                std::string path{};
                if (resolveFontFilePath(fontSpec, ctx.cwd, path)) {
                    if (FT_Face face = rt.face(path, std::max(6, pixelSize)); face) {
                        const int w = measureTextFt(face, text);
                        if (w > 0) {
                            return w;
                        }
                    }
                }
            }
        }
#else
        (void)fontSpec;
        (void)pixelSize;
#endif
        return TinyFontPainter::textWidth(text, fallbackScale);
    };

    auto drawTextLine = [&](int x,
                            int y,
                            std::string_view text,
                            std::string_view fontSpec,
                            int pixelSize,
                            int fallbackScale,
                            RpiRgba color,
                            float opacity01) {
#if defined(AVANTGARDE_HAS_FREETYPE)
        if (!fontSpec.empty()) {
            if (drawTextFt(canvas, x, y, text, fontSpec, pixelSize, color, opacity01, ctx.cwd)) {
                return;
            }
        }
#else
        (void)fontSpec;
        (void)pixelSize;
#endif
        TinyFontPainter::drawText(canvas, x, y, text, fallbackScale, color, opacity01);
    };

    auto drawTextInRect = [&](const RpiRectI& rect,
                              std::string_view text,
                              std::string_view fontSpec,
                              float fontPx,
                              int fallbackScale,
                              UiLayoutAlign align,
                              UiLayoutJustify justify,
                              RpiRgba color,
                              float opacity01,
                              bool wrap) {
        if (rect.w <= 0 || rect.h <= 0 || text.empty()) {
            return;
        }
        const int px = (fontPx > 0.0f) ? static_cast<int>(std::lround(fontPx)) : (fallbackScale * 7);
        const int lineH = std::max(
            TinyFontPainter::lineHeight(fallbackScale),
            std::max(6, px + std::max(1, px / 5)));

        std::vector<std::string> lines{};
        if (!wrap) {
            lines.emplace_back(text);
        } else {
            std::size_t start = 0U;
            while (start < text.size()) {
                while (start < text.size() && text[start] == ' ') {
                    ++start;
                }
                if (start >= text.size()) {
                    break;
                }
                std::size_t end = start;
                std::size_t fitEnd = start;
                int fitWidth = 0;
                while (end < text.size()) {
                    std::size_t next = text.find(' ', end);
                    if (next == std::string_view::npos) {
                        next = text.size();
                    }
                    const std::string_view chunk = text.substr(start, next - start);
                    const int chunkW = measureTextWidth(chunk, fontSpec, px, fallbackScale);
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
                lines.emplace_back(text.substr(start, fitEnd - start));
                start = fitEnd;
            }
        }
        if (lines.empty()) {
            return;
        }

        const std::size_t maxLines = static_cast<std::size_t>(std::max(1, rect.h / lineH));
        if (lines.size() > maxLines) {
            lines.resize(maxLines);
        }
        const int blockH = lineH * static_cast<int>(lines.size());
        int y = rect.y;
        switch (justify) {
            case UiLayoutJustify::Center:
                y = rect.y + std::max(0, (rect.h - blockH) / 2);
                break;
            case UiLayoutJustify::End:
            case UiLayoutJustify::SpaceBetween:
                y = rect.y + std::max(0, rect.h - blockH);
                break;
            case UiLayoutJustify::Start:
            default:
                break;
        }

        for (const std::string& line : lines) {
            const int textW = measureTextWidth(line, fontSpec, px, fallbackScale);
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
            drawTextLine(x, y, line, fontSpec, px, fallbackScale, color, opacity01);
            y += lineH;
            if (y + lineH > rect.y + rect.h + 1) {
                break;
            }
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

        const RpiRectI rect = toRectI(render::charsToPixelsTopDown(
            metrics,
            static_cast<int16_t>(box.rect.x + 1),
            static_cast<int16_t>(box.rect.y + 1),
            box.rect.width,
            box.rect.height));
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        if (const auto bg = nodeBackgroundColor(node, component); bg.has_value()) {
            canvas.fillRect(rect, *bg, nodeOpacity);
        }

        const RpiRgba textColor = nodeTextColor(node, component, defaultText);
        const RpiRgba borderColor = nodeBorderColor(node, component, frameBorder);
        const int textScale = fontScaleFromNode(node);
        const std::string fontSpec = resolveFontSpec(nodeFontSpec(node, component));
        const float fontPx = nodeFontSize(node, component, 0.0f);

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
                drawTextInRect(RpiRectI{rect.x + 2, rect.y, std::max(0, rect.w - 4), rect.h},
                               text,
                               fontSpec,
                               fontPx,
                               textScale,
                               align,
                               node->justify,
                               textColor,
                               nodeOpacity,
                               wrap);
            } break;

            case UiLayoutNodeType::Separator: {
                const int y = rect.y + rect.h / 2;
                canvas.line(rect.x, y, rect.x + rect.w - 1, y, borderColor, nodeOpacity);
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
                    const RpiRgba rowColor = selected ? kAccent : textColor;
                    drawTextInRect(RpiRectI{rect.x + 2, y, std::max(0, rect.w - 4), lineH},
                                   row,
                                   fontSpec,
                                   (fontPx > 0.0f) ? std::max(6.0f, fontPx * 0.86f) : 0.0f,
                                   std::max(1, textScale - 1),
                                   UiLayoutAlign::Start,
                                   UiLayoutJustify::Start,
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
                    drawTextInRect(RpiRectI{rect.x, rect.y, rect.w, labelH},
                                   knob->label,
                                   fontSpec,
                                   (fontPx > 0.0f) ? std::max(6.0f, fontPx * 0.86f) : 0.0f,
                                   std::max(1, textScale - 1),
                                   node->align,
                                   UiLayoutJustify::Start,
                                   textColor,
                                   nodeOpacity,
                                   false);
                }
                const int cy = rect.y + labelH + std::max(8, (rect.h - labelH) / 2);
                const int cx = rect.x + rect.w / 2;
                const int radius = std::max(6, std::min(rect.w, rect.h - labelH) / 3);
                canvas.circle(cx, cy, radius, borderColor, nodeOpacity);

                const float v = std::clamp(knob->value01, 0.0f, 1.0f);
                const float a = (225.0f - v * 270.0f) * static_cast<float>(M_PI / 180.0);
                const int nx = static_cast<int>(std::lround(static_cast<float>(cx) + std::cos(a) * radius * 0.8f));
                const int ny = static_cast<int>(std::lround(static_cast<float>(cy) + std::sin(a) * radius * 0.8f));
                canvas.line(cx, cy, nx, ny, borderColor, nodeOpacity);
                if (knob->selected) {
                    canvas.strokeRect(RpiRectI{rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, kAccent, 1, nodeOpacity);
                }
            } break;

            case UiLayoutNodeType::Switch: {
                const auto* sw = dynamic_cast<const UiSwitchComponent*>(component);
                if (!sw) {
                    break;
                }
                const std::string text = composeSwitchText(*sw);
                drawTextInRect(RpiRectI{rect.x + 1, rect.y + 1, std::max(0, rect.w - 2), std::max(0, rect.h - 2)},
                               text,
                               fontSpec,
                               (fontPx > 0.0f) ? std::max(6.0f, fontPx * 0.86f) : 0.0f,
                               std::max(1, textScale - 1),
                               UiLayoutAlign::Start,
                               UiLayoutJustify::Start,
                               textColor,
                               nodeOpacity,
                               true);
                canvas.strokeRect(rect, borderColor, 1, nodeOpacity);
            } break;

            case UiLayoutNodeType::Icon: {
                const auto* icon = dynamic_cast<const UiIconComponent*>(component);
                const int pad = std::max(0, static_cast<int>(node->padding));
                const RpiRectI iconRect{
                    rect.x + 2 + pad,
                    rect.y + 2 + pad,
                    std::max(4, rect.w - 4 - pad * 2),
                    std::max(4, rect.h - 4 - pad * 2)};
                std::string path{};
                if (icon && !icon->path.empty()) {
                    path = icon->path;
                } else if (!node->assetPath.empty()) {
                    path = node->assetPath;
                }
                const RpiImage* image = loadImageCached(path, ctx.cwd);
                if (image) {
                    drawImageFit(canvas, iconRect, *image, nodeOpacity);
                } else {
                    canvas.strokeRect(iconRect, borderColor, 1, nodeOpacity);
                    canvas.line(iconRect.x, iconRect.y, iconRect.x + iconRect.w - 1, iconRect.y + iconRect.h - 1, borderColor, nodeOpacity);
                    canvas.line(iconRect.x + iconRect.w - 1, iconRect.y, iconRect.x, iconRect.y + iconRect.h - 1, borderColor, nodeOpacity);
                }
            } break;

            case UiLayoutNodeType::AnimSlot: {
                const auto* anim = dynamic_cast<const UiAnimSlotComponent*>(component);
                if (!anim) {
                    break;
                }
                const int pad = std::max(0, static_cast<int>(node->padding));
                RpiRectI slot{
                    rect.x + 2 + pad,
                    rect.y + 2 + pad,
                    std::max(10, std::min(128, rect.w - 4 - pad * 2)),
                    std::max(10, std::min(128, rect.h - 4 - pad * 2))};
                if (node->animShowFrame) {
                    const int frameW = std::max(1, static_cast<int>(std::lround(node->animFrameWidth)));
                    canvas.strokeRect(slot, borderColor, frameW, nodeOpacity);
                    const int inset = std::max(2, frameW + 1);
                    slot.x += inset;
                    slot.y += inset;
                    slot.w = std::max(1, slot.w - inset * 2);
                    slot.h = std::max(1, slot.h - inset * 2);
                }
                const uint64_t ms = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - ctx.startTs)
                        .count());
                const float fps = std::max(0.1f, anim->fps);
                const uint64_t tick = static_cast<uint64_t>(
                    std::floor(static_cast<double>(ms) * static_cast<double>(fps) / 1000.0));
                bool drawn = false;
                if (!anim->frames.empty()) {
                    const std::size_t idx = selectAnimFrameIndex(*anim, tick);
                    const std::string& framePath = anim->frames[idx];
                    if (const RpiImage* img = loadImageCached(framePath, ctx.cwd)) {
                        drawImageFit(canvas, slot, *img, nodeOpacity);
                        drawn = true;
                    }
                }
                if (!drawn) {
                    const std::string animLabel = formatAnimFrameLabel(*anim, tick);
                    drawTextInRect(RpiRectI{slot.x, slot.y, slot.w, slot.h},
                                   animLabel,
                                   fontSpec,
                                   (fontPx > 0.0f) ? std::max(6.0f, fontPx * 0.86f) : 0.0f,
                                   1,
                                   UiLayoutAlign::Center,
                                   UiLayoutJustify::Center,
                                   textColor,
                                   nodeOpacity,
                                   false);
                }
            } break;

            case UiLayoutNodeType::Waveform: {
                const auto* wave = dynamic_cast<const UiWaveformComponent*>(component);
                if (!wave) {
                    break;
                }
                const RpiRectI area{rect.x + 2, rect.y + 2, std::max(8, rect.w - 4), std::max(6, rect.h - 4)};
                canvas.strokeRect(area, borderColor, 1, nodeOpacity);
                if (!wave->peaks01.empty()) {
                    if (wave->curveMode) {
                        int prevX = area.x;
                        int prevY = area.y + area.h / 2;
                        for (std::size_t i = 0; i < wave->peaks01.size(); ++i) {
                            const float x01 = (wave->peaks01.size() > 1U)
                                                  ? static_cast<float>(i) / static_cast<float>(wave->peaks01.size() - 1U)
                                                  : 0.0f;
                            const float y01 = std::clamp(wave->peaks01[i], 0.0f, 1.0f);
                            const int x = area.x + static_cast<int>(
                                                       std::lround(x01 * static_cast<float>(std::max(1, area.w - 1))));
                            const int y = area.y + static_cast<int>(
                                                       std::lround((1.0f - y01) * static_cast<float>(std::max(1, area.h - 1))));
                            if (i > 0U) {
                                canvas.line(prevX, prevY, x, y, textColor, nodeOpacity);
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
                            canvas.line(x, y0, x, y0 + h - 1, textColor, nodeOpacity);
                        }
                    }
                }
                const int px = area.x + static_cast<int>(std::lround(
                                            std::clamp(wave->playhead01, 0.0f, 1.0f) *
                                            static_cast<float>(std::max(1, area.w - 1))));
                canvas.line(px, area.y, px, area.y + area.h - 1, kAccent, nodeOpacity);
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

        RpiRectI hudRect = toRectI(render::charsToPixelsTopDown(metrics, hudX, hudY, hudW, hudH));
        const float scale = std::clamp(hud.scale, 0.80f, 1.20f);
        if (std::fabs(scale - 1.0f) > 0.0001f) {
            const int cx = hudRect.x + hudRect.w / 2;
            const int cy = hudRect.y + hudRect.h / 2;
            hudRect.w = static_cast<int>(std::lround(static_cast<float>(hudRect.w) * scale));
            hudRect.h = static_cast<int>(std::lround(static_cast<float>(hudRect.h) * scale));
            hudRect.x = cx - hudRect.w / 2;
            hudRect.y = cy - hudRect.h / 2;
        }

        const RpiRgba hudText = resolveColor(hud.textColor, defaultText);
        const RpiRgba hudBorder = resolveColor(hud.borderColor, frameBorder);
        const RpiRgba hudBg = resolveColor(hud.backgroundColor, kBgPanel);
        const float baseOpacity = std::clamp(hud.opacity, 0.0f, 1.0f);

        canvas.fillRect(hudRect, hudBg, baseOpacity);
        canvas.strokeRect(hudRect, hudBorder, 1, baseOpacity);

        const int hudScale = std::clamp(
            static_cast<int>(std::lround((hud.fontSize > 0.0f ? hud.fontSize : 14.0f) / 7.0f)),
            1,
            5);
        drawTextInRect(RpiRectI{hudRect.x + static_cast<int>(hud.padding),
                                 hudRect.y + static_cast<int>(hud.padding),
                                 std::max(1, hudRect.w - static_cast<int>(hud.padding) * 2),
                                 std::max(1, hudRect.h - static_cast<int>(hud.padding) * 2)},
                       hud.text,
                       resolveFontSpec(hud.font),
                       hud.fontSize,
                       hudScale,
                       hud.align,
                       hud.justify,
                       hudText,
                       baseOpacity,
                       hud.textWrap);
    }
}

} // namespace avantgarde::raspi
