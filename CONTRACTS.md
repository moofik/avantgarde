# Avantgarde — API Контракты (полная версия, C++17)
Версия: 1.0 • Язык: C++17 • Пространство имён: `avantgarde`

Документ фиксирует стабильные **заголовочные интерфейсы** (контракты) аудио‑устройства Avantgarde. Контракты описывают «что делает» компонент и **RT‑ограничения**, позволяя реализовывать модули, платформенные драйверы и сервисы независимо.

---

## Содержание
1. Принципы и правила RT
2. Базовые типы (`types.h`)
3. Параметры модулей (`IParameterized.h`)
4. DSP‑модули (`IAudioModule.h`)
5. Фабрика модулей (`IModuleFactory.h`)
6. Трек и FX‑цепочка (`ITrack.h`)
7. Аудио‑движок RT (`IAudioEngine.h`)
8. Поверхность управления (`IControlSurface.h`) и обработчик (`IControlHandler.h`)
9. Мост параметров (`IParamBridge.h`)
10. Хранилище проекта (`IProjectStore.h`)
11. Шины и очереди: RT‑команды (`IRtCommandQueue.h`) 
12. Шины и очереди: сервисная шина (`IEventBus.h`)
13. Платформенный слой: `IAudioHost` / `IAudioStream` (`IPlatform.h`)
14. Единые реестры: `CmdId`, `Topic`
15. Жизненный цикл и потоки
16. Рекомендации по реализации
17. Пример привязки движка к платформенному аудио
18. Аудио-рекордер
19. Аудио‑граф и ноды (контракты)
20. RT-хуки (`IRtExtension.h`)
21) Cэмплер/Клип-рекордер (`IClipTrack.h`)

---

## 1) Принципы и правила RT
- В `process()`/`processBlock()` запрещены аллокации, лока, исключения и системные вызовы.
- Все структуры, пересекающие границу RT, — POD, фиксированного размера.
- Control ↔ RT общаются через **узкую SPSC‑очередь** команд. Сервисные события идут через **pub/sub** шину.
- Все значения параметров нормализованы в `[0..1]` (физика описывается метаданными).

---

## 2) `types.h` — базовые типы и структуры
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <type_traits>

namespace avantgarde {

// Константы по умолчанию (могут переопределяться конфигом)
static constexpr int   kMaxTracks      = 4;
static constexpr int   kMaxFxPerTrack  = 8;
static constexpr float kParamMin       = 0.0f;
static constexpr float kParamMax       = 1.0f;
static constexpr int   kDoubleTapMs    = 250;
static constexpr int   kLongPressMs    = 700;

// Типы жестов UI
enum class PressType : uint8_t { Short, Long, Double, Combo };
// Источник события UI
enum class ControlType : uint8_t { Button, Encoder, Pot };

// Адрес назначения параметров/команд: (трек, слот FX), -1 = глобальный/мастер
struct Target { int trackId; int slotId; };

// Метаданные параметра для UI и сериализации
struct ParamMeta {
    std::string name;
    float       minValue;     // физический минимум
    float       maxValue;     // физический максимум
    bool        logarithmic;  // true для лог‑масштаба
    std::string unit;         // "ms", "Hz", "dB", "%" и т.п.
};

// Описание модуля (вне RT) — для UI/пресетов
struct ModuleDescriptor {
    std::string id;           // "fx.delay.basic"
    std::string name;         // имя для UI
    std::size_t numParams;    // param[0..N)
    uint32_t    version;      // версия схемы параметров (миграции)
};

// Универсальная команда для движка/лупера (вне RT составляется, в RT исполняется)
struct Command {
    std::string name; // "play","stop","rec","overdub","clear","mute","solo","quantize"
    Target      target;
    float       value; // 0/1 или произвольное число
};

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
    const float** in;        // [numIn][nframes]
    float**       out;       // [numOut][nframes]
    std::size_t   nframes;   // 128/256/512 и т.п.
};
static_assert(std::is_trivially_copyable<AudioProcessContext>::value, "ctx must be POD");

// Сырой UI‑ивент с таймстемпом для детерминизма
struct ControlEvent {
    ControlType      type;   // Button/Encoder/Pot
    int              id;     // локальный ID элемента
    float            value;  // Button:[0..1], Encoder:±шаг, Pot:[0..1]
    PressType        press;  // Short/Long/Double/Combo
    uint64_t         tsMono; // монотонное время (нс)
    std::vector<int> combo;  // список ID при Combo (только вне RT)
};

} // namespace avantgarde
```

---

## 3) `IParameterized.h` — унификация параметров
```cpp
#pragma once
#include <cstddef>
#include "types.h"

namespace avantgarde {

// Единый интерфейс доступа к параметрам модулей. Значения нормализованы [0..1].
struct IParameterized {
    virtual ~IParameterized() = default;
    virtual std::size_t getParamCount() const = 0;
    virtual float getParam(std::size_t index) const = 0;         // O(1), RT‑safe
    virtual void  setParam(std::size_t index, float value) = 0;   // запись в локальный кэш
    virtual const ParamMeta& getParamMeta(std::size_t index) const = 0; // вне RT
};

} // namespace avantgarde
```

---

## 4) `IAudioModule.h` — DSP‑модуль (эффект/процессор)
```cpp
#pragma once
#include "IParameterized.h"

namespace avantgarde {

// Любой FX/процессор графа. Вся подготовка — в init(); process() — строго RT‑safe.
struct IAudioModule : IParameterized {
    virtual ~IAudioModule() = default;
    virtual void init(double sampleRate, std::size_t maxFrames) = 0; // вне RT
    virtual void process(const AudioProcessContext& ctx) = 0;         // RT, no‑throw
    virtual void reset() = 0;                                         // сброс внутренних состояний
};

} // namespace avantgarde
```

---

## 5) `IModuleFactory.h` — фабрика модулей
```cpp
#pragma once
#include <memory>
#include <string>
#include "IAudioModule.h"

namespace avantgarde {

// Создание модулей по строковому id; позволяет подменять реализации (builtin/FAUST/LV2).
struct IModuleFactory {
    virtual ~IModuleFactory() = default;
    virtual std::unique_ptr<IAudioModule> create(const std::string& id) = 0;         // вне RT
    virtual const ModuleDescriptor& getDescriptor(const std::string& id) const = 0;  // вне RT
};

} // namespace avantgarde
```

---

## 6) `ITrack.h` — трек и FX‑цепочка
```cpp
#pragma once
#include <memory>
#include <vector>
#include "IAudioModule.h"

namespace avantgarde {

// Трек владеет модулями и последовательно прогоняет через них сигнал.
struct ITrack {
    virtual ~ITrack() = default;
    virtual void addModule(std::unique_ptr<IAudioModule> mod) = 0;        // вне RT
    virtual IAudioModule* getModule(std::size_t index) = 0;               // конфигурация/снапшоты
    virtual void process(const AudioProcessContext& ctx) = 0;             // RT‑safe
    // NEW: узкое RT-API для адресных команд (ParamSet, NoteOn/Off, ClipTrigger и т.п.)
    virtual void onRtCommand(const RtCommand& cmd) noexcept = 0;   // RT-safe, без аллокаций
};

} // namespace avantgarde
```

---

## 7) `IAudioEngine.h` — центральное RT‑ядро
```cpp
#pragma once
#include <memory>
#include "ITrack.h"
#include "types.h"

namespace avantgarde {

// Аудио‑движок управляет треками, RT-командами и обработкой блока.
struct IAudioEngine {
    virtual ~IAudioEngine() = default;

    // Регистрация треков до старта аудио (вне RT)
    virtual void registerTrack(std::unique_ptr<ITrack> track) = 0;

    // Обработка одного блока; вызывается из платформенного аудио‑колбэка
    virtual void processBlock(const AudioProcessContext& ctx) = 0;

    // Установка частоты дискретизации до init модулей
    virtual void setSampleRate(double sr) = 0;

    // Команда луперу/транспорту (play/stop/rec/overdub/mute/solo/quantize)
    virtual void onCommand(const Command& cmd) = 0; // вне RT → попадёт в RT очередь

    // Привязка платформенного аудио‑хоста (для телеметрии/настроек)
    virtual void setAudioHost(std::shared_ptr<void> host) noexcept = 0; // тип стирается в контракте

    // Регистрация RT-хуков
    virtual void addRtExtension(IRtExtension* ext) noexcept = 0; // регистрируем до старта стрима
};

} // namespace avantgarde
```

---

## 8) `IControlSurface.h` и `IControlHandler.h` — UI
```cpp
#pragma once
#include "types.h"

namespace avantgarde {

// Источник событий UI: опрос кнопок/энкодеров/потов. poll() неблокирующий.
struct IControlSurface {
    virtual ~IControlSurface() = default;
    virtual bool poll(ControlEvent& outEvent) noexcept = 0; // true, если событие получено
    virtual void attachEventBus(void* bus) noexcept = 0;    // слабая привязка к сервисной шине
};

// Обработчик жестов: перевод ControlEvent → команды/параметры/события UI.
struct IControlHandler {
    virtual ~IControlHandler() = default;
    virtual void handle(const ControlEvent& ev) = 0; // вызывает onCommand()/pushParam()/publish()
};

} // namespace avantgarde
```

---

## 9) `IParamBridge.h` — мост параметров Control → RT
```cpp
#pragma once
#include <cstddef>
#include "types.h"

namespace avantgarde {

// Двойной буфер: Control пишет часто в write‑сторону; RT в прологе блока делает swap.
struct IParamBridge {
    virtual ~IParamBridge() = default;
    virtual void pushParam(Target target, std::size_t index, float value) = 0; // write‑side
    virtual void swapBuffers() = 0; // вызывается строго в прологе RT‑блока
};

} // namespace avantgarde
```

---

## 10) `IProjectStore.h` — сохранение/загрузка проекта
```cpp
#pragma once
#include <string>

namespace avantgarde {

struct ProjectState; // forward‑declare, чтобы не тянуть зависимости

// Хранилище проекта: сериализация графа, параметров и WAV/аудио‑данных.
struct IProjectStore {
    virtual ~IProjectStore() = default;
    virtual void save(const ProjectState& state, const std::string& path) = 0; // вне RT
    virtual ProjectState load(const std::string& path) = 0;                    // вне RT
    virtual void attachAudioHost(void* host) noexcept = 0; // для выбора устройства/пути
};

} // namespace avantgarde
```

---

## 11) `IRtCommandQueue.h` — узкая SPSC‑очередь для RT‑команд
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace avantgarde {

enum class RtQueueOverflow : uint8_t { DropLatest, OverwriteOldest, FailWithFlag };

// Очередь SPSC: один продюсер (Control), один консюмер (RT)
struct IRtCommandQueue {
    virtual ~IRtCommandQueue() = default;
    virtual bool push(const RtCommand& cmd) noexcept = 0; // producer
    virtual bool pop(RtCommand& out) noexcept = 0;         // consumer
    virtual void clear() noexcept = 0;
    virtual std::size_t capacity() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual bool overflowFlagAndReset() noexcept = 0;      // для телеметрии
};

} // namespace avantgarde
```

---

## 12) `IEventBus.h` — сервисная шина событий (pub/sub, не‑RT)
```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <memory>

namespace avantgarde {

using TopicId = uint32_t; // см. раздел 13

// Type‑erased конверт события для шины (вне RT)
struct EventEnvelope {
    TopicId     topic;
    const void* payload;    // указатель на T; владение/копия определяются реализацией
    std::size_t payloadLen; // размер T
    uint64_t    tsMono;     // монотонное время публикации
};

struct Subscription { virtual ~Subscription() = default; virtual void unsubscribe() = 0; };
using SubscriptionPtr = std::unique_ptr<Subscription>;

struct IEventBus {
    virtual ~IEventBus() = default;
    virtual void publish(const EventEnvelope& ev) = 0; // вызывается из Service‑потока
    virtual SubscriptionPtr subscribe(TopicId topic,
        std::function<void(const EventEnvelope&)> callback) = 0;   // колбэк в Service‑потоке
    virtual void setSticky(TopicId topic, const EventEnvelope& last) = 0; // последнее значение темы
    virtual bool getSticky(TopicId topic, EventEnvelope& out) const = 0;
    virtual uint64_t totalPublished() const = 0;
    virtual uint64_t totalDelivered() const = 0;
};

} // namespace avantgarde
```

---

## 13) Платформенный слой: `IAudioHost` / `IAudioStream` (файл `IPlatform.h`)
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "types.h"

namespace avantgarde {

// Описание устройств платформы (CoreAudio / ALSA / JACK / PipeWire)
struct AudioDeviceInfo {
    std::string id;              // "default", "BuiltInOutput", "hw:0,0", ...
    std::string name;            // человекочитаемое
    int maxInput = 0;
    int maxOutput = 2;
    int defaultSampleRate = 48000;
    bool isDefault = false;
};

// Конфигурация потока: частота, размер блока, каналы
struct StreamConfig {
    int sampleRate  = 48000;
    int blockFrames = 256;   // предпочтительно степень двойки
    int numInput    = 0;
    int numOutput   = 2;
};

// Колбэк рендера. Вызывается из аудио‑нити. Никаких аллокаций или исключений.
using AudioRenderCb = void(*)(AudioProcessContext& ctx, void* user) noexcept;
// Не‑RT уведомления (xrun/ошибки). Вызываются из сервисного потока.
using NonRtNotifyCb = void(*)(int code, const char* msg, void* user) noexcept;

struct IAudioStream {
    virtual ~IAudioStream() = default;
    virtual bool start(AudioRenderCb render, void* user) noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void close() noexcept = 0;
    virtual int  sampleRate()  const noexcept = 0;
    virtual int  blockFrames() const noexcept = 0;
    virtual int  numInput()    const noexcept = 0;
    virtual int  numOutput()   const noexcept = 0;
    virtual uint64_t totalCallbacks() const noexcept = 0;
    virtual uint64_t xruns()          const noexcept = 0;
};

struct IAudioHost {
    virtual ~IAudioHost() = default;
    virtual std::vector<AudioDeviceInfo> enumerate() = 0; // вне RT
    virtual std::unique_ptr<IAudioStream> openStream(const StreamConfig& cfg,
                                                     const std::string& inputDeviceId,
                                                     const std::string& outputDeviceId,
                                                     NonRtNotifyCb onNotify = nullptr,
                                                     void* notifyUser = nullptr) = 0; // вне RT
};

} // namespace avantgarde
```

---

## 14) Единые реестры: `CmdId`, `Topic`
```cpp
#pragma once
#include <cstdint>

namespace avantgarde {

// Команды RT‑ядра (используются в RtCommand.id)
enum class CmdId : uint16_t {
    Play = 1,
    Stop,
    StopQuantized,
    RecArm,
    RecDisarm,
    Overdub,
    ParamSet,
    Clear,
    QuantizeMode
};

// Темы сервисной шины (используются в EventBus.TopicId)
enum class Topic : uint32_t {
    UiStatus           = 1001, // транспорт, BPM, quant
    UiBanner           = 1002, // всплывающие сообщения
    UiPage             = 1003, // текущая страница/FX
    MetersUpdate       = 2001, // уровни/пики
    PowerBatteryLow    = 3001, // питание
    ProjectSaveRequest = 4001,
    ProjectSaveDone    = 4002,
    TelemetryRtAlert   = 5001  // переполнения, xruns
};

} // namespace avantgarde
```

---

## 15) Жизненный цикл и потоки (кратко)
- **Service Thread**: исполняет подписчиков `IEventBus`; фан‑ин внешних сигналов; публикует события; шлёт запросы в Control через SPSC‑почтовый ящик.
- **Control Thread**: опрашивает `IControlSurface::poll()`, ведёт FSM транспорта/страниц, публикует статусы в `IEventBus`, пушит команды в `IRtCommandQueue`.
- **Audio RT Thread**: принадлежит `IAudioStream`; вызывает `AudioRenderCb` → `IAudioEngine::processBlock(ctx)`; в прологе читает `IRtCommandQueue`, делает `IParamBridge::swapBuffers()`.

---

## 16) Рекомендации по реализации
- Размеры блоков: CoreAudio 128–256, ALSA 256–512, JACK/PipeWire кратные 64.
- Очереди SPSC: размер степенью двойки; политика переполнения — заранее фиксирована.
- Телеметрия: счётчики `xruns`, `overflowFlag`, `totalCallbacks`, публикация алертов в `Topic::TelemetryRtAlert`.
- Снапшоты/пресеты: `ModuleDescriptor.version` + миграции `vN→vN+1`.

---

## 17) Пример привязки движка к платформенному аудио
```cpp
using namespace avantgarde;

static void RenderThunk(AudioProcessContext& ctx, void* user) noexcept {
    auto* engine = static_cast<IAudioEngine*>(user);
    engine->processBlock(ctx);
}

void run(IAudioHost& host, IAudioEngine& engine) {
    StreamConfig cfg{ .sampleRate=48000, .blockFrames=256, .numInput=2, .numOutput=2 };
    auto stream = host.openStream(cfg, "default", "default");
    stream->start(&RenderThunk, &engine);
    // ... работа устройства ...
    stream->stop();
    stream->close();
}
```

### 18. Аудио-рекордер (`IAudioRecorder.h`)
```cpp
// include/contracts/IAudioRecorder.h
#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace avantgarde {
    
    // Конфигурация записи
    struct RecordConfig {
        int sampleRate = 48000;
        int channels   = 2;     // 1=mono, 2=stereo (non-interleaved в RT)
        int bitDepth   = 24;    // физический файл: 16/24/32f
        std::string format = "wav"; // "wav" | "flac" (MVP: "wav")
    };
    
    // RT-часть: вызывается из аудио-рендера. Никаких аллокаций/исключений/блокировок.
    struct IRtRecordSink {
    virtual ~IRtRecordSink() = default;
    
        // Пишем один блок неинтерливнутых каналов: [channels][nframes]
        // Возвращает false если внутренний кольцевой буфер переполнен (дроп кадра допустим).
        virtual bool writeBlock(const float* const* ch, int nframes) noexcept = 0;
    
        // Опционально — отметка тактов/локаторов (без формата файла, просто события)
        virtual void mark(uint32_t code) noexcept = 0;
    };
    
    // Non-RT контроллер: управляет файлом и потоками записи.
    struct IAudioRecorder {
    virtual ~IAudioRecorder() = default;
    
        // Подготовка/открытие файла. Создаёт внутренний предвыделенный ringbuffer.
        // Путь обычно даёт IProjectStore (папка проекта).
        virtual bool start(const std::string& filePath, const RecordConfig& cfg) = 0;
    
        // Остановка и финализация контейнера (запись заголовков, flush).
        virtual void stop() = 0;
    
        virtual bool isRecording() const noexcept = 0;
    
        // Доступ к RT-синку. Получаем один раз после start() и кэшируем в RT.
        virtual IRtRecordSink* rtSink() noexcept = 0;
    
        // Статистика/диагностика (вне RT)
        virtual uint64_t totalFramesWritten() const noexcept = 0;
        virtual uint64_t droppedBlocks() const noexcept = 0;
    };

} // namespace avantgarde
```

---

## 19) Аудио‑граф и ноды (контракты)
Цель: дать простой и понятный контракт для маршрутизации аудио **без лишних сущностей на публичной поверхности**. Все сложные детали (буферы/план/алокации) остаются внутри реализации графа.

### 19.1 Минимальные типы графа (`graph_types.h`)
```cpp
#ifndef AVANTGARDE_CONTRACTS_GRAPH_TYPES_H
#define AVANTGARDE_CONTRACTS_GRAPH_TYPES_H

#include <cstdint>
#include <cstddef>


/**
 * graph_types.h — базовые типы и инварианты для топологии DSP-графа.
 *
 * Назначение:
 *   Этот файл содержит POD-структуры (plain-old data), которые описывают
 *   топологию графа в "плоском" виде: список узлов (nodes) и рёбер (edges).
 *   Такие структуры безопасно использовать:
 *     • в RT-коде (только чтение уже зафиксированных массивов),
 *     • в кодеке сериализации/десериализации,
 *     • в тестах/моках для детерминируемых проверок.
 *
 * Дизайн-принципы:
 *   1) Никаких динамических аллокаций, строк и исключений в RT-пути.
 *   2) Детерминированный порядок: внешние представления сортируются по ID.
 *   3) Значения параметров узлов отдельно (ParamBridge); граф хранит только форму/связи.
 */
namespace avantgarde {

// Лимиты по умолчанию (если нужны другие — правим их в одном месте).
    inline constexpr std::uint32_t kMaxNodes          = 64;
    inline constexpr std::uint32_t kMaxParamsPerNode  = 32;

/** Типы идентификаторов (числовые, стабильные для сериализации/RT). */
    using NodeId   = std::uint16_t;   // уникален в пределах проекта
    using NodeKind = std::uint16_t;   // код из внешнего реестра типов (Input, FX и т.п.)

/**
 * Описание узла графа в плоском виде.
 * Инварианты:
 *  • id — уникален;
 *  • paramCount ∈ [0..kMaxParamsPerNode];
 *  • kind — внешний тип (маппинг решает приложение).
 */
    struct GraphNodeDesc {
        NodeId            id;
        NodeKind          kind;
        std::uint16_t     paramCount;
        std::uint16_t     reserved{0}; // выравнивание/расширение без слома ABI
    };

/**
 * Ориентированное ребро графа.
 * Инварианты:
 *  • fromId/toId существуют; fromId != toId;
 *  • без дублей; итоговый граф — DAG.
 */
    struct GraphEdgeDesc {
        NodeId fromId;
        NodeId toId;
    };

/**
 * Окно для массового чтения/записи топологии.
 * Вызвавший код предоставляет внешние буферы и их вместимость.
 * Реализация IGraph заполняет их и выставляет фактические счётчики.
 */
    struct GraphTopoView {
        GraphNodeDesc*    nodes;      // [out] вместимость >= *nodeCount
        GraphEdgeDesc*    edges;      // [out] вместимость >= *edgeCount
        std::uint16_t*    nodeCount;  // [in/out] capacity -> filled
        std::uint16_t*    edgeCount;  // [in/out] capacity -> filled
    };

/**
 * Требования к корректности топологии:
 *  1) Уникальные NodeId.
 *  2) Параметры узлов в лимите kMaxParamsPerNode.
 *  3) Рёбра ссылаются на существующие узлы, без петель.
 *  4) Нет дублей рёбер.
 *  5) Граф — DAG (топологически сортируем).
 *
 * Детерминизм внешнего представления:
 *  • Узлы отсортированы по id по возрастанию.
 *  • Рёбра отсортированы по (fromId, toId).
 */

} // namespace avantgarde

#endif // AVANTGARDE_CONTRACTS_GRAPH_TYPES_H
```

### 19.2 Граф (`IAudioGraph.h`)
```cpp
#pragma once
#include <memory>
#include <vector>
#include "IAudioNode.h"

namespace avantgarde {

// Граф управляет нодами/соединениями и сам занимается буферами внутри.
// Снаружи — никакого пула/ExecPlan. В RT исполняется заранее подготовленный внутренний план.
struct IAudioGraph {
    virtual ~IAudioGraph() = default;

    // Добавить ноду; возвращает NodeId (вне RT)
    virtual NodeId addNode(std::unique_ptr<IAudioNode> node) = 0;

    // Соединить выход узла a:o → вход узла b:i (вне RT)
    virtual void connect(NodeId a, PortIndex outPort, NodeId b, PortIndex inPort) = 0;

    // Разорвать соединение (вне RT)
    virtual void disconnect(NodeId a, PortIndex outPort, NodeId b, PortIndex inPort) = 0;

    // Подготовить исполняемое представление (топологический порядок, внутренняя раскладка буферов). Вне RT.
    virtual void prepare(double sampleRate, std::size_t maxFrames) = 0;

    // Выполнить граф в RT одним вызовом. Внутри используется подготовленный план и внутренний workspace.
    virtual void process(const AudioProcessContext& ctx) noexcept = 0;
};

} // namespace avantgarde
```

### 19.3 Граф (`IGraphCodec.h`)
```cpp
#ifndef AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H
#define AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H

#include <cstdint>
#include <cstddef>
#include "IAudioGraph.h"

/**
 * IGraphCodec — сериализация/десериализация топологии (вне RT).
 * Формат данных (JSON/CBOR/…) — деталь реализации; контракт фиксирует поведение.
 * Гарантии:
 *  • Детерминированный вывод (узлы по id, рёбра по (from,to)).
 *  • Сопоставимость через schemaTag.
 */
namespace avantgarde {

    struct GraphCodecConfig {
        const char*  schemaTag;   // например, "avantgarde.project@1" (nullptr -> значение по умолчанию)
        bool         prettyPrint; // форматировать вывод (необязательно)
    };

    class IGraphCodec {
    public:
        virtual ~IGraphCodec() = default;

        // IGraph → bytes (например, JSON).
        // Пишет в заранее выделенный буфер outBuf (ёмкость outCapacity), возвращает фактический размер outSize.
        virtual bool serialize(const IAudioGraph& graph,
                               const GraphCodecConfig& cfg,
                               char* outBuf, std::size_t outCapacity,
                               std::size_t& outSize) const noexcept = 0;

        // bytes → IGraph. Парсит/валидирует и применяет setTopology().
        virtual bool deserialize(const char* data, std::size_t size,
                                 const GraphCodecConfig& cfg,
                                 IAudioGraph& outGraph) const noexcept = 0;
    };

} // namespace avantgarde

/**
 * коротко и по делу — при схеме track1 → master, track2 → master граф у нас выполняет всего три задачи: декларация,
 * валидация и привязка ID. Никакой «магии маршрутизации» он не делает.
 *
 * Что делает граф в такой топологии
 *
 * Декларация состава микшера.
 *
 * Это просто список узлов (Track и один Master) и рёбра Track→Master. Он лежит в проекте и сериализуется детерминированно.
 * Валидация перед запуском
 *
 * Проверка: уникальные id, только разрешённые kind, только Track→Master, без дублей/петель. Это
 * ловит ошибки конфигурации ещё до RT.
 *
 * Привязка идентификаторов.
 * NodeId используется как стабильный ключ:
 * IAudioEngine::bindTrack(nodeId, ITrack*) — связать трек-объект с узлом графа;
 * адресация параметров через ParamBridge как (trackNodeId → baseOffset) + (moduleIndex, paramIndex).
 */

#endif // AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H

```

### 19.4 Что происходит «внутри», но не попадает в публичный контракт
- **Внутренний план (exec plan)** — это просто вектор операций «вызвать ноду N с такими входными/выходными каналами», отсортированный топологически. Он строится при `prepare()` и лежит приватно внутри реализации графа.
- **Рабочая область (workspace)** — предвыделённые звуковые буферы под промежуточные шины. Их размер зависит от `maxFrames` и количества временных рёбер. Управление ими полностью внутри графа.
- **Оптимизации**: aliasing (сквозная проводка без копии), повторное использование буферов, коалесcинг миксов.

### 19.5 Как это стыкуется с уже существующими интерфейсами
- В `IAudioEngine::processBlock(ctx)` движок просто вызывает `graph.process(ctx)`.
- Трековая FX‑цепочка может быть обёрнута в граф как линейный подграф (модули → ноды).
- Никаких публичных `IBufferPool` и `AudioBusView`: меньше API — меньше путаницы и рисков в RT.

Статус: упрощённая спецификация графа v1.0 (публичная поверхность очищена). Реализация графа живёт в `src/graph/` и скрывает детали плана и буферов. Реализации размещаются в `src/graph/` (топологическая сортировка, раскладка bus’ов, пул буферов, ноды‑адаптеры для FX‑модулей).

---

Статус: стабильная спецификация v1.0. Реализации размещаются в `src/runtime/`, `src/control/`, `src/service/`, `src/platform/` и `src/graph/`, тесты — в `test/` с моками по контрактам. Реализации размещаются в `src/runtime/`, `src/control/`, `src/service/`, `src/platform/` и тесты — в `test/` с моками по контрактам.


## 20) `IRtExtension.h` — RT-хуки
```cpp
// include/contracts/IRtExtension.h
#pragma once
#include "types.h"

namespace avantgarde {

struct IRtExtension {
virtual ~IRtExtension() = default;
virtual void onBlockBegin(const AudioProcessContext& ctx) noexcept = 0;
virtual void onBlockEnd(const AudioProcessContext& ctx) noexcept = 0;
};

} // namespace avantgarde
```

## 21) `IClipTrack.h` — базовые типы и структуры
```cpp
#pragma once
#include "ITrack.h"
#include <cstdint>

namespace avantgarde {

/**
 * IClipTrack
 *
 * Capability-интерфейс для трека, который содержит фиксированное
 * количество клип-слотов (Clip Slots) и умеет:
 *   - хранить аудиоматериал в слотах
 *   - воспроизводить его (через RT-команды)
 *   - записывать входной сигнал в слот
 *
 * ВАЖНО:
 *  - Все методы этого интерфейса вызываются ВНЕ RT.
 *  - Реальное воспроизведение / запись управляются через onRtCommand().
 *  - Память под слоты должна быть выделена вне RT.
 */
    struct IClipTrack : ITrack {
        virtual ~IClipTrack() = default;

        /**
         * Возвращает фиксированное количество слотов у трека.
         *
         * Требования:
         *  - Значение не должно меняться во время работы аудио.
         *  - Используется UI и ProjectStore для сериализации.
         *
         * RT-safe: да (но обычно вызывается вне RT).
         */
        virtual uint32_t numSlots() const noexcept = 0;

        /**
         * Загружает аудиофайл в указанный слот.
         *
         * Поведение:
         *  - Декодирование файла выполняется вне RT.
         *  - Память под аудиобуфер выделяется вне RT.
         *  - Старое содержимое слота (если было) освобождается.
         *
         * Ограничения:
         *  - Не должно вызываться во время активного воспроизведения
         *    этого слота (или реализация должна безопасно остановить его).
         *
         * @param slot индекс слота [0 .. numSlots()-1]
         * @param path путь к аудиофайлу
         *
         * @return true если загрузка успешна
         */
        virtual bool loadSlotFromFile(uint32_t slot, const char* path) = 0;

        /**
         * Очищает слот.
         *
         * Поведение:
         *  - Удаляет аудиоданные.
         *  - Сбрасывает состояние воспроизведения/записи.
         *
         * RT:
         *  - Не вызывается из RT.
         *
         * @param slot индекс слота
         * @return true если слот успешно очищен
         */
        virtual bool clearSlot(uint32_t slot) = 0;

        /**
         * Подготавливает слот к записи.
         *
         * Это НЕ запускает запись.
         *
         * Поведение:
         *  - Выделяет/подготавливает буфер записи.
         *  - Помечает слот как "armed".
         *  - Реальный старт записи происходит через RT-команду
         *    (например CmdId::ClipRecordToggle).
         *
         * Используется для:
         *  - подготовки памяти
         *  - проверки доступных ресурсов
         *
         * @param slot индекс слота
         * @param on   true — подготовить к записи, false — снять arm
         *
         * @return true если операция успешна
         */
        virtual bool armRecordSlot(uint32_t slot, bool on) = 0;

        /**
         * Устанавливает длину клипа в тактах (барах).
         *
         * Используется для:
         *  - записи фиксированной длины (например 4 бара)
         *  - синхронизации с транспортом
         *
         * Требует:
         *  - наличия Transport (BPM + time signature)
         *
         * Поведение:
         *  - Пересчитывает ожидаемую длину в семплах
         *    (на основе текущего sampleRate и BPM).
         *
         * RT:
         *  - Только вне RT.
         *
         * @param slot индекс слота
         * @param bars количество тактов (>=1)
         *
         * @return true если значение принято
         */
        virtual bool setSlotLengthInBars(uint32_t slot, uint32_t bars) = 0;

        /**
         * Устанавливает режим воспроизведения слота.
         *
         * loop = true  → клип зацикливается
         * loop = false → one-shot (проигрывается один раз)
         *
         * Влияние:
         *  - Определяет поведение при достижении конца буфера.
         *
         * RT:
         *  - Только вне RT.
         *
         * @param slot индекс слота
         * @param loop режим зацикливания
         *
         * @return true если операция успешна
         */
        virtual bool setSlotLooping(uint32_t slot, bool loop) = 0;
    };

} // namespace avantgarde
```
