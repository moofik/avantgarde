#include "service/ui/UiLayoutJsonLoader.h"

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
                            // Для конфигов нам достаточно ASCII; остальное заменяем маркером.
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

bool parseNode(const JsonValue& nodeValue,
               UiLayoutNode& out,
               std::string& errorOut,
               std::string path);

bool parseChildren(const JsonValue& value,
                   UiLayoutNode& out,
                   std::string& errorOut,
                   const std::string& path) {
    if (value.type != JsonValue::Type::Array) {
        errorOut = path + ".children must be array";
        return false;
    }
    out.children.clear();
    out.children.reserve(value.arrayValue.size());
    for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
        UiLayoutNode child{};
        if (!parseNode(value.arrayValue[i], child, errorOut, path + ".children[" + std::to_string(i) + "]")) {
            return false;
        }
        out.children.push_back(std::move(child));
    }
    return true;
}

bool parseNode(const JsonValue& nodeValue,
               UiLayoutNode& out,
               std::string& errorOut,
               std::string path) {
    if (nodeValue.type != JsonValue::Type::Object) {
        errorOut = path + " must be object";
        return false;
    }

    for (const auto& [key, value] : nodeValue.objectValue) {
        if (key == "children") {
            continue;
        }
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
            if (value.type != JsonValue::Type::Array) {
                errorOut = path + ".effects expects array";
                return false;
            }
            out.effects.clear();
            out.effects.reserve(value.arrayValue.size());
            for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
                const JsonValue& item = value.arrayValue[i];
                if (item.type != JsonValue::Type::Object) {
                    errorOut = path + ".effects[" + std::to_string(i) + "] must be object";
                    return false;
                }
                UiLayoutNode::EffectSpec fx{};
                bool hasType = false;
                for (const auto& [fxKey, fxVal] : item.objectValue) {
                    if (fxKey == "type") {
                        if (fxVal.type != JsonValue::Type::String || fxVal.stringValue.empty()) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].type expects non-empty string";
                            return false;
                        }
                        fx.type = fxVal.stringValue;
                        hasType = true;
                        continue;
                    }
                    if (fxKey == "effect_trigger") {
                        if (fxVal.type != JsonValue::Type::String) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_trigger expects string";
                            return false;
                        }
                        fx.effectTrigger = fxVal.stringValue;
                        continue;
                    }
                    if (fxKey == "effect_color") {
                        if (fxVal.type != JsonValue::Type::String) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_color expects string";
                            return false;
                        }
                        fx.effectColor = fxVal.stringValue;
                        continue;
                    }
                    if (fxKey == "effect_transition") {
                        if (fxVal.type != JsonValue::Type::String) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_transition expects string";
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
                                errorOut = path + ".effects[" + std::to_string(i) +
                                           "].effect_trigger_out expects duration number/string";
                                return false;
                            }
                            outMs = static_cast<uint32_t>(rounded);
                        } else if (fxVal.type == JsonValue::Type::String) {
                            if (!parseDurationMs(fxVal.stringValue, outMs)) {
                                errorOut = path + ".effects[" + std::to_string(i) +
                                           "].effect_trigger_out expects duration (e.g. \"1s\", \"750ms\", 1000)";
                                return false;
                            }
                        } else {
                            errorOut = path + ".effects[" + std::to_string(i) +
                                       "].effect_trigger_out expects duration number/string";
                            return false;
                        }
                        fx.effectTriggerOutMs = outMs;
                        continue;
                    }
                    if (fxKey == "effect_interval_ms") {
                        uint16_t ms = 0;
                        if (!parseUint16Number(fxVal, ms)) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_interval_ms expects integer";
                            return false;
                        }
                        fx.effectIntervalMs = ms;
                        continue;
                    }
                    if (fxKey == "effect_amount") {
                        float amount = 0.0f;
                        if (!parseFloatNumber(fxVal, amount)) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_amount expects number";
                            return false;
                        }
                        fx.effectAmount = amount;
                        continue;
                    }
                    if (fxKey == "effect_speed") {
                        float speed = 0.0f;
                        if (!parseFloatNumber(fxVal, speed)) {
                            errorOut = path + ".effects[" + std::to_string(i) + "].effect_speed expects number";
                            return false;
                        }
                        fx.effectSpeed = speed;
                        continue;
                    }
                    errorOut = path + ".effects[" + std::to_string(i) + "]: unsupported key '" + fxKey + "'";
                    return false;
                }
                if (!hasType) {
                    errorOut = path + ".effects[" + std::to_string(i) + "].type is required";
                    return false;
                }
                out.effects.push_back(std::move(fx));
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
        if (key == "width") {
            UiLayoutSize sz{};
            if (!parseSize(value, sz)) {
                errorOut = path + ".width expects px/percent/auto";
                return false;
            }
            out.width = sz;
            continue;
        }
        if (key == "height") {
            UiLayoutSize sz{};
            if (!parseSize(value, sz)) {
                errorOut = path + ".height expects px/percent/auto";
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
        if (!parseChildren(*children, out, errorOut, path)) {
            return false;
        }
    }

    return true;
}

bool parseTemplate(const JsonValue& root, UiLayoutTemplate& out, std::string& errorOut) {
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

    out = UiLayoutTemplate{};
    out.widgetId = id->stringValue;
    if (!parseNode(*layout, out.root, errorOut, "layout")) {
        return false;
    }
    if (out.root.type == UiLayoutNodeType::Unknown) {
        out.root.type = UiLayoutNodeType::Column;
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
    return loadFromString(ss.str(), out, errorOut);
}

bool UiLayoutJsonLoader::loadFromString(std::string_view content,
                                        UiLayoutTemplate& out,
                                        std::string& errorOut) {
    JsonParser parser(content);
    JsonValue root{};
    if (!parser.parse(root, errorOut)) {
        return false;
    }
    return parseTemplate(root, out, errorOut);
}

} // namespace avantgarde
