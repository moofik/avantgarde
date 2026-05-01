#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "contracts/IUi.h"
#include "contracts/UiPreparedLayout.h"
#include "platform/raspi/RpiPixelCanvas.h"
#include "platform/raspi/RpiUiConfig.h"

namespace avantgarde::raspi {

// Linux/Raspberry renderer для prepared-layout кадра.
// По роли совпадает с MacPrimitiveWindowRenderer:
// - хранит backend вывода,
// - вызывает scene painter,
// - не содержит input-логики.
class RpiPrimitiveWindowRenderer final {
public:
    RpiPrimitiveWindowRenderer();
    ~RpiPrimitiveWindowRenderer();

    bool init(const RpiUiConfig& config, std::string& errorOut);
    void render(const UiState& state);
    void renderPreparedLayout(const UiPreparedLayout& prepared) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace avantgarde::raspi
