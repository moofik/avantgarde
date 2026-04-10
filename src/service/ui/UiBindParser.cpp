#include "service/ui/UiBindParser.h"

#include <string>

namespace avantgarde {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool parseIndex(std::string_view raw, int32_t& out) noexcept {
    if (raw.empty()) {
        return false;
    }
    uint32_t value = 0;
    for (char ch : raw) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(ch - '0');
        if (value > 65535U) {
            return false;
        }
    }
    out = static_cast<int32_t>(value);
    return true;
}

} // namespace

UiBindParsed UiBindParser::parse(std::string_view canonicalBind) {
    UiBindParsed out{};
    out.canonical = std::string(canonicalBind);

    if (canonicalBind.empty()) {
        out.error = "Bind is empty.";
        return out;
    }

    static constexpr std::string_view kFxParamPrefix = "fx.selected.param.";
    if (startsWith(canonicalBind, kFxParamPrefix)) {
        const std::string_view tail = canonicalBind.substr(kFxParamPrefix.size());
        out.ns = "fx";
        out.field = std::string("selected.param.") + std::string(tail);
        out.actionId = UiAction::Id::SceneFxParamValue;

        if (tail == "selected") {
            out.ok = true;
            out.paramIndex = -1;
            return out;
        }

        int32_t idx = -1;
        if (!parseIndex(tail, idx)) {
            out.error = "Invalid fx.selected.param.<index> bind.";
            return out;
        }
        out.ok = true;
        out.paramIndex = idx;
        return out;
    }

    const std::size_t dot = canonicalBind.find('.');
    if (dot == std::string_view::npos || dot == 0U || dot + 1U >= canonicalBind.size()) {
        out.error = "Bind must be namespace-first: <namespace>.<field>.";
        return out;
    }

    out.ns = std::string(canonicalBind.substr(0, dot));
    out.field = std::string(canonicalBind.substr(dot + 1U));
    out.ok = true;
    return out;
}

} // namespace avantgarde
