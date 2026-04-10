#pragma once

#include <mutex>

#include "contracts/IUi.h"

namespace avantgarde {

/**
 * @brief Потокобезопасное хранилище UI-снимка состояния приложения.
 *
 * Зачем введена эта абстракция:
 * 1) В проекте есть минимум два активных потока:
 *    - control-поток, который применяет интенты и обновляет состояние;
 *    - main/UI-поток, который регулярно читает состояние для рендера.
 *    Прямой доступ к UiState без синхронизации дал бы data race.
 *
 * 2) Нам нужна единая "точка правды" для UI-состояния.
 *    Вместо разрозненных полей по разным слоям, UiStateStore хранит
 *    агрегированный UiState и отдает его копией через snapshot().
 *
 * 3) Такой слой уменьшает связность:
 *    - app/service слои работают через простой контракт set-методов и snapshot();
 *    - рендерер и виджеты не знают о деталях синхронизации.
 *
 * 4) Snapshot-подход помогает держать предсказуемый lock-order:
 *    сначала берется копия UiState, потом уже выполняются операции со сценой/рендером.
 *    Это снижает риск взаимных блокировок между разными mutex в UI-пайплайне.
 */
class UiStateStore {
public:
    // Инициализирует пустой store с валидным базовым UiState.
    UiStateStore();

    // Вернуть консистентную копию всего UI-состояния на текущий момент.
    // Возврат по значению намеренный: читатель получает "замороженный" snapshot
    // и может работать с ним вне критической секции.
    UiState snapshot() const;

    // Полная замена текущего UI-состояния.
    void setState(const UiState& state);
    // Частичное обновление transport-секции.
    void setTransport(const UiTransportState& transport);
    // Частичное обновление конкретного трека по индексу.
    // Если индекс вне диапазона, обновление игнорируется.
    void setTrack(std::size_t index, const UiTrackStateView& track);
    // Частичное обновление telemetry-секции.
    void setTelemetry(const UiTelemetryState& telemetry);
    // Частичное обновление pattern-секции.
    void setPattern(const UiPatternState& pattern);

private:
    // Охраняет доступ к state_.
    mutable std::mutex mutex_;
    // Единый агрегированный снимок UI, из которого читают виджеты/рендер.
    UiState state_;
};

} // namespace avantgarde
