#pragma once

#include <string>
#include <vector>

#include "service/ui/layout/SceneFrame.h"

namespace avantgarde {

// Fallback-рендерер SceneFrame -> текстовые строки.
// Используется текущим текстовым стеком и для тестирования layout-пайплайна.
class SceneFrameAsciiRenderer final {
public:
    static std::vector<std::string> render(const SceneFrame& frame);
};

} // namespace avantgarde

