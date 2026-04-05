#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace avantgarde {

// Универсальный вход для визуальных FX (не привязан к тексту/конкретному backend).
struct VisualFxRequest {
    // Стабильный id layout-ноды (из TOML).
    std::string nodeId{};
    // Уникальный id инстанса компонента (предпочтительный ключ состояния FX).
    std::string instanceKey{};
    // Текст ноды (используется как fallback seed).
    std::string nodeText{};

    // Канонический id эффекта, например "glitch".
    std::string effect{};
    // Режим триггера: "time" | "change" | "always" (пусто = time).
    std::string effectTrigger{};
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

// Универсальный стиль блока, который может быть применен к любому ROI (text/image/sprite).
struct VisualFxBlockStyle {
    bool active{false};
    float offsetX{0.0f};
    float offsetY{0.0f};
    float splitPx{0.0f};
    float alpha{1.0f};
    uint8_t sliceCount{2U};
    bool alternatePhase{false};
};

// Контракт любого визуального FX.
class IVisualFx {
public:
    virtual ~IVisualFx() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual VisualFxBlockStyle resolve(const VisualFxRequest& request) = 0;
};

} // namespace avantgarde
