#pragma once

#include <cstdint>
#include <deque>
#include <unordered_map>

#include "contracts/IUiGestureInput.h"
#include "contracts/UiScene.h"
#include "service/ui/input/PressPolicyResolver.h"

namespace avantgarde {

// Scene-aware интерпретатор низкоуровневых событий ввода.
//
// Назначение:
// 1) Принять сырые PrimitiveInputEvent (Down/Up/Repeat),
// 2) Применить scene-специфичную press-политику (tap/hold/repeat),
// 3) Выдать итоговые UiGestureEvent для слоя виджетов.
//
// Важно: интерпретатор НЕ применяет бизнес-логику и не трогает состояние движка.
// Он только превращает "физический ввод" в "UI-жесты".
class UiInputInterpreter final {
public:
    // Подать одно новое сырое событие из IO слоя.
    // scene передается снаружи (активная сцена на момент события).
    // nowMs должен быть монотонным временем (обычно steady_clock в ms).
    void onPrimitiveEvent(const PrimitiveInputEvent& ev,
                          UiScene scene,
                          uint64_t nowMs) noexcept;

    // Периодический tick для генерации hold-событий без ожидания KeyUp.
    void tick(uint64_t nowMs) noexcept;

    // Забрать следующее готовое UiGestureEvent.
    bool poll(UiGestureEvent& out) noexcept;

private:
    struct HoldState final {
        PressPolicy policy{};
        uint64_t pressStartMs{0};
        bool holdEmitted{false};
    };

    static uint16_t keyOf_(PrimitiveControl control) noexcept;
    void enqueue_(UiGesture action, int16_t value, UiPressType press) noexcept;

    PressPolicyResolver policyResolver_{};
    std::unordered_map<uint16_t, HoldState> holdStates_{};
    std::deque<UiGestureEvent> ready_{};
};

} // namespace avantgarde

