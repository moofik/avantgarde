#pragma once

#include "contracts/IUi.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class MacGbWindowRenderer final : public IUiRenderer {
public:
    explicit MacGbWindowRenderer(UiTheme theme, uint16_t textWidth);
    ~MacGbWindowRenderer() override;

    void render(const UiState& state) override;

    // Call periodically on the main thread to dispatch window events.
    void pumpEvents() noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
    uint16_t textWidth_{0};
};

} // namespace avantgarde
