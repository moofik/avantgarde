#pragma once

#include <cstdint>

#include "contracts/IPattern.h"
#include "service/pattern/PatternBank.h"
#include "service/pattern/PatternScheduler.h"
#include "service/pattern/PatternSnapshotManager.h"

namespace avantgarde {

/**
 * @brief Оркестратор pattern-подсистемы (bank + scheduler + active pattern).
 *
 * Роль в архитектуре:
 * - Инкапсулирует хранение паттернов (PatternBank).
 * - Инкапсулирует квантизированное планирование switch (PatternScheduler).
 * - Держит и обновляет текущий activePatternId.
 *
 * Что НЕ делает:
 * - Не конвертирует step events в аудио-команды.
 *   (Эта ответственность останется у отдельного runtime player/extension.)
 */
class PatternEngine final {
public:
    /**
     * @brief Создать PatternEngine с заданной sample rate для scheduler.
     * @param sampleRate Частота дискретизации для расчетов квантизации.
     */
    explicit PatternEngine(double sampleRate = 48000.0) noexcept;

    /**
     * @brief Доступ к банку паттернов (mutable).
     * @return Ссылка на реализацию IPatternBank.
     */
    IPatternBank& bank() noexcept;
    /**
     * @brief Доступ к банку паттернов (read-only).
     * @return Константная ссылка на реализацию IPatternBank.
     */
    const IPatternBank& bank() const noexcept;

    /**
     * @brief Доступ к менеджеру precompiled snapshot-ов (mutable).
     * @return Ссылка на PatternSnapshotManager.
     */
    PatternSnapshotManager& snapshots() noexcept;
    /**
     * @brief Доступ к менеджеру precompiled snapshot-ов (read-only).
     * @return Константная ссылка на PatternSnapshotManager.
     */
    const PatternSnapshotManager& snapshots() const noexcept;

    /**
     * @brief Канонический способ записать/обновить паттерн в engine.
     *
     * Выполняет:
     * 1) put в PatternBank;
     * 2) upsert compiled snapshot в PatternSnapshotManager.
     *
     * @param state Полный PatternState.
     * @return true если оба шага успешны.
     */
    bool putPattern(const PatternState& state);
    /**
     * @brief Удалить паттерн из bank и snapshot manager.
     * @param id PatternId.
     * @return true если хотя бы одна из подсистем реально удалила запись.
     */
    bool erasePattern(PatternId id);

    /**
     * @brief Доступ к планировщику switch (mutable).
     * @return Ссылка на реализацию IPatternScheduler.
     */
    IPatternScheduler& scheduler() noexcept;
    /**
     * @brief Доступ к планировщику switch (read-only).
     * @return Константная ссылка на реализацию IPatternScheduler.
     */
    const IPatternScheduler& scheduler() const noexcept;

    /**
     * @brief Установить active pattern немедленно (без квантизации).
     * @param id Идентификатор паттерна для активации.
     * @return true если id валиден и существует в банке, либо если это kInvalidPatternId
     *         (сброс активного состояния); иначе false.
     */
    bool setActivePattern(PatternId id) noexcept;
    /**
     * @brief Текущий активный паттерн.
     * @return PatternId активного паттерна или kInvalidPatternId, если активного нет.
     */
    PatternId activePatternId() const noexcept;

    /**
     * @brief Запросить переключение паттерна через scheduler.
     * @param target Целевой PatternId.
     * @param quantize Режим квантизации (None/Beat/Bar).
     * @note Это фасад над scheduler.requestSwitch().
     */
    void requestSwitch(PatternId target, QuantizeMode quantize) noexcept;

    /**
     * @brief Обновить scheduler текущим RT-снапшотом транспорта.
     * @param transport Текущее RT-состояние транспорта.
     * @note Вызывается из RT-пути (обычно раз в блок).
     */
    void onTransportRt(const TransportRtSnapshot& transport) noexcept;

    /**
     * @brief Попробовать применить готовый switch, если scheduler его выдал.
     * @param outPatternId Возвращает PatternId, который реально стал активным.
     * @return true если switch успешно применен; иначе false.
     *
     * Детали:
     * - Если scheduler выдал id, которого уже нет в банке, переключение отклоняется.
     * - При успехе activePattern_ обновляется атомарно на уровне этого объекта.
     */
    bool popReadySwitch(PatternId& outPatternId) noexcept;
    /**
     * @brief Забрать готовый switch как уже скомпилированный apply-план.
     * @param outPlan Выходной план операций from->to для быстрого применения.
     * @return true если switch готов, валиден и план успешно собран.
     *
     * Поведение:
     * - читает ready-switch из scheduler;
     * - проверяет наличие target в bank;
     * - строит diff-план через PatternSnapshotManager;
     * - при успехе переключает activePattern_ на target.
     *
     * Важно:
     * - Этот метод не применяет план к движку сам.
     * - Применение делает внешний оркестратор (application/service слой).
     */
    bool popReadySwitchPlan(CompiledSwitchPlan& outPlan) noexcept;
    /**
     * @brief Собрать apply-план переключения active -> target и принять target как active.
     * @param target Целевой паттерн.
     * @param outPlan Готовый diff/full-apply план.
     * @return true если target валиден и план собран.
     *
     * Это control-thread операция. Метод не использует scheduler.
     */
    bool buildSwitchPlanTo(PatternId target, CompiledSwitchPlan& outPlan) noexcept;

private:
    /**
     * @brief Хранилище всех известных паттернов.
     */
    PatternBank bank_{};
    /**
     * @brief Precompiled snapshot storage + diff-plan builder.
     */
    PatternSnapshotManager snapshotManager_{};
    /**
     * @brief Компонент квантизированного тайминга переключений.
     */
    PatternScheduler scheduler_{};
    /**
     * @brief Текущий активный PatternId.
     */
    PatternId activePattern_{kInvalidPatternId};
};

} // namespace avantgarde
