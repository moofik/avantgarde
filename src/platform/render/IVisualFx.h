#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace avantgarde {

// Универсальный вход для визуальных FX (не привязан к тексту/конкретному backend).
struct VisualFxRequest {
    // Стабильный id layout-ноды (из JSON/TOML шаблона).
    std::string nodeId{};
    // Уникальный id инстанса компонента (предпочтительный ключ состояния FX).
    std::string instanceKey{};
    // Текст ноды (используется как fallback seed).
    std::string nodeText{};

    // Канонический id эффекта, например "glitch".
    std::string effect{};
    // Опциональный tint-цвет эффекта в hex (например "#A86DB5").
    std::string effectColor{};
    // Режим триггера: "time" | "change" | "always" (пусто = time).
    std::string effectTrigger{};
    // Режим transition для trigger-переходов:
    // - ""/"crumble" -> фазовое fadeOut/fadeIn c "рассыпанием"
    // - "instant"/"none" -> мгновенная смена без crumble/fade.
    std::string effectTransition{};
    // Таймаут "release" для trigger=change (мс): сколько держать эффект
    // после последнего изменения значения. 0 = default FX.
    uint32_t effectTriggerOutMs{0};
    // Период старта эффекта (мс), не длительность.
    uint16_t effectIntervalMs{0};
    // Интенсивность [0..1].
    float effectAmount{0.0f};
    // Скорость внутренней анимации (больше -> быстрее фаза).
    float effectSpeed{1.0f};

    // Время кадра в ms (steady clock domain).
    uint64_t nowMs{0};

    // Опциональный управляющий value [0..1] для trigger=change.
    bool hasValue01{false};
    float value01{0.0f};
};

// ROI-view в формате RGBA8 (row-major).
// View не владеет памятью: буфером владеет вызывающая сторона (renderer).
struct VisualFxRgbaView {
    uint8_t* pixels{nullptr};
    uint16_t width{0};
    uint16_t height{0};
    uint32_t strideBytes{0};
};

// Параметры glitch-эффекта (срезы/джиттер/сплит).
struct GlitchVisualFxPayload {
    float offsetX{0.0f};
    float offsetY{0.0f};
    float splitPx{0.0f};
    float alpha{1.0f};
    uint8_t sliceCount{2U};
    bool alternatePhase{false};
    float phase01{0.0f};
    float crumble01{0.0f};
};

// Параметры glow-эффекта (ореол/тень/цвет).
struct GlowVisualFxPayload {
    float offsetX{0.0f};
    float radiusPx{0.0f};
    float alpha{1.0f};
    float phase01{0.0f};
    float nearRadiusPx{0.0f};
    float farRadiusPx{0.0f};
    float nearAlpha{0.0f};
    float farAlpha{0.0f};
    bool hasTint{false};
    float tintR{1.0f};
    float tintG{1.0f};
    float tintB{1.0f};
    float tintA{1.0f};
};

// Параметры typing-эффекта (прогрессивное "напечатанное" раскрытие текста).
struct TypingVisualFxPayload {
    // Доля раскрытого контента [0..1].
    float reveal01{0.0f};
    // Фаза [0..1] внутри одного прохода эффекта.
    float phase01{0.0f};
    // Длительность одного прохода в ms (для отладки/метрик).
    uint32_t durationMs{0U};
};

using VisualFxPayload =
    std::variant<std::monostate, GlitchVisualFxPayload, GlowVisualFxPayload, TypingVisualFxPayload>;

// Типизированный стиль блока для ROI (text/image/sprite):
// один активный FX -> один payload конкретного типа.
struct VisualFxBlockStyle {
    bool active{false};
    VisualFxPayload payload{};
};

// Контракт любого визуального FX.
class IVisualFx {
public:
    virtual ~IVisualFx() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual VisualFxBlockStyle resolve(const VisualFxRequest& request) = 0;
    // Пиксельный post-process для ROI.
    // Возвращает true, если буфер был модифицирован.
    virtual bool applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) {
        (void)view;
        (void)request;
        return false;
    }
};

} // namespace avantgarde
