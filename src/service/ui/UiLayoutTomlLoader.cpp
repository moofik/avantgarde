#include "service/ui/UiLayoutTomlLoader.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace avantgarde {
namespace {

enum class ParseContextType : uint8_t {
    TopLevel = 0,
    LayoutNode
};

struct ParseContext {
    ParseContextType type{ParseContextType::TopLevel};
    UiLayoutNode* node{nullptr};
};

std::string trim(std::string_view raw) {
    std::size_t begin = 0;
    std::size_t end = raw.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1U])) != 0) {
        --end;
    }
    return std::string(raw.substr(begin, end - begin));
}

std::string stripComment(std::string_view line) {
    bool inString = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (ch == '#' && !inString) {
            return std::string(line.substr(0, i));
        }
    }
    return std::string(line);
}

bool splitKeyValue(std::string_view line, std::string& keyOut, std::string& valueOut) {
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
        return false;
    }
    keyOut = trim(line.substr(0, eq));
    valueOut = trim(line.substr(eq + 1U));
    return !keyOut.empty();
}

bool parseQuotedString(std::string_view raw, std::string& out) {
    const std::string v = trim(raw);
    if (v.size() < 2U || v.front() != '"' || v.back() != '"') {
        return false;
    }
    out = v.substr(1U, v.size() - 2U);
    return true;
}

bool parseBool(std::string_view raw, bool& out) {
    std::string v = trim(raw);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (v == "true") {
        out = true;
        return true;
    }
    if (v == "false") {
        out = false;
        return true;
    }
    return false;
}

bool parseLayoutJustify(std::string_view raw, UiLayoutJustify& out) {
    std::string value{};
    if (!parseQuotedString(raw, value)) {
        return false;
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "start" || value == "left" || value == "top") {
        out = UiLayoutJustify::Start;
        return true;
    }
    if (value == "center" || value == "middle") {
        out = UiLayoutJustify::Center;
        return true;
    }
    if (value == "end" || value == "right" || value == "bottom") {
        out = UiLayoutJustify::End;
        return true;
    }
    if (value == "space_between" || value == "space-between") {
        out = UiLayoutJustify::SpaceBetween;
        return true;
    }
    return false;
}

bool parseLayoutAlign(std::string_view raw, UiLayoutAlign& out) {
    std::string value{};
    if (!parseQuotedString(raw, value)) {
        return false;
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "start" || value == "left" || value == "top") {
        out = UiLayoutAlign::Start;
        return true;
    }
    if (value == "center" || value == "middle") {
        out = UiLayoutAlign::Center;
        return true;
    }
    if (value == "end" || value == "right" || value == "bottom") {
        out = UiLayoutAlign::End;
        return true;
    }
    return false;
}

bool parseUint16(std::string_view raw, uint16_t& out) {
    const std::string v = trim(raw);
    if (v.empty()) {
        return false;
    }
    uint32_t tmp = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), tmp);
    if (ec != std::errc() || ptr != v.data() + v.size() || tmp > 65535U) {
        return false;
    }
    out = static_cast<uint16_t>(tmp);
    return true;
}

bool parseFloat(std::string_view raw, float& out) {
    const std::string v = trim(raw);
    if (v.empty()) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(v.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parseDurationMs(std::string_view raw, uint32_t& out) {
    std::string token{};
    if (!parseQuotedString(raw, token)) {
        token = trim(raw);
    }
    token = trim(token);
    if (token.empty()) {
        return false;
    }
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    float value = 0.0f;
    float mul = 1.0f;
    if (token.size() > 2U && token.substr(token.size() - 2U) == "ms") {
        if (!parseFloat(std::string_view(token.data(), token.size() - 2U), value)) {
            return false;
        }
        mul = 1.0f;
    } else if (!token.empty() && token.back() == 's') {
        if (!parseFloat(std::string_view(token.data(), token.size() - 1U), value)) {
            return false;
        }
        mul = 1000.0f;
    } else {
        if (!parseFloat(token, value)) {
            return false;
        }
        mul = 1.0f;
    }

    if (!std::isfinite(value) || value < 0.0f) {
        return false;
    }
    const double ms = static_cast<double>(value) * static_cast<double>(mul);
    if (!std::isfinite(ms) || ms < 0.0 ||
        ms > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    out = static_cast<uint32_t>(std::llround(ms));
    return true;
}

bool parseSize(std::string_view raw, UiLayoutSize& out) {
    std::string asString;
    if (parseQuotedString(raw, asString)) {
        const std::string lower = [&asString]() {
            std::string s = asString;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }();
        if (lower == "auto") {
            out.unit = UiLayoutSize::Unit::Auto;
            out.value = 0.0f;
            return true;
        }
        if (!lower.empty() && lower.back() == '%') {
            float p = 0.0f;
            if (!parseFloat(std::string_view(lower).substr(0, lower.size() - 1U), p)) {
                return false;
            }
            out.unit = UiLayoutSize::Unit::Percent;
            out.value = p;
            return true;
        }
        float px = 0.0f;
        if (!parseFloat(lower, px)) {
            return false;
        }
        out.unit = UiLayoutSize::Unit::Px;
        out.value = px;
        return true;
    }

    float px = 0.0f;
    if (!parseFloat(raw, px)) {
        return false;
    }
    out.unit = UiLayoutSize::Unit::Px;
    out.value = px;
    return true;
}

bool parseSizeTuple(std::string_view raw, uint16_t& wOut, uint16_t& hOut) {
    const std::string v = trim(raw);
    if (v.size() < 5U || v.front() != '[' || v.back() != ']') {
        return false;
    }
    const std::string_view inside(v.data() + 1U, v.size() - 2U);
    const std::size_t comma = inside.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    uint16_t w = 0;
    uint16_t h = 0;
    if (!parseUint16(inside.substr(0, comma), w)) {
        return false;
    }
    if (!parseUint16(inside.substr(comma + 1U), h)) {
        return false;
    }
    wOut = w;
    hOut = h;
    return true;
}

bool parseStringList(std::string_view raw, std::vector<std::string>& out) {
    const std::string v = trim(raw);
    if (v.size() < 2U || v.front() != '[' || v.back() != ']') {
        return false;
    }
    const std::string_view inside(v.data() + 1U, v.size() - 2U);
    out.clear();

    std::size_t i = 0;
    while (i < inside.size()) {
        while (i < inside.size() &&
               (std::isspace(static_cast<unsigned char>(inside[i])) != 0 || inside[i] == ',')) {
            ++i;
        }
        if (i >= inside.size()) {
            break;
        }
        if (inside[i] != '"') {
            return false;
        }
        ++i; // skip opening quote
        std::size_t start = i;
        while (i < inside.size() && inside[i] != '"') {
            ++i;
        }
        if (i >= inside.size()) {
            return false;
        }
        out.emplace_back(std::string(inside.substr(start, i - start)));
        ++i; // skip closing quote
        while (i < inside.size() && std::isspace(static_cast<unsigned char>(inside[i])) != 0) {
            ++i;
        }
        if (i < inside.size() && inside[i] == ',') {
            ++i;
        }
    }
    return true;
}

std::vector<std::string> splitPath(std::string_view path) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t dot = path.find('.', begin);
        if (dot == std::string_view::npos) {
            out.emplace_back(trim(path.substr(begin)));
            break;
        }
        out.emplace_back(trim(path.substr(begin, dot - begin)));
        begin = dot + 1U;
    }
    return out;
}

bool parseHeader(std::string_view line, bool& isArrayHeader, std::vector<std::string>& pathOut) {
    const std::string v = trim(line);
    if (v.size() < 3U || v.front() != '[' || v.back() != ']') {
        return false;
    }
    isArrayHeader = (v.size() >= 4U && v[1] == '[' && v[v.size() - 2U] == ']');
    std::string_view payload{};
    if (isArrayHeader) {
        payload = std::string_view(v).substr(2U, v.size() - 4U);
    } else {
        payload = std::string_view(v).substr(1U, v.size() - 2U);
    }
    pathOut = splitPath(payload);
    return !pathOut.empty();
}

bool applyNodeProperty(UiLayoutNode& node,
                       const std::string& key,
                       const std::string& value,
                       std::string& errorOut,
                       std::size_t lineNo) {
    if (key == "type") {
        std::string typeRaw;
        if (!parseQuotedString(value, typeRaw)) {
            errorOut = "line " + std::to_string(lineNo) + ": type expects quoted string";
            return false;
        }
        node.type = parseUiLayoutNodeType(typeRaw);
        if (node.type == UiLayoutNodeType::Unknown) {
            errorOut = "line " + std::to_string(lineNo) + ": unknown node type '" + typeRaw + "'";
            return false;
        }
        return true;
    }
    if (key == "id") {
        if (!parseQuotedString(value, node.id)) {
            errorOut = "line " + std::to_string(lineNo) + ": id expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "text") {
        if (!parseQuotedString(value, node.text)) {
            errorOut = "line " + std::to_string(lineNo) + ": text expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "label") {
        if (!parseQuotedString(value, node.label)) {
            errorOut = "line " + std::to_string(lineNo) + ": label expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "knob_size") {
        float scale = 0.0f;
        if (!parseFloat(value, scale)) {
            errorOut = "line " + std::to_string(lineNo) + ": knob_size expects float";
            return false;
        }
        // Защита от невалидных/экстремальных значений в шаблоне.
        node.knobSize = std::clamp(scale, 0.2f, 4.0f);
        return true;
    }
    if (key == "font") {
        if (!parseQuotedString(value, node.font)) {
            errorOut = "line " + std::to_string(lineNo) + ": font expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "font_size") {
        float size = 0.0f;
        if (!parseFloat(value, size)) {
            errorOut = "line " + std::to_string(lineNo) + ": font_size expects float";
            return false;
        }
        node.fontSize = size;
        return true;
    }
    if (key == "effect") {
        if (!parseQuotedString(value, node.effect)) {
            errorOut = "line " + std::to_string(lineNo) + ": effect expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "effect_trigger") {
        if (!parseQuotedString(value, node.effectTrigger)) {
            errorOut = "line " + std::to_string(lineNo) + ": effect_trigger expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "effect_trigger_out") {
        uint32_t outMs = 0;
        if (!parseDurationMs(value, outMs)) {
            errorOut = "line " + std::to_string(lineNo) +
                       ": effect_trigger_out expects duration (e.g. \"1s\", \"750ms\", 1000)";
            return false;
        }
        node.effectTriggerOutMs = outMs;
        return true;
    }
    if (key == "effect_interval_ms") {
        uint16_t ms = 0;
        if (!parseUint16(value, ms)) {
            errorOut = "line " + std::to_string(lineNo) + ": effect_interval_ms expects integer";
            return false;
        }
        node.effectIntervalMs = ms;
        return true;
    }
    if (key == "effect_amount") {
        float amount = 0.0f;
        if (!parseFloat(value, amount)) {
            errorOut = "line " + std::to_string(lineNo) + ": effect_amount expects float";
            return false;
        }
        node.effectAmount = amount;
        return true;
    }
    if (key == "effect_speed") {
        float speed = 0.0f;
        if (!parseFloat(value, speed)) {
            errorOut = "line " + std::to_string(lineNo) + ": effect_speed expects float";
            return false;
        }
        node.effectSpeed = speed;
        return true;
    }
    if (key == "bind") {
        if (!parseQuotedString(value, node.bind)) {
            errorOut = "line " + std::to_string(lineNo) + ": bind expects quoted string";
            return false;
        }
        return true;
    }
    if (key == "options") {
        std::vector<std::string> options{};
        if (!parseStringList(value, options)) {
            errorOut = "line " + std::to_string(lineNo) + ": options expects [\"a\", \"b\", ...]";
            return false;
        }
        node.options = std::move(options);
        return true;
    }
    if (key == "padding") {
        uint16_t padding = 0;
        if (!parseUint16(value, padding)) {
            errorOut = "line " + std::to_string(lineNo) + ": padding expects integer";
            return false;
        }
        node.padding = padding;
        return true;
    }
    if (key == "gap") {
        uint16_t gap = 0;
        if (!parseUint16(value, gap)) {
            errorOut = "line " + std::to_string(lineNo) + ": gap expects integer";
            return false;
        }
        node.gap = gap;
        return true;
    }
    if (key == "width") {
        UiLayoutSize sz{};
        if (!parseSize(value, sz)) {
            errorOut = "line " + std::to_string(lineNo) + ": width expects px/percent/auto";
            return false;
        }
        node.width = sz;
        return true;
    }
    if (key == "wrap") {
        bool wrap = true;
        if (!parseBool(value, wrap)) {
            errorOut = "line " + std::to_string(lineNo) + ": wrap expects true/false";
            return false;
        }
        node.wrap = wrap;
        return true;
    }
    if (key == "justify") {
        UiLayoutJustify justify = UiLayoutJustify::Start;
        if (!parseLayoutJustify(value, justify)) {
            errorOut = "line " + std::to_string(lineNo) +
                       ": justify expects \"start\"|\"center\"|\"end\"|\"space_between\"";
            return false;
        }
        node.justify = justify;
        return true;
    }
    if (key == "align") {
        UiLayoutAlign align = UiLayoutAlign::Start;
        if (!parseLayoutAlign(value, align)) {
            errorOut = "line " + std::to_string(lineNo) +
                       ": align expects \"start\"|\"center\"|\"end\"";
            return false;
        }
        node.align = align;
        return true;
    }
    if (key == "text_wrap") {
        bool wrap = false;
        if (!parseBool(value, wrap)) {
            errorOut = "line " + std::to_string(lineNo) + ": text_wrap expects true/false";
            return false;
        }
        node.textWrap = wrap;
        return true;
    }
    if (key == "height") {
        UiLayoutSize sz{};
        if (!parseSize(value, sz)) {
            errorOut = "line " + std::to_string(lineNo) + ": height expects px/percent/auto";
            return false;
        }
        node.height = sz;
        return true;
    }
    if (key == "size") {
        uint16_t w = 0;
        uint16_t h = 0;
        if (!parseSizeTuple(value, w, h)) {
            errorOut = "line " + std::to_string(lineNo) + ": size expects [w, h]";
            return false;
        }
        node.width = UiLayoutSize{UiLayoutSize::Unit::Px, static_cast<float>(w)};
        node.height = UiLayoutSize{UiLayoutSize::Unit::Px, static_cast<float>(h)};
        return true;
    }

    // Неизвестные ключи в MVP пропускаем мягко, чтобы не ломать forward-compat.
    return true;
}

bool resolveNodeContext(const std::vector<std::string>& path,
                        bool isArrayHeader,
                        UiLayoutTemplate& out,
                        std::vector<UiLayoutNode*>& stackByDepth,
                        ParseContext& ctx,
                        std::string& errorOut,
                        std::size_t lineNo) {
    if (path.empty() || path.front() != "layout") {
        errorOut = "line " + std::to_string(lineNo) + ": only layout.* tables are supported";
        return false;
    }

    if (!isArrayHeader) {
        if (path.size() != 1U) {
            errorOut = "line " + std::to_string(lineNo) + ": only [layout] table is supported";
            return false;
        }
        stackByDepth.assign(1U, &out.root);
        ctx.type = ParseContextType::LayoutNode;
        ctx.node = &out.root;
        return true;
    }

    // Для [[layout.children...]] все сегменты после "layout" должны быть "children".
    if (path.size() < 2U) {
        errorOut = "line " + std::to_string(lineNo) + ": array table must target layout.children";
        return false;
    }
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (path[i] != "children") {
            errorOut = "line " + std::to_string(lineNo) + ": unsupported array path segment '" + path[i] + "'";
            return false;
        }
    }

    const std::size_t depth = path.size() - 1U; // 1=child of root, 2=child of last child, ...
    if (stackByDepth.empty()) {
        stackByDepth.assign(1U, &out.root);
    }
    if (depth >= 1U && (depth - 1U) >= stackByDepth.size()) {
        errorOut = "line " + std::to_string(lineNo) + ": cannot append child before parent is declared";
        return false;
    }

    UiLayoutNode* parent = stackByDepth[depth - 1U];
    if (!parent) {
        errorOut = "line " + std::to_string(lineNo) + ": parent node is null";
        return false;
    }
    parent->children.emplace_back();
    UiLayoutNode* created = &parent->children.back();

    if (stackByDepth.size() <= depth) {
        stackByDepth.resize(depth + 1U, nullptr);
    }
    stackByDepth[depth] = created;
    stackByDepth.resize(depth + 1U);

    ctx.type = ParseContextType::LayoutNode;
    ctx.node = created;
    return true;
}

bool parseImpl(std::string_view content,
               UiLayoutTemplate& out,
               std::string& errorOut) {
    out = UiLayoutTemplate{};
    out.root.type = UiLayoutNodeType::Column;

    ParseContext ctx{};
    std::vector<UiLayoutNode*> stackByDepth{};
    stackByDepth.assign(1U, &out.root);

    std::istringstream stream{std::string(content)};
    std::string rawLine;
    std::size_t lineNo = 0;
    while (std::getline(stream, rawLine)) {
        ++lineNo;
        const std::string noComment = stripComment(rawLine);
        const std::string line = trim(noComment);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            bool isArrayHeader = false;
            std::vector<std::string> path{};
            if (!parseHeader(line, isArrayHeader, path)) {
                errorOut = "line " + std::to_string(lineNo) + ": invalid table header";
                return false;
            }
            if (!resolveNodeContext(path, isArrayHeader, out, stackByDepth, ctx, errorOut, lineNo)) {
                return false;
            }
            continue;
        }

        std::string key{};
        std::string value{};
        if (!splitKeyValue(line, key, value)) {
            errorOut = "line " + std::to_string(lineNo) + ": invalid key/value assignment";
            return false;
        }

        if (ctx.type == ParseContextType::TopLevel) {
            if (key == "id") {
                if (!parseQuotedString(value, out.widgetId)) {
                    errorOut = "line " + std::to_string(lineNo) + ": top-level id expects quoted string";
                    return false;
                }
                continue;
            }
            // Прочие top-level ключи пока считаем допустимыми и игнорируем.
            continue;
        }

        if (!ctx.node) {
            errorOut = "line " + std::to_string(lineNo) + ": active node context is null";
            return false;
        }
        if (!applyNodeProperty(*ctx.node, key, value, errorOut, lineNo)) {
            return false;
        }
    }

    if (out.widgetId.empty()) {
        errorOut = "top-level id is required";
        return false;
    }
    if (out.root.type == UiLayoutNodeType::Unknown) {
        out.root.type = UiLayoutNodeType::Column;
    }
    return true;
}

} // namespace

bool UiLayoutTomlLoader::loadFromFile(const std::string& path,
                                      UiLayoutTemplate& out,
                                      std::string& errorOut) {
    std::ifstream in(path);
    if (!in.is_open()) {
        errorOut = "cannot open file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadFromString(ss.str(), out, errorOut);
}

bool UiLayoutTomlLoader::loadFromString(std::string_view content,
                                        UiLayoutTemplate& out,
                                        std::string& errorOut) {
    return parseImpl(content, out, errorOut);
}

} // namespace avantgarde
