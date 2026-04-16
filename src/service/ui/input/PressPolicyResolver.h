#pragma once

#include <cstdint>

#include "contracts/IUiGestureInput.h"
#include "contracts/UiScene.h"

namespace avantgarde {

// Политика интерпретации "сырого" аппаратного события.
// Для одного физического контрола задает:
// - какой tap-жест отдать в UI слой,
// - нужен ли hold-режим,
// - какой hold-жест отправлять по таймауту,
// - разрешен ли автоповтор.
struct PressPolicy final {
    bool valid{false};
    UiGesture tapAction{UiGesture::None};
    int16_t tapValue{0};

    bool holdEnabled{false};
    uint32_t holdThresholdMs{300};
    UiGesture holdAction{UiGesture::None};
    int16_t holdValue{0};

    bool repeatEnabled{false};
};

// Scene-aware резолвер press-политик.
// Здесь концентрируется правило "какой физический control во что превращается".
class PressPolicyResolver final {
public:
    PressPolicy resolve(UiScene scene, PrimitiveControl control) const noexcept;

private:
    static bool mapControlToTap_(PrimitiveControl control, UiGesture& action, int16_t& value) noexcept;
    static bool allowsRepeat_(PrimitiveControl control) noexcept;
};

} // namespace avantgarde

