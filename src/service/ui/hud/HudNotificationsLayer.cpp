#include "service/ui/hud/HudNotificationsLayer.h"
#include "service/ui/UiTokenResolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace avantgarde {
namespace {

struct JsonValue {
    enum class Type : uint8_t {
        Null = 0,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type{Type::Null};
    bool boolValue{false};
    double numberValue{0.0};
    std::string stringValue{};
    std::vector<JsonValue> arrayValue{};
    std::vector<std::pair<std::string, JsonValue>> objectValue{};
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view src)
        : src_(src) {}

    bool parse(JsonValue& out, std::string& errorOut) {
        skipWs_();
        if (!parseValue_(out)) {
            errorOut = errorMessage_();
            return false;
        }
        skipWs_();
        if (!eof_()) {
            setError_("unexpected trailing characters");
            errorOut = errorMessage_();
            return false;
        }
        return true;
    }

private:
    bool parseValue_(JsonValue& out) {
        skipWs_();
        if (eof_()) {
            return setError_("unexpected end of input");
        }
        const char ch = peek_();
        if (ch == '{') return parseObject_(out);
        if (ch == '[') return parseArray_(out);
        if (ch == '"') return parseStringValue_(out);
        if (ch == 't') return parseLiteral_("true", JsonValue::Type::Bool, out, true);
        if (ch == 'f') return parseLiteral_("false", JsonValue::Type::Bool, out, false);
        if (ch == 'n') return parseLiteral_("null", JsonValue::Type::Null, out, false);
        if (ch == '-' || (ch >= '0' && ch <= '9')) return parseNumber_(out);
        return setError_("unexpected token");
    }

    bool parseObject_(JsonValue& out) {
        if (!consume_('{')) {
            return false;
        }
        out = JsonValue{};
        out.type = JsonValue::Type::Object;
        skipWs_();
        if (consumeIf_('}')) {
            return true;
        }
        while (!eof_()) {
            std::string key{};
            if (!parseString_(key)) {
                return false;
            }
            skipWs_();
            if (!consume_(':')) {
                return false;
            }
            JsonValue val{};
            if (!parseValue_(val)) {
                return false;
            }
            out.objectValue.emplace_back(std::move(key), std::move(val));
            skipWs_();
            if (consumeIf_('}')) {
                return true;
            }
            if (!consume_(',')) {
                return false;
            }
            skipWs_();
        }
        return setError_("unterminated object");
    }

    bool parseArray_(JsonValue& out) {
        if (!consume_('[')) {
            return false;
        }
        out = JsonValue{};
        out.type = JsonValue::Type::Array;
        skipWs_();
        if (consumeIf_(']')) {
            return true;
        }
        while (!eof_()) {
            JsonValue item{};
            if (!parseValue_(item)) {
                return false;
            }
            out.arrayValue.push_back(std::move(item));
            skipWs_();
            if (consumeIf_(']')) {
                return true;
            }
            if (!consume_(',')) {
                return false;
            }
            skipWs_();
        }
        return setError_("unterminated array");
    }

    bool parseStringValue_(JsonValue& out) {
        std::string value{};
        if (!parseString_(value)) {
            return false;
        }
        out = JsonValue{};
        out.type = JsonValue::Type::String;
        out.stringValue = std::move(value);
        return true;
    }

    bool parseString_(std::string& out) {
        if (!consume_('"')) {
            return false;
        }
        out.clear();
        while (!eof_()) {
            const char ch = advance_();
            if (ch == '"') {
                return true;
            }
            if (ch == '\\') {
                if (eof_()) {
                    return setError_("invalid escape sequence");
                }
                const char esc = advance_();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: return setError_("unsupported escape sequence");
                }
                continue;
            }
            out.push_back(ch);
        }
        return setError_("unterminated string");
    }

    bool parseNumber_(JsonValue& out) {
        const std::size_t begin = pos_;
        if (peek_() == '-') {
            ++pos_;
        }
        if (eof_()) {
            return setError_("invalid number");
        }
        if (peek_() == '0') {
            ++pos_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(peek_()))) {
                return setError_("invalid number");
            }
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_()))) {
                ++pos_;
            }
        }
        if (!eof_() && peek_() == '.') {
            ++pos_;
            if (eof_() || !std::isdigit(static_cast<unsigned char>(peek_()))) {
                return setError_("invalid number fraction");
            }
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_()))) {
                ++pos_;
            }
        }
        if (!eof_() && (peek_() == 'e' || peek_() == 'E')) {
            ++pos_;
            if (!eof_() && (peek_() == '+' || peek_() == '-')) {
                ++pos_;
            }
            if (eof_() || !std::isdigit(static_cast<unsigned char>(peek_()))) {
                return setError_("invalid number exponent");
            }
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_()))) {
                ++pos_;
            }
        }

        const std::string token(src_.substr(begin, pos_ - begin));
        char* endPtr = nullptr;
        const double parsed = std::strtod(token.c_str(), &endPtr);
        if (endPtr == nullptr || *endPtr != '\0' || !std::isfinite(parsed)) {
            return setError_("invalid number value");
        }
        out = JsonValue{};
        out.type = JsonValue::Type::Number;
        out.numberValue = parsed;
        return true;
    }

    bool parseLiteral_(std::string_view literal,
                       JsonValue::Type type,
                       JsonValue& out,
                       bool boolValue) {
        if (src_.substr(pos_, literal.size()) != literal) {
            return setError_("invalid literal");
        }
        pos_ += literal.size();
        out = JsonValue{};
        out.type = type;
        out.boolValue = boolValue;
        return true;
    }

    bool consume_(char expected) {
        skipWs_();
        if (eof_() || peek_() != expected) {
            std::string msg = "expected '";
            msg.push_back(expected);
            msg.push_back('\'');
            return setError_(msg);
        }
        ++pos_;
        return true;
    }

    bool consumeIf_(char expected) {
        skipWs_();
        if (!eof_() && peek_() == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void skipWs_() {
        while (!eof_() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    char peek_() const {
        return src_[pos_];
    }

    char advance_() {
        return src_[pos_++];
    }

    bool eof_() const {
        return pos_ >= src_.size();
    }

    bool setError_(std::string msg) {
        if (error_.empty()) {
            error_ = std::move(msg);
            errorPos_ = pos_;
        }
        return false;
    }

    std::string errorMessage_() const {
        std::ostringstream oss;
        oss << "json parse error at " << errorPos_ << ": " << error_;
        return oss.str();
    }

private:
    std::string_view src_{};
    std::size_t pos_{0};
    std::string error_{};
    std::size_t errorPos_{0};
};

const JsonValue* findField(const JsonValue& object, std::string_view key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const auto& [k, v] : object.objectValue) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

std::string toLowerAscii(std::string value);

const JsonValue* findFieldCaseInsensitive(const JsonValue& object, std::string_view keyLower) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const auto& [k, v] : object.objectValue) {
        if (toLowerAscii(k) == keyLower) {
            return &v;
        }
    }
    return nullptr;
}

const JsonValue* findDottedNode(const JsonValue& root, std::string_view dottedLower) {
    if (root.type != JsonValue::Type::Object) {
        return nullptr;
    }
    std::string remain(dottedLower);
    const JsonValue* current = &root;
    while (true) {
        if (current->type != JsonValue::Type::Object) {
            return nullptr;
        }
        if (const JsonValue* exact = findFieldCaseInsensitive(*current, remain)) {
            return exact;
        }
        const std::size_t dot = remain.find('.');
        if (dot == std::string::npos) {
            return findFieldCaseInsensitive(*current, remain);
        }
        const std::string head = remain.substr(0U, dot);
        const JsonValue* next = findFieldCaseInsensitive(*current, head);
        if (!next) {
            return nullptr;
        }
        current = next;
        remain = remain.substr(dot + 1U);
    }
}

const JsonValue* resolveThemeTokenValue(const JsonValue* themesRoot,
                                        const JsonValue* raw) {
    if (!themesRoot || !raw || raw->type != JsonValue::Type::String) {
        return raw;
    }
    const auto key = UiTokenResolver::normalizeThemeRef(raw->stringValue);
    if (!key) {
        return raw;
    }
    return findDottedNode(*themesRoot, *key);
}

bool parseJsonFile(const std::filesystem::path& path,
                   JsonValue& out,
                   std::string& errorOut) {
    std::ifstream in(path);
    if (!in.is_open()) {
        errorOut = "cannot open file: " + path.string();
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    // Важно: JsonParser хранит string_view, поэтому буфер должен жить дольше parse().
    const std::string content = ss.str();
    JsonParser parser(content);
    if (!parser.parse(out, errorOut)) {
        errorOut = path.string() + ": " + errorOut;
        return false;
    }
    if (out.type != JsonValue::Type::Object) {
        errorOut = path.string() + ": root must be object";
        return false;
    }
    return true;
}

bool loadThemesCatalog(const std::string& fromConfigPath,
                       JsonValue& outThemes,
                       std::string& errorOut) {
    namespace fs = std::filesystem;
    std::error_code ec{};
    fs::path dir = fs::absolute(fs::path(fromConfigPath), ec).parent_path();
    if (ec) {
        dir = fs::path(fromConfigPath).parent_path();
    }
    while (!dir.empty()) {
        const fs::path candidate = dir / "themes.json";
        if (fs::exists(candidate)) {
            JsonValue root{};
            if (!parseJsonFile(candidate, root, errorOut)) {
                return false;
            }
            if (const JsonValue* themes = findField(root, "themes");
                themes && themes->type == JsonValue::Type::Object) {
                outThemes = *themes;
                return true;
            }
            if (const JsonValue* theme = findField(root, "theme");
                theme && theme->type == JsonValue::Type::Object) {
                outThemes = *theme;
                return true;
            }
            outThemes = std::move(root);
            return true;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    outThemes = JsonValue{};
    outThemes.type = JsonValue::Type::Object;
    return true;
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parseString(const JsonValue& value, std::string& out) {
    if (value.type != JsonValue::Type::String) {
        return false;
    }
    out = value.stringValue;
    return true;
}

bool parseFloat(const JsonValue& value, float& out) {
    if (value.type != JsonValue::Type::Number) {
        return false;
    }
    out = static_cast<float>(value.numberValue);
    return std::isfinite(out);
}

bool parseUint(const JsonValue& value, uint32_t& out) {
    if (value.type != JsonValue::Type::Number) {
        return false;
    }
    const double d = value.numberValue;
    if (!std::isfinite(d) || d < 0.0 || d > static_cast<double>(UINT32_MAX)) {
        return false;
    }
    out = static_cast<uint32_t>(std::llround(d));
    return true;
}

bool parseEffectSpec(const JsonValue& value,
                     UiLayoutNode::EffectSpec& out,
                     const JsonValue* themesRoot,
                     std::string* errorOut = nullptr,
                     std::string_view errorPath = {}) {
    if (value.type != JsonValue::Type::Object) {
        return false;
    }
    UiLayoutNode::EffectSpec fx{};
    if (const JsonValue* v = findField(value, "type")) {
        if (!parseString(*v, fx.type) || fx.type.empty()) {
            return false;
        }
    } else {
        return false;
    }
    if (const JsonValue* raw = findField(value, "effect_color")) {
        const JsonValue* v = resolveThemeTokenValue(themesRoot, raw);
        if (!v && errorOut) {
            *errorOut = std::string(errorPath) + ".effect_color unresolved theme token: " + raw->stringValue;
            return false;
        }
        if (v) {
            (void)parseString(*v, fx.effectColor);
        }
    }
    if (const JsonValue* v = findField(value, "effect_trigger")) {
        (void)parseString(*v, fx.effectTrigger);
    }
    if (const JsonValue* v = findField(value, "effect_transition")) {
        (void)parseString(*v, fx.effectTransition);
    }
    if (const JsonValue* raw = findField(value, "effect_trigger_out")) {
        const JsonValue* v = resolveThemeTokenValue(themesRoot, raw);
        if (!v && errorOut) {
            *errorOut = std::string(errorPath) + ".effect_trigger_out unresolved theme token: " + raw->stringValue;
            return false;
        }
        if (!v) {
            v = raw;
        }
        uint32_t ms = 0;
        if (parseUint(*v, ms)) {
            fx.effectTriggerOutMs = ms;
        } else if (v->type == JsonValue::Type::String) {
            std::string lower = toLowerAscii(v->stringValue);
            if (!lower.empty() && lower.size() > 2U && lower.substr(lower.size() - 2U) == "ms") {
                std::string num = lower.substr(0, lower.size() - 2U);
                char* end = nullptr;
                const float parsed = std::strtof(num.c_str(), &end);
                if (end != nullptr && *end == '\0' && std::isfinite(parsed) && parsed >= 0.0f) {
                    fx.effectTriggerOutMs = static_cast<uint32_t>(std::llround(parsed));
                }
            }
        }
    }
    if (const JsonValue* raw = findField(value, "effect_interval_ms")) {
        const JsonValue* v = resolveThemeTokenValue(themesRoot, raw);
        if (!v && errorOut) {
            *errorOut = std::string(errorPath) + ".effect_interval_ms unresolved theme token: " + raw->stringValue;
            return false;
        }
        if (!v) {
            v = raw;
        }
        uint32_t iv = 0;
        if (parseUint(*v, iv)) {
            fx.effectIntervalMs = static_cast<uint16_t>(std::min<uint32_t>(iv, 65535U));
        }
    }
    if (const JsonValue* raw = findField(value, "effect_amount")) {
        const JsonValue* v = resolveThemeTokenValue(themesRoot, raw);
        if (!v && errorOut) {
            *errorOut = std::string(errorPath) + ".effect_amount unresolved theme token: " + raw->stringValue;
            return false;
        }
        if (!v) {
            v = raw;
        }
        (void)parseFloat(*v, fx.effectAmount);
    }
    if (const JsonValue* raw = findField(value, "effect_speed")) {
        const JsonValue* v = resolveThemeTokenValue(themesRoot, raw);
        if (!v && errorOut) {
            *errorOut = std::string(errorPath) + ".effect_speed unresolved theme token: " + raw->stringValue;
            return false;
        }
        if (!v) {
            v = raw;
        }
        (void)parseFloat(*v, fx.effectSpeed);
    }
    out = std::move(fx);
    return true;
}

UiHudPosition parseHudPosition(std::string raw) {
    const std::string key = toLowerAscii(std::move(raw));
    if (key == "center") {
        return UiHudPosition::Center;
    }
    return UiHudPosition::TopCenter;
}

UiLayoutAlign parseHudAlign(std::string raw) {
    const std::string key = toLowerAscii(std::move(raw));
    if (key == "center") {
        return UiLayoutAlign::Center;
    }
    if (key == "end" || key == "right") {
        return UiLayoutAlign::End;
    }
    return UiLayoutAlign::Start;
}

UiLayoutJustify parseHudJustify(std::string raw) {
    const std::string key = toLowerAscii(std::move(raw));
    if (key == "center") {
        return UiLayoutJustify::Center;
    }
    if (key == "end" || key == "bottom") {
        return UiLayoutJustify::End;
    }
    if (key == "space_between") {
        return UiLayoutJustify::SpaceBetween;
    }
    return UiLayoutJustify::Start;
}

HudNotificationLevel parseLevel(std::string raw) {
    const std::string key = toLowerAscii(std::move(raw));
    if (key == "critical") {
        return HudNotificationLevel::Critical;
    }
    if (key == "action") {
        return HudNotificationLevel::Action;
    }
    return HudNotificationLevel::Info;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

} // namespace

HudNotificationsLayer::HudNotificationsLayer()
    : config_(defaultConfig_()) {}

HudNotificationsLayer::HudConfig HudNotificationsLayer::defaultConfig_() {
    HudConfig cfg{};

    cfg.defaults.level = HudNotificationLevel::Info;
    cfg.defaults.styleRef = "hud.base";
    cfg.defaults.enterRef = "fade_in";
    cfg.defaults.exitRef = "fade_out";
    cfg.defaults.lifetimeMs = 1300;
    cfg.defaults.holdMs = 900;

    HudStyleSpec base{};
    base.width = 24;
    base.height = 5;
    base.padding = 1;
    base.position = UiHudPosition::TopCenter;
    base.font = "gothic";
    base.fontSize = 0.0f;
    base.align = UiLayoutAlign::Center;
    base.justify = UiLayoutJustify::Center;
    base.textWrap = true;
    base.textColor = "#E3D4E8";
    base.borderColor = "#8F6E95";
    base.backgroundColor = "#130D16D8";
    cfg.styles.emplace("hud.base", base);

    HudStyleSpec accent = base;
    accent.borderColor = "#C081B8";
    accent.textColor = "#F1D9ED";
    cfg.styles.emplace("hud.accent", accent);

    HudStyleSpec critical = base;
    critical.borderColor = "#C81C63";
    critical.textColor = "#FFD8E5";
    critical.backgroundColor = "#1C0A12E0";
    cfg.styles.emplace("hud.critical", critical);

    HudAnimationSpec fadeIn{};
    fadeIn.type = "fade_scale_glow";
    fadeIn.durationMs = 190;
    fadeIn.fromOpacity = 0.0f;
    fadeIn.toOpacity = 1.0f;
    fadeIn.fromScale = 0.95f;
    fadeIn.toScale = 1.0f;
    fadeIn.fromGlow = 0.35f;
    fadeIn.toGlow = 0.12f;
    cfg.animations.emplace("fade_in", fadeIn);

    HudAnimationSpec fadeOut{};
    fadeOut.type = "fade_glitch";
    fadeOut.durationMs = 260;
    fadeOut.fromOpacity = 1.0f;
    fadeOut.toOpacity = 0.0f;
    fadeOut.fromScale = 1.0f;
    fadeOut.toScale = 1.0f;
    fadeOut.fromGlow = 0.12f;
    fadeOut.toGlow = 0.0f;
    fadeOut.glitchTo = 1.0f;
    cfg.animations.emplace("fade_out", fadeOut);

    HudTypeSpec info = cfg.defaults;
    info.level = HudNotificationLevel::Info;
    info.styleRef = "hud.base";
    cfg.types.emplace("info", info);

    HudTypeSpec action = cfg.defaults;
    action.level = HudNotificationLevel::Action;
    action.styleRef = "hud.accent";
    action.enterRef = "fade_in";
    action.exitRef = "fade_out";
    action.lifetimeMs = 1450;
    action.holdMs = 1000;
    cfg.types.emplace("action", action);

    HudTypeSpec crit = cfg.defaults;
    crit.level = HudNotificationLevel::Critical;
    crit.styleRef = "hud.critical";
    crit.lifetimeMs = 1900;
    crit.holdMs = 1300;
    cfg.types.emplace("critical", crit);

    cfg.events.emplace("snapshot.captured", HudEventSpec{.typeRef = "info", .textTemplate = "SNAPSHOT {slot} CAPTURED"});
    cfg.events.emplace("snapshot.applied", HudEventSpec{.typeRef = "action", .textTemplate = "SNAPSHOT {slot} APPLIED"});
    return cfg;
}

uint64_t HudNotificationsLayer::nowMs_() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string HudNotificationsLayer::eventKey_(HudEventId eventId) {
    switch (eventId) {
        case HudEventId::SnapshotCaptured: return "snapshot.captured";
        case HudEventId::SnapshotApplied: return "snapshot.applied";
        default: return "event.unknown";
    }
}

std::string HudNotificationsLayer::levelKey_(HudNotificationLevel level) {
    switch (level) {
        case HudNotificationLevel::Action: return "action";
        case HudNotificationLevel::Critical: return "critical";
        case HudNotificationLevel::Info:
        default:
            return "info";
    }
}

std::string HudNotificationsLayer::normalizeRef_(std::string ref,
                                                  const char* atPrefix,
                                                  const char* plainPrefix) {
    if (ref.rfind(atPrefix, 0) == 0U) {
        return ref.substr(std::char_traits<char>::length(atPrefix));
    }
    if (ref.rfind(plainPrefix, 0) == 0U) {
        return ref.substr(std::char_traits<char>::length(plainPrefix));
    }
    return ref;
}

std::string HudNotificationsLayer::applyPayload_(std::string templ,
                                                  const HudEventPayload& payload) {
    const auto replaceAll = [](std::string& s, std::string_view from, std::string_view to) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    if (!payload.text.empty()) {
        replaceAll(templ, "{text}", payload.text);
    }
    if (payload.slot > 0) {
        replaceAll(templ, "{slot}", std::to_string(payload.slot));
    }
    return templ;
}

bool HudNotificationsLayer::loadConfigFromFile(const std::string& path,
                                               std::string& errorOut) {
    const auto fail = [&](std::string msg) -> bool {
        if (errorOut.empty()) {
            errorOut = std::move(msg);
        }
        return false;
    };
    JsonValue root{};
    if (!parseJsonFile(std::filesystem::path(path), root, errorOut)) {
        return fail("hud.parse_json_failed");
    }
    JsonValue themesRoot{};
    if (!loadThemesCatalog(path, themesRoot, errorOut)) {
        return fail("hud.load_themes_failed");
    }
    const JsonValue* themes = (themesRoot.type == JsonValue::Type::Object) ? &themesRoot : nullptr;

    HudConfig next = defaultConfig_();

    if (const JsonValue* def = findField(root, "default"); def && def->type == JsonValue::Type::Object) {
        if (const JsonValue* styleRef = findField(*def, "style_ref"); styleRef && styleRef->type == JsonValue::Type::String) {
            next.defaults.styleRef = normalizeRef_(styleRef->stringValue, "@styles.", "styles.");
        }
        if (const JsonValue* enter = findField(*def, "enter"); enter && enter->type == JsonValue::Type::String) {
            next.defaults.enterRef = enter->stringValue;
        }
        if (const JsonValue* exit = findField(*def, "exit"); exit && exit->type == JsonValue::Type::String) {
            next.defaults.exitRef = exit->stringValue;
        }
        if (const JsonValue* life = findField(*def, "lifetime_ms")) {
            uint32_t v = 0;
            if (parseUint(*life, v)) next.defaults.lifetimeMs = std::max<uint32_t>(100U, v);
        }
        if (const JsonValue* hold = findField(*def, "hold_ms")) {
            uint32_t v = 0;
            if (parseUint(*hold, v)) next.defaults.holdMs = v;
        }
        if (const JsonValue* level = findField(*def, "level"); level && level->type == JsonValue::Type::String) {
            next.defaults.level = parseLevel(level->stringValue);
        }
    }

    if (const JsonValue* textAnims = findField(root, "text_animations");
        textAnims && textAnims->type == JsonValue::Type::Object) {
        for (const auto& [name, av] : textAnims->objectValue) {
            UiLayoutNode::EffectSpec fx{};
            if (!parseEffectSpec(av, fx, themes, &errorOut, std::string("text_animations.") + name)) {
                continue;
            }
            next.textAnimations[name] = std::move(fx);
        }
    }

    if (const JsonValue* styles = findField(root, "styles"); styles && styles->type == JsonValue::Type::Object) {
        for (const auto& [name, sv] : styles->objectValue) {
            if (sv.type != JsonValue::Type::Object) {
                continue;
            }
            HudStyleSpec s = next.styles.count(name) ? next.styles[name] : next.styles["hud.base"];
            auto resolveTheme = [&](const JsonValue* raw, std::string_view fieldPath) -> const JsonValue* {
                const JsonValue* resolved = resolveThemeTokenValue(themes, raw);
                if (!resolved && raw && raw->type == JsonValue::Type::String) {
                    errorOut = std::string("styles.") + name + "." + std::string(fieldPath) +
                               " unresolved theme token: " + raw->stringValue;
                }
                return resolved;
            };
            if (const JsonValue* width = findField(sv, "width")) {
                width = resolveTheme(width, "width");
                if (!width) return fail("hud.style.width_resolve_failed");
                uint32_t v = 0;
                if (parseUint(*width, v)) s.width = static_cast<uint16_t>(std::clamp<uint32_t>(v, 8U, 120U));
            }
            if (const JsonValue* height = findField(sv, "height")) {
                height = resolveTheme(height, "height");
                if (!height) return fail("hud.style.height_resolve_failed");
                uint32_t v = 0;
                if (parseUint(*height, v)) s.height = static_cast<uint16_t>(std::clamp<uint32_t>(v, 2U, 40U));
            }
            if (const JsonValue* padding = findField(sv, "padding")) {
                padding = resolveTheme(padding, "padding");
                if (!padding) return fail("hud.style.padding_resolve_failed");
                uint32_t v = 0;
                if (parseUint(*padding, v)) s.padding = static_cast<uint16_t>(std::clamp<uint32_t>(v, 0U, 10U));
            }
            if (const JsonValue* pos = findField(sv, "position"); pos && pos->type == JsonValue::Type::String) {
                s.position = parseHudPosition(pos->stringValue);
            }
            if (const JsonValue* fontRaw = findField(sv, "font")) {
                const JsonValue* font = resolveTheme(fontRaw, "font");
                if (!font) return fail("hud.style.font_resolve_failed");
                if (font->type == JsonValue::Type::String) {
                s.font = font->stringValue;
                }
            }
            if (const JsonValue* fontSizeRaw = findField(sv, "font_size")) {
                const JsonValue* fontSize = resolveTheme(fontSizeRaw, "font_size");
                if (!fontSize) return fail("hud.style.font_size_resolve_failed");
                float v = 0.0f;
                if (parseFloat(*fontSize, v) && std::isfinite(v)) {
                    s.fontSize = std::max(0.0f, v);
                }
            }
            if (const JsonValue* align = findField(sv, "align"); align && align->type == JsonValue::Type::String) {
                s.align = parseHudAlign(align->stringValue);
            }
            if (const JsonValue* justify = findField(sv, "justify"); justify && justify->type == JsonValue::Type::String) {
                s.justify = parseHudJustify(justify->stringValue);
            }
            if (const JsonValue* wrap = findField(sv, "text_wrap")) {
                if (wrap->type == JsonValue::Type::Bool) {
                    s.textWrap = wrap->boolValue;
                } else if (wrap->type == JsonValue::Type::String) {
                    const std::string val = toLowerAscii(wrap->stringValue);
                    if (val == "true" || val == "yes" || val == "1") {
                        s.textWrap = true;
                    } else if (val == "false" || val == "no" || val == "0") {
                        s.textWrap = false;
                    }
                }
            }
            if (const JsonValue* effects = findField(sv, "text_animations")) {
                s.textAnimationRefs.clear();
                if (effects->type == JsonValue::Type::String) {
                    s.textAnimationRefs.push_back(
                        normalizeRef_(effects->stringValue, "@text_animations.", "text_animations."));
                } else if (effects->type == JsonValue::Type::Array) {
                    for (const JsonValue& item : effects->arrayValue) {
                        if (item.type != JsonValue::Type::String) {
                            continue;
                        }
                        s.textAnimationRefs.push_back(
                            normalizeRef_(item.stringValue, "@text_animations.", "text_animations."));
                    }
                }
            }
            if (const JsonValue* tc = findField(sv, "text_color"); tc && tc->type == JsonValue::Type::String) {
                tc = resolveTheme(tc, "text_color");
                if (!tc) return fail("hud.style.text_color_resolve_failed");
                s.textColor = tc->stringValue;
            }
            if (const JsonValue* bc = findField(sv, "border_color"); bc && bc->type == JsonValue::Type::String) {
                bc = resolveTheme(bc, "border_color");
                if (!bc) return fail("hud.style.border_color_resolve_failed");
                s.borderColor = bc->stringValue;
            }
            if (const JsonValue* bg = findField(sv, "background_color"); bg && bg->type == JsonValue::Type::String) {
                bg = resolveTheme(bg, "background_color");
                if (!bg) return fail("hud.style.background_color_resolve_failed");
                s.backgroundColor = bg->stringValue;
            }
            next.styles[name] = std::move(s);
        }
    }

    if (const JsonValue* anims = findField(root, "animations"); anims && anims->type == JsonValue::Type::Object) {
        for (const auto& [name, av] : anims->objectValue) {
            if (av.type != JsonValue::Type::Object) {
                continue;
            }
            HudAnimationSpec a = next.animations.count(name) ? next.animations[name] : HudAnimationSpec{};
            if (const JsonValue* type = findField(av, "type"); type && type->type == JsonValue::Type::String) {
                a.type = toLowerAscii(type->stringValue);
            }
            if (const JsonValue* d = findField(av, "duration_ms")) {
                uint32_t v = 0;
                if (parseUint(*d, v)) a.durationMs = std::max<uint32_t>(10U, v);
            }
            if (const JsonValue* v = findField(av, "from_opacity")) parseFloat(*v, a.fromOpacity);
            if (const JsonValue* v = findField(av, "to_opacity")) parseFloat(*v, a.toOpacity);
            if (const JsonValue* v = findField(av, "from_scale")) parseFloat(*v, a.fromScale);
            if (const JsonValue* v = findField(av, "to_scale")) parseFloat(*v, a.toScale);
            if (const JsonValue* v = findField(av, "from_glow")) parseFloat(*v, a.fromGlow);
            if (const JsonValue* v = findField(av, "to_glow")) parseFloat(*v, a.toGlow);
            if (const JsonValue* v = findField(av, "glitch_to")) parseFloat(*v, a.glitchTo);
            next.animations[name] = std::move(a);
        }
    }

    if (const JsonValue* types = findField(root, "types"); types && types->type == JsonValue::Type::Object) {
        for (const auto& [name, tv] : types->objectValue) {
            if (tv.type != JsonValue::Type::Object) {
                continue;
            }
            HudTypeSpec t = next.defaults;
            if (const JsonValue* level = findField(tv, "level"); level && level->type == JsonValue::Type::String) {
                t.level = parseLevel(level->stringValue);
            } else {
                t.level = parseLevel(name);
            }
            if (const JsonValue* styleRef = findField(tv, "style_ref"); styleRef && styleRef->type == JsonValue::Type::String) {
                t.styleRef = normalizeRef_(styleRef->stringValue, "@styles.", "styles.");
            }
            if (const JsonValue* enter = findField(tv, "enter"); enter && enter->type == JsonValue::Type::String) {
                t.enterRef = enter->stringValue;
            }
            if (const JsonValue* exit = findField(tv, "exit"); exit && exit->type == JsonValue::Type::String) {
                t.exitRef = exit->stringValue;
            }
            if (const JsonValue* life = findField(tv, "lifetime_ms")) {
                uint32_t v = 0;
                if (parseUint(*life, v)) t.lifetimeMs = std::max<uint32_t>(100U, v);
            }
            if (const JsonValue* hold = findField(tv, "hold_ms")) {
                uint32_t v = 0;
                if (parseUint(*hold, v)) t.holdMs = v;
            }
            next.types[name] = std::move(t);
        }
    }

    if (const JsonValue* events = findField(root, "events"); events && events->type == JsonValue::Type::Object) {
        for (const auto& [name, ev] : events->objectValue) {
            if (ev.type != JsonValue::Type::Object) {
                continue;
            }
            HudEventSpec e = next.events.count(name) ? next.events[name] : HudEventSpec{};
            if (const JsonValue* type = findField(ev, "type"); type && type->type == JsonValue::Type::String) {
                e.typeRef = type->stringValue;
            }
            if (const JsonValue* text = findField(ev, "text"); text && text->type == JsonValue::Type::String) {
                e.textTemplate = text->stringValue;
            }
            next.events[name] = std::move(e);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(next);
    queue_.clear();
    active_.reset();
    return true;
}

void HudNotificationsLayer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    active_.reset();
}

HudNotificationsLayer::RuntimeItem HudNotificationsLayer::buildRuntimeItemFromEvent_(
    std::string eventKey,
    const HudEventPayload& payload,
    uint64_t nowMs) const {
    const auto eventIt = config_.events.find(eventKey);
    const HudEventSpec eventSpec = (eventIt != config_.events.end())
                                       ? eventIt->second
                                       : HudEventSpec{.typeRef = "info", .textTemplate = eventKey};

    const auto typeIt = config_.types.find(eventSpec.typeRef);
    const HudTypeSpec type = (typeIt != config_.types.end()) ? typeIt->second : config_.defaults;

    const auto styleIt = config_.styles.find(type.styleRef);
    const HudStyleSpec style = (styleIt != config_.styles.end())
                                   ? styleIt->second
                                   : config_.styles.at("hud.base");

    RuntimeItem item{};
    item.startMs = nowMs;
    item.baseView.visible = true;
    item.baseView.position = style.position;
    item.baseView.text = applyPayload_(eventSpec.textTemplate, payload);
    item.baseView.width = style.width;
    item.baseView.height = style.height;
    item.baseView.padding = style.padding;
    item.baseView.font = style.font;
    item.baseView.fontSize = style.fontSize;
    item.baseView.align = style.align;
    item.baseView.justify = style.justify;
    item.baseView.textWrap = style.textWrap;
    item.baseView.textEffects.clear();
    for (const std::string& ref : style.textAnimationRefs) {
        const auto it = config_.textAnimations.find(ref);
        if (it == config_.textAnimations.end()) {
            continue;
        }
        item.baseView.textEffects.push_back(it->second);
    }
    item.baseView.textColor = style.textColor;
    item.baseView.borderColor = style.borderColor;
    item.baseView.backgroundColor = style.backgroundColor;
    item.baseView.fxInstanceId = nowMs;

    item.enterAnim = type.enterRef;
    item.exitAnim = type.exitRef;
    item.holdMs = type.holdMs;

    if (const auto it = config_.animations.find(item.enterAnim); it != config_.animations.end()) {
        item.enterMs = it->second.durationMs;
    }
    if (const auto it = config_.animations.find(item.exitAnim); it != config_.animations.end()) {
        item.exitMs = it->second.durationMs;
    }

    const uint32_t baseLifetime = std::max<uint32_t>(type.lifetimeMs, item.enterMs + item.exitMs + 40U);
    const uint32_t maxHoldByLife = (baseLifetime > (item.enterMs + item.exitMs))
                                       ? (baseLifetime - item.enterMs - item.exitMs)
                                       : 0U;
    item.holdMs = std::min<uint32_t>(item.holdMs, maxHoldByLife);
    return item;
}

HudNotificationsLayer::RuntimeItem HudNotificationsLayer::buildRuntimeItemFromText_(
    std::string text,
    HudNotificationLevel level,
    uint64_t nowMs) const {
    const std::string typeKey = levelKey_(level);
    HudEventPayload p{};
    p.text = std::move(text);
    RuntimeItem item = buildRuntimeItemFromEvent_("custom." + typeKey, p, nowMs);
    // Если такого события нет в events-каталоге, используем прямой текст.
    if (item.baseView.text.rfind("custom.", 0) == 0U || item.baseView.text.empty()) {
        item.baseView.text = p.text;
    }
    return item;
}

void HudNotificationsLayer::notify(HudEventId eventId, const HudEventPayload& payload) {
    const uint64_t now = nowMs_();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(buildRuntimeItemFromEvent_(eventKey_(eventId), payload, now));
}

void HudNotificationsLayer::notifyText(std::string text, HudNotificationLevel level) {
    const uint64_t now = nowMs_();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(buildRuntimeItemFromText_(std::move(text), level, now));
}

UiHudOverlayView HudNotificationsLayer::evaluate_(const RuntimeItem& item,
                                                  uint64_t nowMs,
                                                  bool& finished) const {
    UiHudOverlayView out = item.baseView;
    out.visible = true;
    out.opacity = 1.0f;
    out.scale = 1.0f;
    out.glow01 = 0.0f;
    out.glitch01 = 0.0f;

    const uint64_t total = static_cast<uint64_t>(item.enterMs) + item.holdMs + item.exitMs;
    const uint64_t elapsed = (nowMs > item.startMs) ? (nowMs - item.startMs) : 0U;
    if (elapsed >= total) {
        finished = true;
        out.visible = false;
        return out;
    }

    const auto resolveAnim = [&](const std::string& key) -> HudAnimationSpec {
        const auto it = config_.animations.find(key);
        if (it != config_.animations.end()) {
            return it->second;
        }
        return HudAnimationSpec{};
    };

    const HudAnimationSpec enter = resolveAnim(item.enterAnim);
    const HudAnimationSpec exit = resolveAnim(item.exitAnim);

    auto applyAnim = [&](const HudAnimationSpec& a, float t) {
        out.opacity = std::clamp(lerp(a.fromOpacity, a.toOpacity, t), 0.0f, 1.0f);
        out.scale = std::clamp(lerp(a.fromScale, a.toScale, t), 0.80f, 1.20f);
        out.glow01 = std::clamp(lerp(a.fromGlow, a.toGlow, t), 0.0f, 1.0f);
        out.glitch01 = std::clamp(a.glitchTo * t, 0.0f, 1.0f);
    };

    if (elapsed < item.enterMs) {
        const float t = (item.enterMs > 0U)
                            ? static_cast<float>(elapsed) / static_cast<float>(item.enterMs)
                            : 1.0f;
        applyAnim(enter, t);
        return out;
    }

    if (elapsed < (static_cast<uint64_t>(item.enterMs) + item.holdMs)) {
        out.opacity = std::clamp(enter.toOpacity, 0.0f, 1.0f);
        out.scale = std::clamp(enter.toScale, 0.80f, 1.20f);
        const float holdT = (item.holdMs > 0U)
                                ? static_cast<float>(elapsed - item.enterMs) / static_cast<float>(item.holdMs)
                                : 0.0f;
        const float breath = 0.06f * std::sin(holdT * 6.283185f * 1.5f);
        out.glow01 = std::clamp(enter.toGlow + breath, 0.0f, 1.0f);
        out.glitch01 = 0.0f;
        return out;
    }

    const uint64_t exitElapsed = elapsed - item.enterMs - item.holdMs;
    const float t = (item.exitMs > 0U)
                        ? static_cast<float>(exitElapsed) / static_cast<float>(item.exitMs)
                        : 1.0f;
    applyAnim(exit, t);
    if (toLowerAscii(exit.type) == "fade_out") {
        out.glitch01 = 0.0f;
    }
    return out;
}

UiHudOverlayView HudNotificationsLayer::view(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    while (true) {
        if (!active_.has_value()) {
            if (queue_.empty()) {
                return {};
            }
            active_ = queue_.front();
            queue_.pop_front();
            if (active_->startMs == 0U) {
                active_->startMs = nowMs;
            }
        }

        bool finished = false;
        UiHudOverlayView out = evaluate_(*active_, nowMs, finished);
        if (finished) {
            active_.reset();
            continue;
        }
        return out;
    }
}

} // namespace avantgarde
