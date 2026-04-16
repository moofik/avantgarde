#include "service/ui/input/UiInputInterpreter.h"

#include <algorithm>

namespace avantgarde {

uint16_t UiInputInterpreter::keyOf_(PrimitiveControl control) noexcept {
    return static_cast<uint16_t>(control);
}

void UiInputInterpreter::enqueue_(UiGesture action, int16_t value, UiPressType press) noexcept {
    if (action == UiGesture::None) {
        return;
    }
    UiGestureEvent out{};
    out.action = action;
    out.value = value;
    out.press = press;
    if (ready_.size() >= 1024U) {
        ready_.pop_front();
    }
    ready_.push_back(out);
}

void UiInputInterpreter::onPrimitiveEvent(const PrimitiveInputEvent& ev,
                                          UiScene scene,
                                          uint64_t nowMs) noexcept {
    if (ev.control == PrimitiveControl::None) {
        return;
    }
    const PressPolicy policy = policyResolver_.resolve(scene, ev.control);
    if (!policy.valid) {
        return;
    }

    const uint16_t key = keyOf_(ev.control);
    switch (ev.phase) {
        case PrimitivePhase::Down: {
            if (policy.holdEnabled) {
                HoldState hs{};
                hs.policy = policy;
                hs.pressStartMs = nowMs;
                hs.holdEmitted = false;
                holdStates_[key] = hs;
                return;
            }
            enqueue_(policy.tapAction, policy.tapValue, UiPressType::Tap);
            return;
        }
        case PrimitivePhase::Repeat: {
            if (!policy.repeatEnabled || policy.holdEnabled) {
                return;
            }
            enqueue_(policy.tapAction, policy.tapValue, UiPressType::Repeat);
            return;
        }
        case PrimitivePhase::Up: {
            const auto it = holdStates_.find(key);
            if (it == holdStates_.end()) {
                return;
            }
            const HoldState hs = it->second;
            holdStates_.erase(it);
            if (!hs.holdEmitted) {
                enqueue_(hs.policy.tapAction, hs.policy.tapValue, UiPressType::Tap);
            }
            return;
        }
        default:
            return;
    }
}

void UiInputInterpreter::tick(uint64_t nowMs) noexcept {
    for (auto& [key, state] : holdStates_) {
        (void)key;
        if (!state.policy.holdEnabled || state.holdEmitted) {
            continue;
        }
        const uint64_t elapsed = (nowMs >= state.pressStartMs) ? (nowMs - state.pressStartMs) : 0U;
        if (elapsed < state.policy.holdThresholdMs) {
            continue;
        }
        state.holdEmitted = true;
        enqueue_(state.policy.holdAction, state.policy.holdValue, UiPressType::Hold);
    }
}

bool UiInputInterpreter::poll(UiGestureEvent& out) noexcept {
    out = UiGestureEvent{};
    if (ready_.empty()) {
        return false;
    }
    out = ready_.front();
    ready_.pop_front();
    return true;
}

} // namespace avantgarde

