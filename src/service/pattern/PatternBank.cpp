#include "service/pattern/PatternBank.h"

namespace avantgarde {

std::size_t PatternBank::size() const noexcept {
    return states_.size();
}

bool PatternBank::contains(PatternId id) const noexcept {
    return states_.find(id) != states_.end();
}

bool PatternBank::get(PatternId id, PatternState& out) const {
    const auto it = states_.find(id);
    if (it == states_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool PatternBank::put(const PatternState& state) {
    if (state.id == kInvalidPatternId) {
        // Невалидный id запрещаем сохранять, чтобы не ломать адресацию банка.
        return false;
    }
    // Upsert: если id уже есть, состояние полностью заменяется новым снимком.
    states_[state.id] = state;
    return true;
}

bool PatternBank::erase(PatternId id) {
    return states_.erase(id) > 0;
}

} // namespace avantgarde
