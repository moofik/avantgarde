#pragma once

#include "contracts/IUi.h"

namespace avantgarde {

class AnsiUiRenderer final : public IUiRenderer {
public:
    void render(const UiState& state) override;
};

} // namespace avantgarde
