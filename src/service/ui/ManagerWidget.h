#pragma once

#include <optional>
#include <string>
#include <vector>

#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Single-panel file manager widget (MC-like navigation, compact layout).
// Handles directory traversal, file selection and preview/load intents.
class ManagerWidget final : public IUiWidget {
public:
    explicit ManagerWidget(uint16_t frameWidth = 60,
                           std::optional<UiLayoutTemplate> layoutTemplate = std::nullopt) noexcept;

    const char* id() const noexcept override;
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;

private:
    struct LayoutModel {
        bool enabled{false};
        std::string title{"MANAGER"};
        std::string keysHint{" keys [j/k] [enter] [h] [space] [a] [esc] "};
    };

    struct Entry {
        std::string name;
        std::string path;
        bool isDir{false};
    };

    uint16_t frameWidth_{60};
    uint16_t listRows_{10};
    bool autoPreview_{false};
    mutable std::string lastError_{};
    mutable std::string loadedCwd_{};
    mutable std::vector<Entry> entries_{};
    LayoutModel layout_{};
    std::optional<UiLayoutTemplate> layoutTemplate_{};

    void refresh_(UiNavState& navState) const;
    const Entry* selected_(const UiNavState& navState) const noexcept;
    void buildLayoutModel_(const UiLayoutTemplate& tpl);
    static void collectNodes_(const UiLayoutNode& root, std::vector<const UiLayoutNode*>& out) noexcept;
    static bool isAudioFile_(const std::string& name);
    static std::string toLower_(std::string s);
    static std::string padRight_(const std::string& s, std::size_t width);
    static std::string trimMiddle_(const std::string& s, std::size_t width);
};

} // namespace avantgarde
