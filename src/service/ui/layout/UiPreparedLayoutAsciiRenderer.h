#pragma once

#include <string>
#include <vector>

#include "contracts/UiPreparedLayout.h"

namespace avantgarde {

// Рендер prepared-layout модели в текстовый кадр fallback-режима.
class UiPreparedLayoutAsciiRenderer final {
public:
    static std::vector<std::string> render(const UiPreparedLayout& prepared);
};

} // namespace avantgarde
