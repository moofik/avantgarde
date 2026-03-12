#pragma once

#include "contracts/IDisplay.h"
#include "contracts/IUi.h"

namespace avantgarde {

class LowResUiRenderer final : public IUiRenderer {
public:
    explicit LowResUiRenderer(IDisplay& display) noexcept;

    void render(const UiState& state) override;

private:
    IDisplay& display_;
};

} // namespace avantgarde
