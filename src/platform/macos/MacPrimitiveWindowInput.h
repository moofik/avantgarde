#pragma once

#include <memory>

#include "contracts/IUiGestureInput.h"

namespace avantgarde::macos {

// Сборщик input-событий для macOS window режима.
// Важно: не знает ничего про рендер кадра и не зависит от renderer-классов.
class MacPrimitiveWindowInput final {
public:
    MacPrimitiveWindowInput();
    ~MacPrimitiveWindowInput();

    bool readNextInputEvent(PrimitiveInputEvent& out) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace avantgarde::macos
