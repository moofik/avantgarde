#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "platform/render/IVisualFx.h"

namespace avantgarde {

// Унифицированный стиль визуального FX для текстового рендера.
// Конкретный backend (macOS/Raspberry) сам решает, как применить эти параметры.
struct VisualFxTextStyle {
    bool active{false};
    float jitterX{0.0f};
    float splitPx{0.0f};
    float alpha{1.0f};
    uint8_t bandCount{2U};
    bool alternatePhase{false};
};

// Платформенно-нейтральный процессор визуальных FX.
// Не привязан к тексту: базовый API - resolveBlockStyle(...).
class VisualFxProcessor final {
public:
    VisualFxProcessor();

    // Регистрация кастомного FX-реализатора (по id()).
    void registerFx(std::unique_ptr<IVisualFx> fx);

    // Базовый API для произвольного ROI-блока.
    VisualFxBlockStyle resolveBlockStyle(const VisualFxRequest& request);

    // Адаптер для текстового рендера (совместимость текущего backend).
    VisualFxTextStyle resolveTextStyle(const VisualFxRequest& request);

    // Legacy-метод для обратной совместимости тестов/старого кода.
    VisualFxTextStyle resolveGlitchTextStyle(const VisualFxRequest& request);

private:
    // id(effect) -> реализация.
    std::unordered_map<std::string, std::unique_ptr<IVisualFx>> fxById_{};
};

} // namespace avantgarde
