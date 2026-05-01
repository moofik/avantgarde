#pragma once

#include <cstdint>

#include "contracts/IUiGestureInput.h"

namespace avantgarde::raspi {

// Маппинг Linux evdev keycode -> унифицированный PrimitiveControl.
// Интерфейс аналогичен по роли macOS input mapper: только перевод кода кнопки.
PrimitiveControl mapPrimitiveLinuxKeyCode(uint16_t code, bool shiftHeld) noexcept;

} // namespace avantgarde::raspi

