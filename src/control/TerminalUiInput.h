#pragma once

#include <string_view>

#include "contracts/IUiGestureInput.h"

namespace avantgarde {

class TerminalUiInput final : public IUiGestureInput {
public:
    TerminalUiInput();
    ~TerminalUiInput() override;

    bool poll(UiGestureEvent& out) noexcept override;

    static UiGesture mapKey(char ch) noexcept;
    static UiGesture mapEscapeSequence(std::string_view seq) noexcept;

private:
    bool valid_{false};
    int oldFlags_{0};
    bool hasOldTermios_{false};

    struct termios_storage;
    termios_storage* oldTerm_{nullptr};
};

} // namespace avantgarde
