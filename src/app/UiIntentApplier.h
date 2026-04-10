#pragma once

#include <vector>

#include "app/SamplerEngineLayer.h"
#include "contracts/UiIntent.h"
#include "contracts/UiNavState.h"
#include "service/UiStateStore.h"

namespace avantgarde {

// Централизованный применитель UiIntent.
// Важно:
// - класс не знает про ввод/виджеты;
// - класс не хранит undo/redo;
// - применяет intent к engine + control model + ui store + ui nav-router.
class UiIntentApplier {
public:
    // Явный runtime-контекст применения, который оркестратор подает снаружи.
    struct Context {
        SamplerEngineLayer& engine;
        UiStateStore& uiStore;
        UiTransportState& transport;
        std::vector<UiTrackStateView>& tracks;
        // UI-навигация (scene/cursor/scroll). Может быть nullptr в headless-тестах.
        UiNavState* nav{nullptr};
    };

    // Сформировать обратный intent для undo на основе текущего состояния.
    bool buildUndoIntent(const UiIntent& forward,
                         const UiTransportState& transport,
                         const std::vector<UiTrackStateView>& tracks,
                         UiIntent& undoOut) const;

    // Применить intent. Возвращает true, только если состояние реально изменилось.
    bool apply(const UiIntent& intent, Context& ctx) const;

private:
    // Безопасное ограничение track index в диапазон [0..N-1].
    static uint8_t clampTrack_(uint8_t track, const std::vector<UiTrackStateView>& tracks) noexcept;
    // Вспомогательные преобразования quantize <-> float payload для UiIntent.
    static float quantModeToIntentValue_(QuantizeMode mode) noexcept;
    static QuantizeMode quantModeFromIntentValue_(float value) noexcept;
    // Пересчитать визуальный state трека из clip + transport (без учета mute как stop).
    static void refreshTrackViewState_(uint8_t track,
                                       const UiTransportState& transport,
                                       std::vector<UiTrackStateView>& tracks) noexcept;
};

} // namespace avantgarde
