#pragma once

#include <cstdint>
#include <string>

#include "contracts/IUi.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class GothicGbUiRenderer final : public IUiRenderer {
public:
    using Rgb = UiThemeRgb;
    using Palette = UiThemePalette;

    explicit GothicGbUiRenderer(UiTheme theme, uint16_t width) noexcept;
    ~GothicGbUiRenderer() override;

    void render(const UiState& state) override;
    void renderCustomFrame(const std::string& monoFrame);

    static std::string buildMonochromeFrame(const UiState& state, uint16_t width);

private:
    Palette palette_{};
    uint16_t width_{0};
    bool ansiCapable_{false};
    bool enteredAltScreen_{false};
    std::string lastFrame_;

    void ensureTerminalReady_() noexcept;
    std::string colorizeFrame_(const UiState& state, const std::string& monoFrame) const;
};

} // namespace avantgarde
