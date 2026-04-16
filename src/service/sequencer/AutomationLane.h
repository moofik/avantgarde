#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "contracts/ISequencer.h"

namespace avantgarde {

/**
 * @brief Реализация AutomationLane для Sequencer MVP.
 *
 * Назначение:
 * - Хранит automation-точки как абсолютные sampleTime события.
 * - Поддерживает запись "жестом" (begin/push/commit).
 * - Поддерживает quantized commit всего жеста одной операцией.
 * - Поддерживает batch undo/redo по жестам.
 *
 * Важно:
 * - Класс работает в control/service слое (не RT).
 * - RT-плеер в будущем будет только читать диапазоны событий и переводить их в
 *   ParamSet/RtCommand, без мутаций этого хранилища.
 */
class AutomationLane final : public IAutomationLane {
public:
    bool beginGesture(const SequencerParamTarget& target,
                      AutomationInterpolationMode interpolation) override;
    bool pushGesturePoint(uint64_t sampleTime, float value) override;
    bool commitGesture(const TransportRtSnapshot& transport,
                       QuantizeMode quantize,
                       AutomationGestureCommitResult& out) override;
    void cancelGesture() noexcept override;

    bool undoLastGesture() noexcept override;
    bool redoLastGesture() noexcept override;

    void collectEventsInRange(uint64_t beginSampleInclusive,
                              uint64_t endSampleExclusive,
                              std::vector<AutomationPointEvent>& out) const override;

    /**
     * @brief Прочитать все накопленные события (read-only).
     *
     * Нужен для тестов и диагностики control-слоя.
     */
    const std::vector<AutomationPointEvent>& events() const noexcept;

    // Добавить одиночную automation-точку напрямую (без gesture commit-пайплайна).
    // Используется редактором Sequencer View.
    uint64_t addPoint(const SequencerParamTarget& target,
                      AutomationInterpolationMode interpolation,
                      uint64_t sampleTime,
                      float value);
    // Удалить точку по eventId.
    bool removeEvent(uint64_t eventId) noexcept;
    // Сместить точку по времени на deltaSamples (отрицательное значение = влево).
    bool nudgeEventTime(uint64_t eventId, int64_t deltaSamples) noexcept;
    // Абсолютно установить время точки.
    bool setEventTime(uint64_t eventId, uint64_t sampleTime) noexcept;
    // Установить значение точки.
    bool setEventValue(uint64_t eventId, float value) noexcept;

private:
    struct PendingGesture {
        SequencerParamTarget target{};
        AutomationInterpolationMode interpolation{AutomationInterpolationMode::Linear};
        std::vector<AutomationPoint> points{};
    };

    struct GestureBatch {
        uint64_t batchId{0};
        std::vector<AutomationPointEvent> inserted{};
    };

private:
    static uint64_t quantizeForwardSample_(uint64_t now,
                                           const TransportRtSnapshot& transport,
                                           QuantizeMode quantize) noexcept;
    static uint64_t computeQuantumSamples_(const TransportRtSnapshot& transport,
                                           QuantizeMode quantize) noexcept;
    static bool sameTarget_(const SequencerParamTarget& a,
                            const SequencerParamTarget& b) noexcept;
    static void sortEvents_(std::vector<AutomationPointEvent>& events);
    void removeEventsById_(const std::vector<AutomationPointEvent>& inserted) noexcept;
    void clearUndoRedo_() noexcept;

private:
    // Канонический массив automation-точек lane.
    std::vector<AutomationPointEvent> events_{};
    // Временный буфер текущего жеста записи.
    std::optional<PendingGesture> pending_{};
    // Undo/redo стеки по gesture-batch.
    std::vector<GestureBatch> undoStack_{};
    std::vector<GestureBatch> redoStack_{};
    // Генераторы идентификаторов.
    uint64_t nextEventId_{1};
    uint64_t nextBatchId_{1};
};

} // namespace avantgarde
