#pragma once

#include <string>

#include "contracts/IUi.h"
#include "contracts/IUiInput.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class MacGbWindowRenderer final : public IUiRenderer {
public:
    explicit MacGbWindowRenderer(UiTheme theme, uint16_t textWidth);
    ~MacGbWindowRenderer() override;

    void render(const UiState& state) override;
    void renderCustomFrame(const std::string& monoFrame, bool showHeaderOverlay);

    // Call periodically on the main thread to dispatch window events.
    void pumpEvents() noexcept;
    // Poll mapped keyboard action captured from the window event loop.
    bool pollInput(UiInputEvent& out) noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
    uint16_t textWidth_{0};
};

} // namespace avantgarde
