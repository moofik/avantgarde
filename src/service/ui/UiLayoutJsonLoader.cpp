#include "service/ui/UiLayoutJsonLoader.h"
#include "service/ui/UiTokenResolver.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
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
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parseHex4_(cp)) {
                            return false;
                        }
                        if (cp <= 0x7FU) {
                            out.push_back(static_cast<char>(cp));
                        } else {
                            // Для layout-конфигов достаточно базового набора символов;
                            // неподдерживаемые unicode-коды заменяем маркером.
                            out.push_back('?');
                        }
                    } break;
                    default:
                        return setError_("unsupported escape sequence");
                }
                continue;
            }
            out.push_back(ch);
        }
        return setError_("unterminated string");
    }

    bool parseHex4_(uint32_t& out) {
        if (pos_ + 4U > src_.size()) {
            return setError_("invalid unicode escape");
        }
        out = 0U;
        for (int i = 0; i < 4; ++i) {
            const char ch = src_[pos_++];
            out <<= 4U;
            if (ch >= '0' && ch <= '9') out |= static_cast<uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') out |= static_cast<uint32_t>(10 + ch - 'a');
            else if (ch >= 'A' && ch <= 'F') out |= static_cast<uint32_t>(10 + ch - 'A');
            else return setError_("invalid unicode escape");
        }
        return true;
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
        } else if (peek_() >= '1' && peek_() <= '9') {
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_())) != 0) {
                ++pos_;
            }
        } else {
            return setError_("invalid number");
        }

        if (!eof_() && peek_() == '.') {
            ++pos_;
            if (eof_() || std::isdigit(static_cast<unsigned char>(peek_())) == 0) {
                return setError_("invalid number");
            }
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_())) != 0) {
                ++pos_;
            }
        }

        if (!eof_() && (peek_() == 'e' || peek_() == 'E')) {
            ++pos_;
            if (!eof_() && (peek_() == '+' || peek_() == '-')) {
                ++pos_;
            }
            if (eof_() || std::isdigit(static_cast<unsigned char>(peek_())) == 0) {
                return setError_("invalid number");
            }
            while (!eof_() && std::isdigit(static_cast<unsigned char>(peek_())) != 0) {
                ++pos_;
            }
        }

        const std::string token(src_.substr(begin, pos_ - begin));
        char* end = nullptr;
        const double parsed = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
            return setError_("invalid number");
        }

        out = JsonValue{};
        out.type = JsonValue::Type::Number;
        out.numberValue = parsed;
        return true;
    }

    bool parseLiteral_(std::string_view keyword,
                       JsonValue::Type type,
                       JsonValue& out,
                       bool boolValue) {
        if (src_.substr(pos_, keyword.size()) != keyword) {
            return setError_("invalid literal");
        }
        pos_ += keyword.size();
        out = JsonValue{};
        out.type = type;
        out.boolValue = boolValue;
        return true;
    }

    void skipWs_() {
        while (!eof_() &&
               std::isspace(static_cast<unsigned char>(src_[pos_])) != 0) {
            ++pos_;
        }
    }

    char peek_() const {
        return eof_() ? '\0' : src_[pos_];
    }

    char advance_() {
        return eof_() ? '\0' : src_[pos_++];
    }

    bool consume_(char expected) {
        if (eof_() || src_[pos_] != expected) {
            return setError_(std::string("expected '") + expected + "'");
        }
        ++pos_;
        return true;
    }

    bool consumeIf_(char ch) {
        if (!eof_() && src_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool eof_() const {
        return pos_ >= src_.size();
    }

    bool setError_(std::string msg) {
        if (error_.empty()) {
            error_ = std::move(msg);
        }
        return false;
    }

    std::string errorMessage_() const {
        std::ostringstream ss;
        ss << "json parse error at pos " << pos_ << ": " << (error_.empty() ? "unknown error" : error_);
        return ss.str();
    }

private:
    std::string_view src_{};
    std::size_t pos_{0};
    std::string error_{};
};

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parseFloatToken(std::string_view raw, float& out) {
    const std::string token(raw);
    char* end = nullptr;
    const float parsed = std::strtof(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parseDurationMs(std::string_view raw, uint32_t& out) {
    std::string token(raw);
    token = toLowerAscii(token);
    if (token.empty()) {
        return false;
    }
    float value = 0.0f;
    float mul = 1.0f;
    if (token.size() > 2U && token.substr(token.size() - 2U) == "ms") {
        if (!parseFloatToken(std::string_view(token.data(), token.size() - 2U), value)) {
            return false;
        }
    } else if (!token.empty() && token.back() == 's') {
        if (!parseFloatToken(std::string_view(token.data(), token.size() - 1U), value)) {
            return false;
        }
        mul = 1000.0f;
    } else {
        if (!parseFloatToken(token, value)) {
            return false;
        }
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

bool parseSizeFromString(std::string_view raw, UiLayoutSize& out) {
    std::string value = toLowerAscii(std::string(raw));
    if (value == "auto") {
        out.unit = UiLayoutSize::Unit::Auto;
        out.value = 0.0f;
        return true;
    }
    if (!value.empty() && value.back() == '%') {
        float percent = 0.0f;
        if (!parseFloatToken(std::string_view(value.data(), value.size() - 1U), percent)) {
            return false;
        }
        out.unit = UiLayoutSize::Unit::Percent;
        out.value = percent;
        return true;
    }
    auto parsePxLike = [&](std::string_view suffix) -> bool {
        if (value.size() <= suffix.size()) {
            return false;
        }
        if (value.rfind(suffix) != value.size() - suffix.size()) {
            return false;
        }
        float px = 0.0f;
        if (!parseFloatToken(std::string_view(value.data(), value.size() - suffix.size()), px)) {
            return false;
        }
        out.unit = UiLayoutSize::Unit::Px;
        out.value = px;
        return true;
    };
    // Внутренние единицы layout — grid-cell.
    // Для понятности поддерживаем явные синонимы:
    // - px, cell/cells, ch, row/rows, col/cols.
    if (parsePxLike("px") || parsePxLike("cells") || parsePxLike("cell") || parsePxLike("rows") ||
        parsePxLike("row") || parsePxLike("cols") || parsePxLike("col") || parsePxLike("ch")) {
        return true;
    }
    float px = 0.0f;
    if (!parseFloatToken(value, px)) {
        return false;
    }
    out.unit = UiLayoutSize::Unit::Px;
    out.value = px;
    return true;
}

bool parseSize(const JsonValue& value, UiLayoutSize& out) {
    if (value.type == JsonValue::Type::String) {
        return parseSizeFromString(value.stringValue, out);
    }
    if (value.type == JsonValue::Type::Number) {
        if (!std::isfinite(value.numberValue)) {
            return false;
        }
        out.unit = UiLayoutSize::Unit::Px;
        out.value = static_cast<float>(value.numberValue);
        return true;
    }
    return false;
}

bool parseUint16Number(const JsonValue& value, uint16_t& out) {
    if (value.type != JsonValue::Type::Number || !std::isfinite(value.numberValue)) {
        return false;
    }
    const double rounded = std::llround(value.numberValue);
    if (std::fabs(value.numberValue - rounded) > 0.000001) {
        return false;
    }
    if (rounded < 0.0 || rounded > 65535.0) {
        return false;
    }
    out = static_cast<uint16_t>(rounded);
    return true;
}

bool parseFloatNumber(const JsonValue& value, float& out) {
    if (value.type != JsonValue::Type::Number || !std::isfinite(value.numberValue)) {
        return false;
    }
    out = static_cast<float>(value.numberValue);
    return std::isfinite(out);
}

bool parseBool(const JsonValue& value, bool& out) {
    if (value.type != JsonValue::Type::Bool) {
        return false;
    }
    out = value.boolValue;
    return true;
}

bool parseJustify(const JsonValue& value, UiLayoutJustify& out) {
    if (value.type != JsonValue::Type::String) {
        return false;
    }
    std::string token = toLowerAscii(value.stringValue);
    if (token == "start" || token == "left" || token == "top") {
        out = UiLayoutJustify::Start;
        return true;
    }
    if (token == "center" || token == "middle") {
        out = UiLayoutJustify::Center;
        return true;
    }
    if (token == "end" || token == "right" || token == "bottom") {
        out = UiLayoutJustify::End;
        return true;
    }
    if (token == "space_between" || token == "space-between") {
        out = UiLayoutJustify::SpaceBetween;
        return true;
    }
    return false;
}

bool parseAlign(const JsonValue& value, UiLayoutAlign& out) {
    if (value.type != JsonValue::Type::String) {
        return false;
    }
    std::string token = toLowerAscii(value.stringValue);
    if (token == "start" || token == "left" || token == "top") {
        out = UiLayoutAlign::Start;
        return true;
    }
    if (token == "center" || token == "middle") {
        out = UiLayoutAlign::Center;
        return true;
    }
    if (token == "end" || token == "right" || token == "bottom") {
        out = UiLayoutAlign::End;
        return true;
    }
    return false;
}

const JsonValue* findObjectField(const JsonValue& object, std::string_view key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const auto& [name, value] : object.objectValue) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

struct ParseContext {
    const JsonValue* stylesRoot{nullptr};
    const JsonValue* themeRoot{nullptr};
    const JsonValue* effectsRoot{nullptr};
    const JsonValue* animationsRoot{nullptr};
    std::vector<std::string> styleStack{};
    std::vector<std::string> effectPresetStack{};
    std::vector<std::string> animationPresetStack{};
};

std::string normalizeStyleRef(std::string_view raw) {
    std::string ref = toLowerAscii(std::string(raw));
    if (ref.rfind("@styles.", 0) == 0U) {
        return ref.substr(std::string("@styles.").size());
    }
    if (ref.rfind("styles.", 0) == 0U) {
        return ref.substr(std::string("styles.").size());
    }
    return ref;
}

std::string normalizeEffectPresetRef(std::string_view raw) {
    std::string ref = toLowerAscii(std::string(raw));
    if (ref.rfind("@effects.", 0) == 0U) {
        return ref.substr(std::string("@effects.").size());
    }
    if (ref.rfind("effects.", 0) == 0U) {
        return ref.substr(std::string("effects.").size());
    }
    return ref;
}

std::string normalizeAnimationPresetRef(std::string_view raw) {
    std::string ref = toLowerAscii(std::string(raw));
    if (ref.rfind("@animations.", 0) == 0U) {
        return ref.substr(std::string("@animations.").size());
    }
    if (ref.rfind("animations.", 0) == 0U) {
        return ref.substr(std::string("animations.").size());
    }
    return ref;
}

const JsonValue* findObjectFieldCaseInsensitive(const JsonValue& object, std::string_view keyLower) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const auto& [name, value] : object.objectValue) {
        if (toLowerAscii(name) == keyLower) {
            return &value;
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
        if (const JsonValue* exact = findObjectFieldCaseInsensitive(*current, remain)) {
            return exact;
        }

        const std::size_t dotPos = remain.find('.');
        if (dotPos == std::string::npos) {
            return findObjectFieldCaseInsensitive(*current, remain);
        }
        const std::string head = remain.substr(0U, dotPos);
        const JsonValue* next = findObjectFieldCaseInsensitive(*current, head);
        if (!next) {
            return nullptr;
        }
        current = next;
        remain = remain.substr(dotPos + 1U);
    }
}

const JsonValue* resolveThemeToken(const ParseContext* context, std::string_view raw) {
    if (!context || !context->themeRoot) {
        return nullptr;
    }
    const auto keyOpt = UiTokenResolver::normalizeThemeRef(raw);
    if (!keyOpt) {
        return nullptr;
    }
    const std::string& key = *keyOpt;
    if (key.empty()) {
        return nullptr;
    }
    return findDottedNode(*context->themeRoot, key);
}

bool looksLikeThemeToken(std::string_view raw) {
    return UiTokenResolver::normalizeThemeRef(raw).has_value();
}

const JsonValue* findStyleNode(const JsonValue& stylesRoot, std::string_view rawRef) {
    if (stylesRoot.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const std::string key = normalizeStyleRef(rawRef);
    if (key.empty()) {
        return nullptr;
    }
    for (const auto& [name, value] : stylesRoot.objectValue) {
        if (toLowerAscii(name) == key) {
            return &value;
        }
    }
    return nullptr;
}

const JsonValue* findEffectPresetNode(const JsonValue& effectsRoot, std::string_view rawRef) {
    if (effectsRoot.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const std::string key = normalizeEffectPresetRef(rawRef);
    if (key.empty()) {
        return nullptr;
    }
    return findDottedNode(effectsRoot, key);
}

const JsonValue* findAnimationPresetNode(const JsonValue& animationsRoot, std::string_view rawRef) {
    if (animationsRoot.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const std::string key = normalizeAnimationPresetRef(rawRef);
    if (key.empty()) {
        return nullptr;
    }
    return findDottedNode(animationsRoot, key);
}

void mergeStyleObjectMap(JsonValue& dstStyles, const JsonValue& srcStyles) {
    if (dstStyles.type != JsonValue::Type::Object || srcStyles.type != JsonValue::Type::Object) {
        return;
    }
    for (const auto& [srcKey, srcVal] : srcStyles.objectValue) {
        bool replaced = false;
        for (auto& [dstKey, dstVal] : dstStyles.objectValue) {
            if (toLowerAscii(dstKey) == toLowerAscii(srcKey)) {
                dstVal = srcVal;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            dstStyles.objectValue.emplace_back(srcKey, srcVal);
        }
    }
}

bool parseNode(const JsonValue& nodeValue,
               UiLayoutNode& out,
               std::string& errorOut,
               std::string path,
               ParseContext* context);

bool parseChildren(const JsonValue& value,
                   UiLayoutNode& out,
                   std::string& errorOut,
                   const std::string& path,
                   ParseContext* context) {
    if (value.type != JsonValue::Type::Array) {
        errorOut = path + ".children must be array";
        return false;
    }
    out.children.clear();
    out.children.reserve(value.arrayValue.size());
    for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
        UiLayoutNode child{};
        if (!parseNode(value.arrayValue[i],
                       child,
                       errorOut,
                       path + ".children[" + std::to_string(i) + "]",
                       context)) {
            return false;
        }
        out.children.push_back(std::move(child));
    }
    return true;
}

bool applyEffectSpecObject(const JsonValue& effectObject,
                           UiLayoutNode::EffectSpec& fx,
                           bool& hasType,
                           ParseContext* context,
                           const std::string& path,
                           std::string& errorOut) {
    if (effectObject.type != JsonValue::Type::Object) {
        errorOut = path + " must be object";
        return false;
    }

    if (const JsonValue* preset = findObjectField(effectObject, "preset")) {
        if (preset->type != JsonValue::Type::String || preset->stringValue.empty()) {
            errorOut = path + ".preset expects non-empty string";
            return false;
        }
        if (!context || !context->effectsRoot) {
            errorOut = path + ".preset requires effects catalog";
            return false;
        }
        const std::string presetKey = normalizeEffectPresetRef(preset->stringValue);
        if (presetKey.empty()) {
            errorOut = path + ".preset contains empty reference";
            return false;
        }
        if (std::find(context->effectPresetStack.begin(), context->effectPresetStack.end(), presetKey) !=
            context->effectPresetStack.end()) {
            errorOut = path + ".preset cyclic reference: " + presetKey;
            return false;
        }
        const JsonValue* presetNode = findEffectPresetNode(*context->effectsRoot, preset->stringValue);
        if (!presetNode) {
            errorOut = path + ".preset unresolved: " + preset->stringValue;
            return false;
        }
        context->effectPresetStack.push_back(presetKey);
        const bool ok = applyEffectSpecObject(*presetNode,
                                              fx,
                                              hasType,
                                              context,
                                              path + ".preset(" + preset->stringValue + ")",
                                              errorOut);
        context->effectPresetStack.pop_back();
        if (!ok) {
            return false;
        }
    }

    for (const auto& [fxKey, rawFxVal] : effectObject.objectValue) {
        if (fxKey == "preset") {
            continue;
        }
        const JsonValue* fxValPtr = &rawFxVal;
        if (rawFxVal.type == JsonValue::Type::String) {
            if (const JsonValue* themed = resolveThemeToken(context, rawFxVal.stringValue)) {
                fxValPtr = themed;
            } else if (looksLikeThemeToken(rawFxVal.stringValue)) {
                errorOut = path + "." + fxKey + " unresolved theme token: " + rawFxVal.stringValue;
                return false;
            }
        }
        const JsonValue& fxVal = *fxValPtr;

        if (fxKey == "type") {
            if (fxVal.type != JsonValue::Type::String || fxVal.stringValue.empty()) {
                errorOut = path + ".type expects non-empty string";
                return false;
            }
            fx.type = fxVal.stringValue;
            hasType = true;
            continue;
        }
        if (fxKey == "effect_trigger") {
            if (fxVal.type != JsonValue::Type::String) {
                errorOut = path + ".effect_trigger expects string";
                return false;
            }
            fx.effectTrigger = fxVal.stringValue;
            continue;
        }
        if (fxKey == "effect_color") {
            if (fxVal.type != JsonValue::Type::String) {
                errorOut = path + ".effect_color expects string";
                return false;
            }
            fx.effectColor = fxVal.stringValue;
            continue;
        }
        if (fxKey == "effect_transition") {
            if (fxVal.type != JsonValue::Type::String) {
                errorOut = path + ".effect_transition expects string";
                return false;
            }
            fx.effectTransition = fxVal.stringValue;
            continue;
        }
        if (fxKey == "effect_trigger_out") {
            uint32_t outMs = 0;
            if (fxVal.type == JsonValue::Type::Number) {
                const double rounded = std::llround(fxVal.numberValue);
                if (std::fabs(fxVal.numberValue - rounded) > 0.000001 || rounded < 0.0 ||
                    rounded > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
                    errorOut = path + ".effect_trigger_out expects duration number/string";
                    return false;
                }
                outMs = static_cast<uint32_t>(rounded);
            } else if (fxVal.type == JsonValue::Type::String) {
                if (!parseDurationMs(fxVal.stringValue, outMs)) {
                    errorOut = path + ".effect_trigger_out expects duration (e.g. \"1s\", \"750ms\", 1000)";
                    return false;
                }
            } else {
                errorOut = path + ".effect_trigger_out expects duration number/string";
                return false;
            }
            fx.effectTriggerOutMs = outMs;
            continue;
        }
        if (fxKey == "effect_interval_ms") {
            uint16_t ms = 0;
            if (!parseUint16Number(fxVal, ms)) {
                errorOut = path + ".effect_interval_ms expects integer";
                return false;
            }
            fx.effectIntervalMs = ms;
            continue;
        }
        if (fxKey == "effect_amount") {
            float amount = 0.0f;
            if (!parseFloatNumber(fxVal, amount)) {
                errorOut = path + ".effect_amount expects number";
                return false;
            }
            fx.effectAmount = amount;
            continue;
        }
        if (fxKey == "effect_speed") {
            float speed = 0.0f;
            if (!parseFloatNumber(fxVal, speed)) {
                errorOut = path + ".effect_speed expects number";
                return false;
            }
            fx.effectSpeed = speed;
            continue;
        }
        errorOut = path + ": unsupported key '" + fxKey + "'";
        return false;
    }
    return true;
}

bool applyAnimationSpecObject(const JsonValue& animObject,
                              UiLayoutNode& outNode,
                              ParseContext* context,
                              const std::string& path,
                              std::string& errorOut) {
    if (animObject.type != JsonValue::Type::Object) {
        errorOut = path + " must be object";
        return false;
    }

    if (const JsonValue* preset = findObjectField(animObject, "preset")) {
        if (preset->type != JsonValue::Type::String || preset->stringValue.empty()) {
            errorOut = path + ".preset expects non-empty string";
            return false;
        }
        if (!context || !context->animationsRoot) {
            errorOut = path + ".preset requires animations catalog";
            return false;
        }
        const std::string presetKey = normalizeAnimationPresetRef(preset->stringValue);
        if (presetKey.empty()) {
            errorOut = path + ".preset contains empty reference";
            return false;
        }
        if (std::find(context->animationPresetStack.begin(), context->animationPresetStack.end(), presetKey) !=
            context->animationPresetStack.end()) {
            errorOut = path + ".preset cyclic reference: " + presetKey;
            return false;
        }
        const JsonValue* presetNode = findAnimationPresetNode(*context->animationsRoot, preset->stringValue);
        if (!presetNode) {
            errorOut = path + ".preset unresolved: " + preset->stringValue;
            return false;
        }
        context->animationPresetStack.push_back(presetKey);
        const bool ok = applyAnimationSpecObject(*presetNode,
                                                 outNode,
                                                 context,
                                                 path + ".preset(" + preset->stringValue + ")",
                                                 errorOut);
        context->animationPresetStack.pop_back();
        if (!ok) {
            return false;
        }
    }

    for (const auto& [animKey, rawAnimVal] : animObject.objectValue) {
        if (animKey == "preset") {
            continue;
        }
        const JsonValue* animValPtr = &rawAnimVal;
        if (rawAnimVal.type == JsonValue::Type::String) {
            if (const JsonValue* themed = resolveThemeToken(context, rawAnimVal.stringValue)) {
                animValPtr = themed;
            } else if (looksLikeThemeToken(rawAnimVal.stringValue)) {
                errorOut = path + "." + animKey + " unresolved theme token: " + rawAnimVal.stringValue;
                return false;
            }
        }
        const JsonValue& animVal = *animValPtr;

        if (animKey == "mode") {
            if (animVal.type != JsonValue::Type::String || animVal.stringValue.empty()) {
                errorOut = path + ".mode expects non-empty string";
                return false;
            }
            const std::string mode = toLowerAscii(animVal.stringValue);
            if (mode != "loop" && mode != "scrub") {
                errorOut = path + ".mode expects loop|scrub";
                return false;
            }
            outNode.animMode = mode;
            continue;
        }
        if (animKey == "fps") {
            float fps = 0.0f;
            if (!parseFloatNumber(animVal, fps)) {
                errorOut = path + ".fps expects number";
                return false;
            }
            if (!std::isfinite(fps) || fps <= 0.0f) {
                errorOut = path + ".fps must be > 0";
                return false;
            }
            outNode.animFps = fps;
            continue;
        }
        if (animKey == "frames") {
            if (animVal.type != JsonValue::Type::Array) {
                errorOut = path + ".frames expects array";
                return false;
            }
            outNode.animFrames.clear();
            outNode.animFrames.reserve(animVal.arrayValue.size());
            for (std::size_t i = 0; i < animVal.arrayValue.size(); ++i) {
                const JsonValue& frame = animVal.arrayValue[i];
                if (frame.type != JsonValue::Type::String || frame.stringValue.empty()) {
                    errorOut = path + ".frames[" + std::to_string(i) + "] expects non-empty string";
                    return false;
                }
                outNode.animFrames.push_back(frame.stringValue);
            }
            continue;
        }
        errorOut = path + ": unsupported key '" + animKey + "'";
        return false;
    }

    return true;
}

bool parseNode(const JsonValue& nodeValue,
               UiLayoutNode& out,
               std::string& errorOut,
               std::string path,
               ParseContext* context) {
    if (nodeValue.type != JsonValue::Type::Object) {
        errorOut = path + " must be object";
        return false;
    }

    // style_ref разворачиваем перед локальными полями ноды.
    // Приоритет: локальные поля ноды выше style_ref.
    if (const JsonValue* styleRef = findObjectField(nodeValue, "style_ref")) {
        if (!context || !context->stylesRoot) {
            errorOut = path + ".style_ref requires styles catalog";
            return false;
        }
        std::vector<std::string> refs{};
        if (styleRef->type == JsonValue::Type::String) {
            refs.push_back(styleRef->stringValue);
        } else if (styleRef->type == JsonValue::Type::Array) {
            refs.reserve(styleRef->arrayValue.size());
            for (const JsonValue& item : styleRef->arrayValue) {
                if (item.type != JsonValue::Type::String || item.stringValue.empty()) {
                    errorOut = path + ".style_ref expects string or string[]";
                    return false;
                }
                refs.push_back(item.stringValue);
            }
        } else {
            errorOut = path + ".style_ref expects string or string[]";
            return false;
        }

        for (const std::string& rawRef : refs) {
            const std::string key = normalizeStyleRef(rawRef);
            if (key.empty()) {
                errorOut = path + ".style_ref contains empty reference";
                return false;
            }
            if (std::find(context->styleStack.begin(), context->styleStack.end(), key) != context->styleStack.end()) {
                errorOut = path + ".style_ref cyclic reference: " + key;
                return false;
            }
            const JsonValue* styleNode = findStyleNode(*context->stylesRoot, key);
            if (!styleNode) {
                errorOut = path + ".style_ref unresolved: " + rawRef;
                return false;
            }
            if (styleNode->type != JsonValue::Type::Object) {
                errorOut = path + ".style_ref points to non-object style: " + rawRef;
                return false;
            }
            if (findObjectField(*styleNode, "children")) {
                errorOut = path + ".style_ref style must not contain children: " + rawRef;
                return false;
            }
            context->styleStack.push_back(key);
            const bool ok = parseNode(*styleNode,
                                      out,
                                      errorOut,
                                      path + ".style_ref(" + rawRef + ")",
                                      context);
            context->styleStack.pop_back();
            if (!ok) {
                return false;
            }
        }
    }

    // anim_ref разворачиваем в поля animMode/animFps/animFrames до локальных
    // override полей ноды. Приоритет у локальных anim_* ключей ниже.
    if (const JsonValue* animRef = findObjectField(nodeValue, "anim_ref")) {
        if (!context || !context->animationsRoot) {
            errorOut = path + ".anim_ref requires animations catalog";
            return false;
        }
        if (animRef->type != JsonValue::Type::String || animRef->stringValue.empty()) {
            errorOut = path + ".anim_ref expects non-empty string";
            return false;
        }
        const std::string key = normalizeAnimationPresetRef(animRef->stringValue);
        if (key.empty()) {
            errorOut = path + ".anim_ref contains empty reference";
            return false;
        }
        if (std::find(context->animationPresetStack.begin(), context->animationPresetStack.end(), key) !=
            context->animationPresetStack.end()) {
            errorOut = path + ".anim_ref cyclic reference: " + key;
            return false;
        }
        const JsonValue* animNode = findAnimationPresetNode(*context->animationsRoot, animRef->stringValue);
        if (!animNode) {
            errorOut = path + ".anim_ref unresolved: " + animRef->stringValue;
            return false;
        }
        context->animationPresetStack.push_back(key);
        const bool ok = applyAnimationSpecObject(*animNode,
                                                 out,
                                                 context,
                                                 path + ".anim_ref(" + animRef->stringValue + ")",
                                                 errorOut);
        context->animationPresetStack.pop_back();
        if (!ok) {
            return false;
        }
    }

    auto parseEffectsArrayForNode = [&](const JsonValue& arrayValue,
                                        std::string_view keyName,
                                        std::vector<UiLayoutNode::EffectSpec>& outEffects) -> bool {
        if (arrayValue.type != JsonValue::Type::Array) {
            errorOut = path + "." + std::string(keyName) + " expects array";
            return false;
        }
        outEffects.clear();
        outEffects.reserve(arrayValue.arrayValue.size());
        for (std::size_t i = 0; i < arrayValue.arrayValue.size(); ++i) {
            const JsonValue& item = arrayValue.arrayValue[i];
            if (item.type != JsonValue::Type::Object) {
                errorOut = path + "." + std::string(keyName) + "[" + std::to_string(i) + "] must be object";
                return false;
            }
            UiLayoutNode::EffectSpec fx{};
            bool hasType = false;
            if (!applyEffectSpecObject(item,
                                       fx,
                                       hasType,
                                       context,
                                       path + "." + std::string(keyName) + "[" + std::to_string(i) + "]",
                                       errorOut)) {
                return false;
            }
            if (!hasType) {
                errorOut = path + "." + std::string(keyName) + "[" + std::to_string(i) + "].type is required";
                return false;
            }
            outEffects.push_back(std::move(fx));
        }
        return true;
    };

    auto parseNodeStateBlock = [&](const JsonValue& stateValue,
                                   std::string_view stateKey,
                                   UiLayoutNode::StateSpec& outState) -> bool {
        const std::string statePath = path + "." + std::string(stateKey);
        if (stateValue.type != JsonValue::Type::Object) {
            errorOut = statePath + " expects object";
            return false;
        }
        for (const auto& [key, rawStateValue] : stateValue.objectValue) {
            const JsonValue* valuePtr = &rawStateValue;
            if (rawStateValue.type == JsonValue::Type::String) {
                if (const JsonValue* themed = resolveThemeToken(context, rawStateValue.stringValue)) {
                    valuePtr = themed;
                } else if (looksLikeThemeToken(rawStateValue.stringValue)) {
                    errorOut = statePath + "." + key + " unresolved theme token: " + rawStateValue.stringValue;
                    return false;
                }
            }
            const JsonValue& value = *valuePtr;

            if (key == "if") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".if expects string";
                    return false;
                }
                outState.ifExpr = value.stringValue;
                continue;
            }
            if (key == "opacity") {
                float v = 0.0f;
                if (!parseFloatNumber(value, v)) {
                    errorOut = statePath + ".opacity expects number";
                    return false;
                }
                outState.opacity = std::clamp(v, 0.0f, 1.0f);
                continue;
            }
            if (key == "effects") {
                if (!parseEffectsArrayForNode(value, stateKey == std::string_view("active")
                                                         ? "active.effects"
                                                         : (stateKey == std::string_view("inactive")
                                                                ? "inactive.effects"
                                                                : "disabled.effects"),
                                              outState.effects)) {
                    return false;
                }
                continue;
            }
            if (key == "text_color") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".text_color expects string";
                    return false;
                }
                outState.textColor = value.stringValue;
                continue;
            }
            if (key == "border_color") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".border_color expects string";
                    return false;
                }
                outState.borderColor = value.stringValue;
                continue;
            }
            if (key == "background_color") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".background_color expects string";
                    return false;
                }
                outState.backgroundColor = value.stringValue;
                continue;
            }
            if (key == "playhead_color") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".playhead_color expects string";
                    return false;
                }
                outState.playheadColor = value.stringValue;
                continue;
            }
            if (key == "font") {
                if (value.type != JsonValue::Type::String) {
                    errorOut = statePath + ".font expects string";
                    return false;
                }
                outState.font = value.stringValue;
                continue;
            }
            if (key == "font_size") {
                float v = 0.0f;
                if (!parseFloatNumber(value, v)) {
                    errorOut = statePath + ".font_size expects number";
                    return false;
                }
                outState.fontSize = v;
                continue;
            }
            if (key == "knob_size") {
                float v = 0.0f;
                if (!parseFloatNumber(value, v)) {
                    errorOut = statePath + ".knob_size expects number";
                    return false;
                }
                outState.knobSize = std::clamp(v, 0.2f, 4.0f);
                continue;
            }
            errorOut = statePath + ": unsupported key '" + key + "'";
            return false;
        }
        return true;
    };

    for (const auto& [key, rawValue] : nodeValue.objectValue) {
        if (key == "children") {
            continue;
        }
        if (key == "style_ref") {
            continue;
        }
        if (key == "anim_ref") {
            continue;
        }
        const JsonValue* valuePtr = &rawValue;
        if (rawValue.type == JsonValue::Type::String) {
            if (const JsonValue* themed = resolveThemeToken(context, rawValue.stringValue)) {
                valuePtr = themed;
            } else if (looksLikeThemeToken(rawValue.stringValue)) {
                errorOut = path + "." + key + " unresolved theme token: " + rawValue.stringValue;
                return false;
            }
        }
        const JsonValue& value = *valuePtr;
        if (key == "type") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".type expects string";
                return false;
            }
            out.type = parseUiLayoutNodeType(value.stringValue);
            if (out.type == UiLayoutNodeType::Unknown) {
                errorOut = path + ".type has unknown node type '" + value.stringValue + "'";
                return false;
            }
            continue;
        }
        if (key == "id") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".id expects string";
                return false;
            }
            out.id = value.stringValue;
            continue;
        }
        if (key == "text") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".text expects string";
                return false;
            }
            out.text = value.stringValue;
            continue;
        }
        if (key == "label") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".label expects string";
                return false;
            }
            out.label = value.stringValue;
            continue;
        }
        if (key == "path") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".path expects string";
                return false;
            }
            out.assetPath = value.stringValue;
            continue;
        }
        if (key == "text_color") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".text_color expects string";
                return false;
            }
            out.textColor = value.stringValue;
            continue;
        }
        if (key == "border_color") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".border_color expects string";
                return false;
            }
            out.borderColor = value.stringValue;
            continue;
        }
        if (key == "background_color") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".background_color expects string";
                return false;
            }
            out.backgroundColor = value.stringValue;
            continue;
        }
        if (key == "playhead_color") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".playhead_color expects string";
                return false;
            }
            out.playheadColor = value.stringValue;
            continue;
        }
        if (key == "default_text_color") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".default_text_color expects string";
                return false;
            }
            out.defaultTextColor = value.stringValue;
            continue;
        }
        if (key == "font") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".font expects string";
                return false;
            }
            out.font = value.stringValue;
            continue;
        }
        if (key == "font_size") {
            float v = 0.0f;
            if (!parseFloatNumber(value, v)) {
                errorOut = path + ".font_size expects number";
                return false;
            }
            out.fontSize = v;
            continue;
        }
        if (key == "effects") {
            if (!parseEffectsArrayForNode(value, "effects", out.effects)) {
                return false;
            }
            continue;
        }
        if (key == "anim") {
            if (!applyAnimationSpecObject(value, out, context, path + ".anim", errorOut)) {
                return false;
            }
            continue;
        }
        if (key == "anim_mode") {
            if (value.type != JsonValue::Type::String || value.stringValue.empty()) {
                errorOut = path + ".anim_mode expects non-empty string";
                return false;
            }
            const std::string mode = toLowerAscii(value.stringValue);
            if (mode != "loop" && mode != "scrub") {
                errorOut = path + ".anim_mode expects loop|scrub";
                return false;
            }
            out.animMode = mode;
            continue;
        }
        if (key == "anim_fps") {
            float fps = 0.0f;
            if (!parseFloatNumber(value, fps)) {
                errorOut = path + ".anim_fps expects number";
                return false;
            }
            if (!std::isfinite(fps) || fps <= 0.0f) {
                errorOut = path + ".anim_fps must be > 0";
                return false;
            }
            out.animFps = fps;
            continue;
        }
        if (key == "anim_frames") {
            if (value.type != JsonValue::Type::Array) {
                errorOut = path + ".anim_frames expects array";
                return false;
            }
            out.animFrames.clear();
            out.animFrames.reserve(value.arrayValue.size());
            for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
                const JsonValue& frame = value.arrayValue[i];
                if (frame.type != JsonValue::Type::String || frame.stringValue.empty()) {
                    errorOut = path + ".anim_frames[" + std::to_string(i) + "] expects non-empty string";
                    return false;
                }
                out.animFrames.push_back(frame.stringValue);
            }
            continue;
        }
        if (key == "anim_show_frame") {
            bool v = false;
            if (!parseBool(value, v)) {
                errorOut = path + ".anim_show_frame expects bool";
                return false;
            }
            out.animShowFrame = v;
            continue;
        }
        if (key == "anim_frame_width") {
            float v = 0.0f;
            if (!parseFloatNumber(value, v)) {
                errorOut = path + ".anim_frame_width expects number";
                return false;
            }
            out.animFrameWidth = std::clamp(v, 0.0f, 16.0f);
            continue;
        }
        if (key == "anim_frame_radius") {
            float v = 0.0f;
            if (!parseFloatNumber(value, v)) {
                errorOut = path + ".anim_frame_radius expects number";
                return false;
            }
            out.animFrameRadius = std::clamp(v, 0.0f, 64.0f);
            continue;
        }
        if (key == "active") {
            if (!parseNodeStateBlock(value, "active", out.active)) {
                return false;
            }
            continue;
        }
        if (key == "inactive") {
            if (!parseNodeStateBlock(value, "inactive", out.inactive)) {
                return false;
            }
            continue;
        }
        if (key == "disabled") {
            if (!parseNodeStateBlock(value, "disabled", out.disabled)) {
                return false;
            }
            continue;
        }
        if (key == "effect" || key == "effect_trigger" || key == "effect_transition" ||
            key == "effect_trigger_out" || key == "effect_interval_ms" ||
            key == "effect_amount" || key == "effect_speed") {
            errorOut = path + ": key '" + key + "' is removed, use effects: [{...}]";
            return false;
        }
        if (key == "bind") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".bind expects string";
                return false;
            }
            out.bind = value.stringValue;
            continue;
        }
        if (key == "target") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".target expects string";
                return false;
            }
            out.target = value.stringValue;
            continue;
        }
        if (key == "visible_if") {
            if (value.type != JsonValue::Type::String) {
                errorOut = path + ".visible_if expects string";
                return false;
            }
            out.visibleIf = value.stringValue;
            continue;
        }
        if (key == "opacity") {
            float v = 0.0f;
            if (!parseFloatNumber(value, v)) {
                errorOut = path + ".opacity expects number";
                return false;
            }
            out.opacity = std::clamp(v, 0.0f, 1.0f);
            continue;
        }
        if (key == "knob_size") {
            float v = 0.0f;
            if (!parseFloatNumber(value, v)) {
                errorOut = path + ".knob_size expects number";
                return false;
            }
            out.knobSize = std::clamp(v, 0.2f, 4.0f);
            continue;
        }
        if (key == "options") {
            if (value.type != JsonValue::Type::Array) {
                errorOut = path + ".options expects array";
                return false;
            }
            out.options.clear();
            out.options.reserve(value.arrayValue.size());
            for (const JsonValue& item : value.arrayValue) {
                if (item.type != JsonValue::Type::String) {
                    errorOut = path + ".options expects array of strings";
                    return false;
                }
                out.options.push_back(item.stringValue);
            }
            continue;
        }
        if (key == "margin") {
            uint16_t v = 0;
            if (!parseUint16Number(value, v)) {
                errorOut = path + ".margin expects integer";
                return false;
            }
            out.margin = v;
            continue;
        }
        if (key == "padding") {
            uint16_t v = 0;
            if (!parseUint16Number(value, v)) {
                errorOut = path + ".padding expects integer";
                return false;
            }
            out.padding = v;
            continue;
        }
        if (key == "gap") {
            uint16_t v = 0;
            if (!parseUint16Number(value, v)) {
                errorOut = path + ".gap expects integer";
                return false;
            }
            out.gap = v;
            continue;
        }
        if (key == "enabled_if" || key == "enabled_id" ||
            key == "active_if" || key == "active_id" ||
            key == "effects_active" || key == "effects_inactive" || key == "effects_disabled" ||
            key == "active_opacity" || key == "inactive_opacity" || key == "disabled_opacity") {
            errorOut = path + "." + key + " is removed, use active/inactive/disabled state blocks";
            return false;
        }
        if (key == "width") {
            UiLayoutSize sz{};
            if (!parseSize(value, sz)) {
                errorOut = path + ".width expects number/px/%/auto/rows/cols/cells";
                return false;
            }
            out.width = sz;
            continue;
        }
        if (key == "height") {
            UiLayoutSize sz{};
            if (!parseSize(value, sz)) {
                errorOut = path + ".height expects number/px/%/auto/rows/cols/cells";
                return false;
            }
            out.height = sz;
            continue;
        }
        if (key == "size") {
            if (value.type != JsonValue::Type::Array || value.arrayValue.size() != 2U) {
                errorOut = path + ".size expects [w,h]";
                return false;
            }
            uint16_t w = 0;
            uint16_t h = 0;
            if (!parseUint16Number(value.arrayValue[0], w) || !parseUint16Number(value.arrayValue[1], h)) {
                errorOut = path + ".size expects [w,h] integers";
                return false;
            }
            out.width.unit = UiLayoutSize::Unit::Px;
            out.width.value = static_cast<float>(w);
            out.height.unit = UiLayoutSize::Unit::Px;
            out.height.value = static_cast<float>(h);
            continue;
        }
        if (key == "wrap") {
            bool v = false;
            if (!parseBool(value, v)) {
                errorOut = path + ".wrap expects bool";
                return false;
            }
            out.wrap = v;
            continue;
        }
        if (key == "justify") {
            UiLayoutJustify j = UiLayoutJustify::Start;
            if (!parseJustify(value, j)) {
                errorOut = path + ".justify expects start|center|end|space_between";
                return false;
            }
            out.justify = j;
            continue;
        }
        if (key == "align") {
            UiLayoutAlign a = UiLayoutAlign::Start;
            if (!parseAlign(value, a)) {
                errorOut = path + ".align expects start|center|end";
                return false;
            }
            out.align = a;
            continue;
        }
        if (key == "text_wrap") {
            bool v = false;
            if (!parseBool(value, v)) {
                errorOut = path + ".text_wrap expects bool";
                return false;
            }
            out.textWrap = v;
            continue;
        }

        errorOut = path + ": unsupported key '" + key + "'";
        return false;
    }

    if (const JsonValue* children = findObjectField(nodeValue, "children")) {
        if (!parseChildren(*children, out, errorOut, path, context)) {
            return false;
        }
    }

    return true;
}

bool parseTemplate(const JsonValue& root,
                   const JsonValue* externalThemes,
                   const JsonValue* externalStyles,
                   const JsonValue* externalEffects,
                   const JsonValue* externalAnimations,
                   UiLayoutTemplate& out,
                   std::string& errorOut) {
    if (root.type != JsonValue::Type::Object) {
        errorOut = "root must be object";
        return false;
    }
    const JsonValue* id = findObjectField(root, "id");
    if (!id || id->type != JsonValue::Type::String || id->stringValue.empty()) {
        errorOut = "top-level id is required and must be string";
        return false;
    }
    const JsonValue* layout = findObjectField(root, "layout");
    if (!layout) {
        errorOut = "top-level layout is required";
        return false;
    }

    JsonValue mergedStyles{};
    mergedStyles.type = JsonValue::Type::Object;
    if (externalStyles && externalStyles->type == JsonValue::Type::Object) {
        mergedStyles = *externalStyles;
    }
    if (const JsonValue* inlineStyles = findObjectField(root, "styles")) {
        if (inlineStyles->type != JsonValue::Type::Object) {
            errorOut = "top-level styles must be object";
            return false;
        }
        mergeStyleObjectMap(mergedStyles, *inlineStyles);
    }

    JsonValue mergedThemes{};
    mergedThemes.type = JsonValue::Type::Object;
    if (externalThemes && externalThemes->type == JsonValue::Type::Object) {
        mergedThemes = *externalThemes;
    }
    if (const JsonValue* inlineTheme = findObjectField(root, "theme")) {
        if (inlineTheme->type != JsonValue::Type::Object) {
            errorOut = "top-level theme must be object";
            return false;
        }
        mergeStyleObjectMap(mergedThemes, *inlineTheme);
    }
    if (const JsonValue* inlineThemes = findObjectField(root, "themes")) {
        if (inlineThemes->type != JsonValue::Type::Object) {
            errorOut = "top-level themes must be object";
            return false;
        }
        if (const JsonValue* nestedTheme = findObjectField(*inlineThemes, "theme")) {
            if (nestedTheme->type != JsonValue::Type::Object) {
                errorOut = "top-level themes.theme must be object";
                return false;
            }
            mergeStyleObjectMap(mergedThemes, *nestedTheme);
        } else {
            mergeStyleObjectMap(mergedThemes, *inlineThemes);
        }
    }

    JsonValue mergedEffects{};
    mergedEffects.type = JsonValue::Type::Object;
    if (externalEffects && externalEffects->type == JsonValue::Type::Object) {
        mergedEffects = *externalEffects;
    }
    if (const JsonValue* inlineEffects = findObjectField(root, "effects")) {
        if (inlineEffects->type != JsonValue::Type::Object) {
            errorOut = "top-level effects must be object";
            return false;
        }
        mergeStyleObjectMap(mergedEffects, *inlineEffects);
    }

    JsonValue mergedAnimations{};
    mergedAnimations.type = JsonValue::Type::Object;
    if (externalAnimations && externalAnimations->type == JsonValue::Type::Object) {
        mergedAnimations = *externalAnimations;
    }
    if (const JsonValue* inlineAnimations = findObjectField(root, "animations")) {
        if (inlineAnimations->type != JsonValue::Type::Object) {
            errorOut = "top-level animations must be object";
            return false;
        }
        mergeStyleObjectMap(mergedAnimations, *inlineAnimations);
    }

    ParseContext context{};
    context.stylesRoot = mergedStyles.objectValue.empty() ? nullptr : &mergedStyles;
    context.themeRoot = mergedThemes.objectValue.empty() ? nullptr : &mergedThemes;
    context.effectsRoot = mergedEffects.objectValue.empty() ? nullptr : &mergedEffects;
    context.animationsRoot = mergedAnimations.objectValue.empty() ? nullptr : &mergedAnimations;

    out = UiLayoutTemplate{};
    out.widgetId = id->stringValue;
    if (!parseNode(*layout, out.root, errorOut, "layout", &context)) {
        return false;
    }
    if (out.root.type == UiLayoutNodeType::Unknown) {
        out.root.type = UiLayoutNodeType::Column;
    }
    return true;
}

bool loadCatalogFromAncestors(const std::string& layoutPath,
                              std::string_view catalogFileName,
                              std::string_view nestedField,
                              std::string_view altNestedField,
                              JsonValue& outCatalog,
                              std::string& errorOut) {
    namespace fs = std::filesystem;
    outCatalog = JsonValue{};
    outCatalog.type = JsonValue::Type::Object;

    fs::path dir = fs::path(layoutPath).parent_path();
    std::vector<fs::path> matches{};
    while (!dir.empty()) {
        const fs::path catalogPath = dir / std::string(catalogFileName);
        if (fs::exists(catalogPath)) {
            matches.push_back(catalogPath);
        }

        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    if (matches.empty()) {
        return true;
    }

    if (matches.size() > 1U) {
        std::ostringstream ss;
        ss << "duplicate " << catalogFileName << " files found. Keep only one project-level catalog:";
        for (const fs::path& p : matches) {
            ss << "\n  - " << p.string();
        }
        errorOut = ss.str();
        return false;
    }

    const fs::path catalogPath = matches.front();
    std::ifstream in(catalogPath);
    if (!in.is_open()) {
        errorOut = "cannot open " + std::string(catalogFileName) + ": " + catalogPath.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    JsonParser parser(source);
    JsonValue root{};
    if (!parser.parse(root, errorOut)) {
        errorOut = std::string(catalogFileName) + " parse error: " + errorOut;
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        errorOut = std::string(catalogFileName) + " root must be object";
        return false;
    }

    const JsonValue* nested = nullptr;
    if (!nestedField.empty()) {
        nested = findObjectField(root, nestedField);
    }
    if (!nested && !altNestedField.empty()) {
        nested = findObjectField(root, altNestedField);
    }
    if (nested) {
        if (nested->type != JsonValue::Type::Object) {
            errorOut = std::string(catalogFileName) + ": field '" +
                       std::string(!nestedField.empty() ? nestedField : altNestedField) +
                       "' must be object";
            return false;
        }
        outCatalog = *nested;
    } else {
        outCatalog = root;
    }

    return true;
}

} // namespace

bool UiLayoutJsonLoader::loadFromFile(const std::string& path,
                                      UiLayoutTemplate& out,
                                      std::string& errorOut) {
    std::ifstream in(path);
    if (!in.is_open()) {
        errorOut = "cannot open file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();
    JsonParser parser(source);
    JsonValue root{};
    if (!parser.parse(root, errorOut)) {
        return false;
    }

    JsonValue themes{};
    if (!loadCatalogFromAncestors(path, "themes.json", "themes", "theme", themes, errorOut)) {
        return false;
    }
    JsonValue styles{};
    if (!loadCatalogFromAncestors(path, "styles.json", "styles", "", styles, errorOut)) {
        return false;
    }
    JsonValue effects{};
    if (!loadCatalogFromAncestors(path, "effects.json", "effects", "", effects, errorOut)) {
        return false;
    }
    JsonValue animations{};
    if (!loadCatalogFromAncestors(path, "animations.json", "animations", "", animations, errorOut)) {
        return false;
    }
    const JsonValue* themesPtr = themes.objectValue.empty() ? nullptr : &themes;
    const JsonValue* stylesPtr = styles.objectValue.empty() ? nullptr : &styles;
    const JsonValue* effectsPtr = effects.objectValue.empty() ? nullptr : &effects;
    const JsonValue* animationsPtr = animations.objectValue.empty() ? nullptr : &animations;
    return parseTemplate(root, themesPtr, stylesPtr, effectsPtr, animationsPtr, out, errorOut);
}

bool UiLayoutJsonLoader::loadFromString(std::string_view content,
                                        UiLayoutTemplate& out,
                                        std::string& errorOut) {
    JsonParser parser(content);
    JsonValue root{};
    if (!parser.parse(root, errorOut)) {
        return false;
    }
    return parseTemplate(root, nullptr, nullptr, nullptr, nullptr, out, errorOut);
}

} // namespace avantgarde
