#pragma once

#include "contracts/IUiInput.h"

namespace avantgarde {

class TerminalUiInput final : public IUiInput {
public:
    TerminalUiInput();
    ~TerminalUiInput() override;

    bool poll(UiInputEvent& out) noexcept override;

    static UiInputAction mapKey(char ch) noexcept;

private:
    bool valid_{false};
    int oldFlags_{0};
    bool hasOldTermios_{false};

    struct termios_storage;
    termios_storage* oldTerm_{nullptr};
};

} // namespace avantgarde
