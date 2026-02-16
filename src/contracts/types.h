#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <type_traits>

namespace avantgarde {


// Константы по умолчанию (могут переопределяться конфигом)
    static constexpr int kMaxTracks = 4;
    static constexpr int kMaxFxPerTrack = 8;
    static constexpr float kParamMin = 0.0f;
    static constexpr float kParamMax = 1.0f;
    static constexpr int kDoubleTapMs = 250;
    static constexpr int kLongPressMs = 700;


// Типы жестов UI
    enum class PressType : uint8_t { Short, Long, Double, Combo };
// Источник события UI
    enum class ControlType : uint8_t { Button, Encoder, Pot };


// Адрес назначения параметров/команд: (трек, слот FX), -1 = глобальный/мастер
    struct Target { int trackId; int slotId; };


// Метаданные параметра для UI и сериализации
    struct ParamMeta {
        std::string name;
        float minValue; // физический минимум
        float maxValue; // физический максимум
        bool logarithmic; // true для лог‑масштаба
        std::string unit; // "ms", "Hz", "dB", "%" и т.п.
    };

    struct ParamKV {
        uint16_t index;
        float    value;
    };
    
// Описание модуля (вне RT) — для UI/пресетов
    struct ModuleDescriptor {
        std::string id; // "fx.delay.basic"
        std::string name; // имя для UI
        std::size_t numParams; // param[0..N)
        uint32_t version; // версия схемы параметров (миграции)
    };


// Универсальная команда для движка/лупера (вне RT составляется, в RT исполняется)
    struct Command {
        std::string name; // "play","stop","rec","overdub","clear","mute","solo","quantize"
        Target target;
        float value; // 0/1 или произвольное число
    };

// Компактный POD‑пакет для RT‑команды; id — enum CmdId (см. раздел 13)
    struct RtCommand {
        uint16_t id; // CmdId
        int16_t track; // -1 = master
        int16_t slot; // FX‑слот или -1
        uint16_t index; // индекс параметра (для ParamSet)
        float value; // полезная нагрузка
    };
    static_assert(std::is_trivially_copyable<RtCommand>::value, "RtCommand must be POD");

// Контекст обработки одного аудио‑блока
    struct AudioProcessContext {
        const float** in; // [numIn][nframes]
        float** out; // [numOut][nframes]
        std::size_t nframes; // 128/256/512 и т.п.
    };
    static_assert(std::is_trivially_copyable<AudioProcessContext>::value, "ctx must be POD");

// Сырой UI‑ивент с таймстемпом для детерминизма
    struct ControlEvent {
        ControlType type; // Button/Encoder/Pot
        int id; // локальный ID элемента
        float value; // Button:[0..1], Encoder:±шаг, Pot:[0..1]
        PressType press; // Short/Long/Double/Combo
        uint64_t tsMono; // монотонное время (нс)
        std::vector<int> combo; // список ID при Combo (только вне RT)
    };

    struct TimeSig {
        std::uint16_t num{4}; // числитель, например 4
        std::uint16_t den{4}; // знаменатель, например 4
    };

    struct Tempo {
        double bpm{120.0};    // 120.0 BPM
    };

    struct TransportPos {
        std::uint64_t samplePos{0}; // абсолютная позиция в сэмплах (источник истины для RT)
        Tempo         tempo{};      // текущий темп
        TimeSig       sig{};        // размер
    };

} // namespace avantgarde