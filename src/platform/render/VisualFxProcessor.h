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
    float phase01{0.0f};
    float crumble01{0.0f};
    float glowNearRadiusPx{0.0f};
    float glowFarRadiusPx{0.0f};
    float glowNearAlpha{0.0f};
    float glowFarAlpha{0.0f};
    bool hasTint{false};
    float tintR{1.0f};
    float tintG{1.0f};
    float tintB{1.0f};
    float tintA{1.0f};
};

// Геометрический стиль для текстовых эффектов (срезы + оффсеты).
// Это отдельный слой от pixel-postprocess и нужен, чтобы рендерер
// не хардкодил названия конкретных эффектов.
struct VisualFxTextGeometryStyle {
    bool active{false};
    uint8_t sliceCount{0U};
    float jitterX{0.0f};
    float splitPx{0.0f};
    float alpha{1.0f};
    bool alternatePhase{false};
};

// Профиль "раскрытия текста" для эффектов типа typewriter.
struct VisualFxTextRevealStyle {
    bool active{false};
    float reveal01{1.0f};
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
    // Пиксельный post-process ROI (RGBA8). Возвращает true при модификации буфера.
    bool applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request);

    // Адаптер для текстового рендера (совместимость текущего backend).
    VisualFxTextStyle resolveTextStyle(const VisualFxRequest& request);
    // Геометрический профиль для текстового рендера (без знания effect-id в рендерере).
    VisualFxTextGeometryStyle resolveTextGeometryStyle(const VisualFxRequest& request);
    // Профиль посимвольного раскрытия текста (typing и подобные эффекты).
    VisualFxTextRevealStyle resolveTextRevealStyle(const VisualFxRequest& request);
    // Подсказка рендереру: эффект требует цельный проход по всей строке
    // (нельзя дробить текст на слова/сегменты).
    bool requiresWholeTextPass(const VisualFxRequest& request) const;

    // Legacy-метод для обратной совместимости тестов/старого кода.
    VisualFxTextStyle resolveGlitchTextStyle(const VisualFxRequest& request);

private:
    // id(effect) -> реализация.
    std::unordered_map<std::string, std::unique_ptr<IVisualFx>> fxById_{};
};

} // namespace avantgarde
