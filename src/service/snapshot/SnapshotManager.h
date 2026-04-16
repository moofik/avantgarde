#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "contracts/IUi.h"
#include "contracts/UiIntent.h"

namespace avantgarde {

// Менеджер пользовательских snapshot-слотов (E/R/T/Y).
//
// Зона ответственности:
// - хранить 4 snapshot-слота;
// - собирать snapshot из текущего FX-контекста (track + fxSlot + param mirror);
// - отдавать batch intent-ов для recall;
// - возвращать side-intent-ы (HUD), чтобы orchestration-слой не хардкодил уведомления.
//
// Важно:
// - менеджер НЕ знает про audio engine;
// - менеджер НЕ применяет intent-ы сам;
// - менеджер НЕ пишет lane/events — это делает orchestration-слой выше.
class SnapshotManager final {
public:
    static constexpr uint8_t kSlotCount = 4U;

    // Публичное состояние одного snapshot-слота.
    struct SlotState {
        bool occupied{false};
        uint8_t track{0};
        uint8_t fxSlot{0};
        std::vector<UiIntent> intents{};
    };

    // Runtime-контекст, который orchestration-слой передает для обработки snapshot-жеста.
    struct GestureContext {
        bool recordEnabled{false};
        bool transportPlaying{false};
        uint8_t selectedTrack{0};
        uint8_t selectedFx{0};
        const std::vector<UiTrackStateView>* tracks{nullptr};
        const std::unordered_map<uint64_t, float>* fxParamMirror{nullptr};
    };

    // Результат обработки snapshot-жеста.
    struct GestureResult {
        bool handled{false};
        bool changed{false};
        bool recallRequested{false};
        uint8_t recallTrack{0};
        uint8_t recallFxSlot{0};
        uint8_t recallSlot{0};
        // Intent-ы, которые нужно применить к runtime/model.
        std::vector<UiIntent> applyIntents{};
        // Side-intent-ы (HUD и прочие out-of-band реакции).
        std::vector<UiIntent> sideIntents{};
    };

public:
    // Явный capture (без policy-ветвления trigger режима).
    GestureResult captureSlot(uint8_t slotIndex,
                              uint8_t track,
                              uint8_t fxSlot,
                              const std::vector<UiTrackStateView>& tracks,
                              const std::unordered_map<uint64_t, float>& fxParamMirror);

    // Базовая trigger-логика snapshot-кнопки внутри manager-а.
    // Примечание: scene-aware policy (например запрет capture вне FX-сцен)
    // выполняется внешним orchestration-слоем.
    GestureResult handleSlotGesture(uint8_t slotIndex, const GestureContext& ctx);

    // Прямой recall по slot index (для sequencer EventLane SnapshotRecall).
    // Нужен, чтобы воспроизведение lane могло переиспользовать те же snapshot-данные.
    GestureResult buildRecallResult(uint8_t slotIndex) const;

    // Read-only снимок текущих слотов (для диагностики/тестов/UI).
    const std::array<SlotState, kSlotCount>& slots() const noexcept { return slots_; }

private:
    static uint8_t clampTrack_(uint8_t track, const std::vector<UiTrackStateView>& tracks) noexcept;
    static uint64_t makeFxParamMirrorKey_(uint8_t track, uint8_t fxSlot, uint16_t paramIndex) noexcept;
    static UiIntent makeSnapshotNoticeIntent_(UiIntentType type, uint8_t slot);
    static UiIntent makeHudTextIntent_(std::string text, UiHudIntentLevel level);

    // Сборка snapshot-данных для capture (SetFxEnabled + SetFxParam[*]).
    bool buildCaptureIntents_(uint8_t track,
                              uint8_t fxSlot,
                              const std::vector<UiTrackStateView>& tracks,
                              const std::unordered_map<uint64_t, float>& fxParamMirror,
                              std::vector<UiIntent>& outIntents) const;

private:
    std::array<SlotState, kSlotCount> slots_{};
};

} // namespace avantgarde
