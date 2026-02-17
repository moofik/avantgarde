// include/contracts/ITransport.h
#pragma once
#include <cstdint>
#include <type_traits>

namespace avantgarde {


/**
 * QuantizeMode
 *
 * Режим квантизации для транспортно-зависимых команд
 * (StopQuantized, PatternSwitchQuantized и т.п.).
 */
    enum class QuantizeMode : uint8_t {
        None = 0,  // выполнять немедленно
        Beat = 1,  // выполнять на ближайшей границе доли
        Bar  = 2   // выполнять на ближайшей границе такта
    };

/**
 * TransportRtSnapshot
 *
 * RT-only снапшот состояния транспорта.
 *
 * ВАЖНО:
 *  - Структура POD и trivially copyable.
 *  - Читается в RT после swapBuffers().
 *  - Значения валидны в пределах текущего аудиоблока.
 *  - Не содержит указателей или динамической памяти.
 */
    struct TransportRtSnapshot {

        /**
         * playing
         *
         * true  → транспорт находится в режиме воспроизведения.
         * false → транспорт остановлен.
         *
         * Используется в RT для:
         *  - запуска/остановки секвенсора
         *  - активации записи клипов
         *  - расчёта квантизации
         */
        bool playing;

        /**
         * tsNum (time signature numerator)
         *
         * Числитель размера такта.
         * Например:
         *   4 в размере 4/4
         *   3 в размере 3/4
         *
         * Используется для:
         *  - вычисления длины такта
         *  - определения границы Bar при квантизации
         */
        uint8_t tsNum;

        /**
         * tsDen (time signature denominator)
         *
         * Знаменатель размера такта.
         * Например:
         *   4 в размере 4/4
         *   8 в размере 6/8
         *
         * В v1 может использоваться только для формальной поддержки 4/4.
         * В более продвинутой версии влияет на расчёт beat/bar.
         */
        uint8_t tsDen;

        /**
         * ppq (pulses per quarter note)
         *
         * Разрешение музыкального времени.
         * Количество тиков на одну четверть.
         *
         * Пример:
         *   96  → стандартное MIDI-разрешение
         *   192 → более точная сетка
         *
         * Используется для:
         *  - секвенсора
         *  - микротайминга
         *  - расчёта swing
         *
         * В v1 может быть фиксированным.
         */
        uint16_t ppq;

        /**
         * bpm (beats per minute)
         *
         * Темп композиции.
         *
         * Используется для:
         *  - перевода музыкального времени (beat/bar)
         *    в абсолютное время (samples)
         *  - расчёта длины клипов в семплах
         *  - квантизации
         *
         * Допустимо изменение во время воспроизведения.
         */
        float bpm;

        /**
         * quant
         *
         * Текущий режим квантизации.
         *
         * Определяет, как обрабатывать команды:
         *  - None → немедленно
         *  - Beat → на границе ближайшей доли
         *  - Bar  → на границе ближайшего такта
         *
         * Используется Scheduler'ом.
         */
        QuantizeMode quant;

        /**
         * swing
         *
         * Глобальный коэффициент свинга (0.0 – 1.0).
         *
         * 0.0 → свинг отключён (ровная сетка)
         * 0.5 → умеренный свинг
         *
         * Используется секвенсором или Scheduler'ом
         * для смещения каждой второй доли.
         *
         * В v1 может быть неактивен.
         */
        float swing;

        /**
         * sampleTime
         *
         * Абсолютная позиция транспорта в семплах.
         *
         * RT-owned:
         *  - увеличивается только в RT через advanceSampleTime().
         *  - монотонно возрастает.
         *
         * Используется для:
         *  - вычисления текущего beat/bar
         *  - определения границ квантизации
         *  - синхронизации клипов
         *
         * ВАЖНО:
         *  - НЕ устанавливается Control-слоем.
         *  - Не сбрасывается при остановке, если не реализован rewind.
         */
        uint64_t sampleTime;
    };

    static_assert(std::is_trivially_copyable_v<TransportRtSnapshot>,
                  "TransportRtSnapshot must be POD for RT safety");

    struct ITransportBridge {
        virtual ~ITransportBridge() = default;

        // Control-side: задать параметры транспорта (часто, неблокирующе).
        virtual void setPlaying(bool on) = 0;
        virtual void setTempo(float bpm) = 0;
        virtual void setTimeSignature(uint8_t num, uint8_t den) = 0;
        virtual void setQuantize(QuantizeMode q) = 0;
        virtual void setSwing(float s01) = 0;

        // RT-side: вызывается строго в прологе блока, как ParamBridge::swapBuffers()
        virtual void swapBuffers() noexcept = 0;

        // RT-side: взять актуальный снапшот (валиден до следующего swap)
        virtual const TransportRtSnapshot& rt() const noexcept = 0;

        // RT-side: апдейт sampleTime (только RT увеличивает)
        virtual void advanceSampleTime(uint64_t frames) noexcept = 0;
    };

} // namespace avantgarde
