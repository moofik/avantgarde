#pragma once

#include <optional>
#include <string_view>

#include "contracts/IUi.h"
#include "contracts/UiNavState.h"
#include "service/ui/layout/SceneFrame.h"

namespace avantgarde {

// Построение SceneFrame для главного Tracks-экрана.
// Это bridge между domain-state и универсальными UI-примитивами.
class TracksSceneFrameBuilder final {
public:
    static SceneFrame build(const UiState& state,
                            const UiNavState& navState,
                            uint16_t width,
                            std::string_view headerTitle,
                            std::string_view actionStatusLine,
                            std::string_view keysHintLine);
};

} // namespace avantgarde

