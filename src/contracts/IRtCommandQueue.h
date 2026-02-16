#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <contracts/types.h>


namespace avantgarde {

/** Политика переполнения кольцевого буфера. */
    enum class RtQueueOverflow : uint8_t { DropLatest, OverwriteOldest, FailWithFlag };


// Очередь SPSC: один продюсер (Control), один консюмер (RT)
    struct IRtCommandQueue {
        virtual ~IRtCommandQueue() = default;
        /** Положить команду; возвращает false, если политика == FailWithFlag и буфер полон. */
        virtual bool push(const RtCommand& cmd) noexcept = 0; // producer
        /** Забрать команду; возвращает false, если пусто. */
        virtual bool pop(RtCommand& out) noexcept = 0; // consumer
        /** Сбросить все команды (вне RT, например при stop). */
        virtual void clear() noexcept = 0;
        /** Текущие счётчики для телеметрии (читаются из Service). */
        virtual std::size_t capacity() const noexcept = 0;
        virtual std::size_t size() const noexcept = 0;
        virtual bool overflowFlagAndReset() noexcept = 0; // для телеметрии
    };

}
// ---------------------------------------------------------------------------
// Протокол полезной нагрузки RtCommand (для новых CmdId, секвенсер/транспорт)
// ---------------------------------------------------------------------------
//
// Общий формат:
//   RtCommand { id, track, slot, index, value }
//
// 1) Continue
//   - Глобальная команда транспорта; track = -1, slot = -1; index/value игнорируются.
//
// 2) SetTempoBpm
//   - track = -1, slot = -1; value = BPM (float, допускаем 20..300), index игнорируется.
//   - В RT хранение точного BPM (double) остаётся задачей движка (value — «носитель»).
//
// 3) SetTimeSig
//   - track = -1, slot = -1;
//   - value = числитель (num) как float (будет приведён к ближайшему целому ≥1),
//   - index = знаменатель (den) как uint16_t (ожидаемые значения: 1,2,4,8,16).
//
// 4) SetLoopRegion
//   - track = -1, slot = -1;
//   - Передача границ в сэмплах без расширения RtCommand:
//       index = (startSamples & 0xFFFF)  // младшие 16 бит
//       value = reinterpret_as_float(endSamples_low32)  // low 32 бита через побитовую упаковку
//   - Примечание: это временный «хук». В будущей версии возможно добавим расширенный пакет.
//   - Альтернатива (упрощённо для MVP): использовать теги сервиса через IEventBus.
//
// 5) NoteOn
//   - track = TrackId (>=0), slot = -1,
//   - index = клавиша (0..127),
//   - value = velocity [0..1].
//
// 6) NoteOff
//   - track = TrackId (>=0), slot = -1,
//   - index = клавиша (0..127),
//   - value игнорируется.
//
// 7) ClipTrigger
//   - track = TrackId (>=0), slot = -1,
//   - index = clipId (семантика задаётся секвенсором/UI),
//   - value = [0..1] необязательный аргумент интенсивности/варианта.
// --------------------------------------------------------------------------
