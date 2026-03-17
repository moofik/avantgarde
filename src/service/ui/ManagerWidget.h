#pragma once

#include <string>
#include <vector>

#include "contracts/IUiWidget.h"

namespace avantgarde {

// Single-panel file manager widget (MC-like navigation, compact layout).
// Handles directory traversal, file selection and preview/load intents.
class ManagerWidget final : public IUiWidget {
public:
    explicit ManagerWidget(uint16_t frameWidth = 60) noexcept;

    const char* id() const noexcept override;
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    WidgetOutput onInput(UiInputAction action, const UiState& rtState, UiNavState& navState) override;

private:
    struct Entry {
        std::string name;
        std::string path;
        bool isDir{false};
    };

    uint16_t frameWidth_{60};
    uint16_t listRows_{10};
    bool autoPreview_{false};
    std::string lastError_;
    std::string loadedCwd_;
    std::vector<Entry> entries_;

    void refresh_(UiNavState& navState);
    const Entry* selected_(const UiNavState& navState) const noexcept;
    static bool isAudioFile_(const std::string& name);
    static std::string toLower_(std::string s);
    static std::string padRight_(const std::string& s, std::size_t width);
    static std::string trimMiddle_(const std::string& s, std::size_t width);
};

} // namespace avantgarde

