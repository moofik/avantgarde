#pragma once

#include <atomic>
#include <cstdint>

#include "contracts/IPattern.h"

namespace avantgarde {

/**
 * @brief Квантованный планировщик переключения паттернов.
 *
 * Назначение:
 * - Решает только момент переключения (тайминг), а не применение паттерна.
 * - Поддерживает QuantizeMode::None/Beat/Bar.
 * - Обеспечивает детерминированный switch по sampleTime транспорта.
 *
 * Потоковая модель:
 * - control-thread: requestSwitch()
 * - RT-thread: onTransport() и popReadySwitch()
 *
 * Почему это отдельная сущность:
 * - отделяет "когда переключить" от "что переключить";
 * - уменьшает влияние джиттера UI/control слоя;
 * - упрощает тестирование квантизации независимо от движка/рендера.
 */
class PatternScheduler final : public IPatternScheduler {
public:
    /**
     * @brief Создать планировщик с заданной sample rate.
     * @param sampleRate Частота дискретизации для вычисления границ Beat/Bar.
     * @note Если передано невалидное значение, используется 48000.0.
     */
    explicit PatternScheduler(double sampleRate = 48000.0) noexcept;

    /**
     * @brief Запросить переключение на целевой паттерн.
     * @param req Запрос с target id и режимом квантизации.
     *
     * Поведение:
     * - Сохраняется только последний запрос (last-write-wins).
     * - Само переключение не выполняется здесь; только ставится заявка.
     *
     * Потоки:
     * - Вызывается из control-thread.
     */
    void requestSwitch(const PatternSwitchRequest& req) noexcept override;
    /**
     * @brief Забрать готовое к применению переключение (RT poll).
     * @param outPatternId Возвращает id паттерна, если switch готов.
     * @return true если готовый switch был и выдан; иначе false.
     *
     * Потоки:
     * - Вызывается из RT-thread.
     */
    bool popReadySwitch(PatternId& outPatternId) noexcept override;
    /**
     * @brief Обновить внутренний таймер планировщика по состоянию транспорта.
     * @param transport RT-снапшот транспорта на текущий момент.
     *
     * Поведение:
     * - Подхватывает новые control-запросы.
     * - Вычисляет/проверяет dueSample для quantized switch.
     * - Переводит switch в состояние "ready", когда наступила граница.
     *
     * Потоки:
     * - Вызывается из RT-thread (обычно в прологе аудио-блока).
     */
    void onTransport(const TransportRtSnapshot& transport) noexcept override;

private:
    /**
     * @brief Перевести квант (beat/bar) в длину в сэмплах.
     * @param snap Снапшот транспорта (bpm, time signature).
     * @param quant Режим квантизации.
     * @param sampleRate Частота дискретизации.
     * @return Размер кванта в сэмплах (минимум 1).
     */
    static uint64_t computeQuantumSamples(const TransportRtSnapshot& snap,
                                          QuantizeMode quant,
                                          double sampleRate) noexcept;
    /**
     * @brief Вычислить ближайший dueSample для выполнения switch.
     * @param now Текущая позиция транспорта в сэмплах.
     * @param snap Снапшот транспорта.
     * @param quant Режим квантизации.
     * @param sampleRate Частота дискретизации.
     * @return Абсолютный sampleTime, когда switch должен сработать.
     */
    static uint64_t computeDueSample(uint64_t now,
                                     const TransportRtSnapshot& snap,
                                     QuantizeMode quant,
                                     double sampleRate) noexcept;

    /**
     * @brief Упаковать PatternSwitchRequest в 64-битное mailbox значение.
     * @param req Запрос переключения.
     * @return Упакованное значение для атомарной передачи control->RT.
     */
    static uint64_t packRequest_(const PatternSwitchRequest& req) noexcept;
    /**
     * @brief Распаковать mailbox-значение обратно в PatternSwitchRequest.
     * @param packed Упакованное 64-битное значение.
     * @return Восстановленный запрос переключения.
     */
    static PatternSwitchRequest unpackRequest_(uint64_t packed) noexcept;

private:
    /**
     * @brief Частота дискретизации для math квантизации.
     *
     * Используется в computeQuantumSamples()/computeDueSample().
     */
    double sampleRate_{48000.0};

    /**
     * @brief Mailbox payload control->RT с последним switch-запросом.
     *
     * Формат:
     * - lower bits: PatternId target
     * - higher bits: QuantizeMode
     */
    std::atomic<uint64_t> requestData_{0};
    /**
     * @brief Версия mailbox для детекции новых заявок на RT стороне.
     *
     * Каждый requestSwitch() увеличивает sequence; RT сравнивает с seenSeq_.
     */
    std::atomic<uint32_t> requestSeq_{0};

    /**
     * @brief Последняя версия requestSeq_, которую обработал RT.
     * @note RT-only состояние.
     */
    uint32_t seenSeq_{0};
    /**
     * @brief Флаг, что есть отложенный quantized switch.
     * armed_ = true означает: “есть отложенный quantized switch, ждем момент dueSample_”.
     * armed_ = false означает: ждать нечего.
     * @note RT-only состояние.
     */
    bool armed_{false};
    /**
     * @brief Абсолютный sampleTime, когда отложенный switch должен сработать.
     * @note RT-only состояние.
     */
    uint64_t dueSample_{0};
    /**
     * @brief Target pattern для armed switch.
     * @note RT-only состояние.
     */
    PatternId dueTarget_{kInvalidPatternId};
    /**
     * @brief Готовый к выдаче switch (single-slot ready queue).
     * @note RT-only состояние.
     */
    PatternId ready_{kInvalidPatternId};
};

} // namespace avantgarde
