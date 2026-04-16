#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "app/UiIntentApplier.h"
#include "contracts/UiIntent.h"
#include "contracts/UiNavState.h"
#include "service/snapshot/SnapshotManager.h"

namespace avantgarde {

// Оркестратор snapshot-сценариев.
//
// Задача класса:
// - принять snapshot-intent верхнего уровня (trigger/capture/recall);
// - выбрать policy-ветку через SnapshotManager;
// - применить сгенерированные intent-команды через UiIntentApplier;
// - вернуть агрегированный результат для вызывающего слоя.
//
// Важно:
// - сам orchestration здесь, а не в SamplerApplication;
// - атомарное применение каждого intent по-прежнему делается UiIntentApplier.
class SnapshotIntentOrchestrator final {
public:
    struct Context {
        // Глобальный REC-флаг.
        bool recordEnabled{false};
        // Состояние транспорта (PLAY/STOP).
        bool transportPlaying{false};
        // UI-навигация (нужна для selected track/fx в trigger-режиме).
        const UiNavState* nav{nullptr};
        // Кэш track-state control-слоя.
        std::vector<UiTrackStateView>* tracks{nullptr};
        // Mirror FX-параметров для capture.
        const std::unordered_map<uint64_t, float>* fxParamMirror{nullptr};
        // Исполнитель intent-команд.
        UiIntentApplier* intentApplier{nullptr};
        // Контекст применителя (engine/ui-store/nav/hud).
        UiIntentApplier::Context* applierContext{nullptr};
        // Флаг-защита от самозаписи при snapshot recall dispatch.
        bool* snapshotRecallDispatchFlag{nullptr};
        // Коллбек вызывается после успешного apply intent-а
        // (например, чтобы синхронизировать локальные mirror-структуры).
        std::function<void(const UiIntent&)> onIntentApplied{};
    };

    struct Result {
        // Intent был snapshot-типа и обработан оркестратором.
        bool handled{false};
        // Произошло изменение состояния (capture/recall и/или apply).
        bool changed{false};
        // Был выполнен recall из snapshot-слота.
        bool recallRequested{false};
        // Метаданные recall для event-lane записи.
        uint8_t recallSlot{0};
        uint8_t recallTrack{0};
        uint8_t recallFxSlot{0};
    };

public:
    bool supports(const UiIntent& intent) const noexcept;
    Result dispatch(const UiIntent& intent, const Context& ctx);

private:
    SnapshotManager snapshotManager_{};
};

} // namespace avantgarde

