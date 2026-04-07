#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ITransport.h"
#include "types.h"

namespace avantgarde {

// Устойчивый идентификатор паттерна в рамках PatternBank.
using PatternId = uint16_t;
constexpr PatternId kInvalidPatternId = 0xFFFFu;

// Тип события шага секвенсора.
// Важно: это описание музыкального намерения; в RT оно маппится в RtCommand.
enum class PatternStepOp : uint8_t {
    Trigger = 0,   // триггер клипа/слота
    NoteOn = 1,    // нота с velocity
    NoteOff = 2,   // отпускание ноты
    NoteDetune = 3,// тонкая подстройка ноты (fine detune)
    ParamSet = 4   // параметр (трек/FX/глобальный)
};

// Снимок транспортных параметров, сохраняемый внутри паттерна.
// Не содержит sampleTime: живая временная позиция принадлежит только RT-транспорту.
struct PatternTransportSnapshot {
    float bpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{QuantizeMode::Bar};
    float swing01{0.0f};
};

// Снимок одного параметра FX в конкретном слоте.
struct PatternFxParam {
    uint8_t slot{0};
    uint16_t index{0};
    float value{0.0f};
};

// Снимок трека внутри паттерна.
// Храним только состояние/параметры (данные), но не DSP-инстансы.
struct PatternTrackSnapshot {
    uint8_t trackId{0};
    bool muted{false};
    bool armed{false};
    float gain01{1.0f};
    float playbackInc{1.0f};
    uint32_t bars{4};
    uint32_t clipRefId{0}; // стабильная ссылка на клип в проектном хранилище
    std::vector<ParamKV> trackParams{}; // TrackParamId -> value
    std::vector<PatternFxParam> fxParams{};
};

// Универсальное step-событие внутри паттерна.
// Поля index/value трактуются по op (например key/velocity, param/value и т.д.).
struct PatternStepEvent {
    uint32_t step{0};       // индекс шага в сетке паттерна
    uint8_t trackId{0};     // целевой трек
    int16_t slot{-1};       // FX-слот или -1 для трек/глобальных параметров
    PatternStepOp op{PatternStepOp::Trigger};
    uint16_t index{0};      // key/paramIndex/clipId
    float value{0.0f};      // velocity/paramValue
};

// Полное состояние паттерна.
struct PatternState {
    PatternId id{kInvalidPatternId};
    PatternTransportSnapshot transport{};
    uint32_t lengthInSteps{64};
    uint16_t stepsPerBeat{4};
    std::vector<PatternTrackSnapshot> tracks{};
    std::vector<PatternStepEvent> events{};
};

// Запрос на переключение паттерна с режимом квантизации.
struct PatternSwitchRequest {
    PatternId target{kInvalidPatternId};
    QuantizeMode quantize{QuantizeMode::Bar};
};

// Банк паттернов (service/control слой; не RT).
struct IPatternBank {
    virtual ~IPatternBank() = default;
    virtual std::size_t size() const noexcept = 0;
    virtual bool contains(PatternId id) const noexcept = 0;
    virtual bool get(PatternId id, PatternState& out) const = 0;
    virtual bool put(const PatternState& state) = 0;
    virtual bool erase(PatternId id) = 0;
};

// Планировщик переключения паттернов.
// Control кладет запросы, RT в прологе блока проверяет "готово ли к переключению".
struct IPatternScheduler {
    virtual ~IPatternScheduler() = default;
    virtual void requestSwitch(const PatternSwitchRequest& req) noexcept = 0; // control-thread
    virtual bool popReadySwitch(PatternId& outPatternId) noexcept = 0;        // RT-thread
    virtual void onTransport(const TransportRtSnapshot& transport) noexcept = 0; // RT-thread
};

// RT-плеер паттерна: переводит PatternStepEvent в команды RT-очереди.
struct IRtCommandQueue;
struct IPatternRuntimePlayer {
    virtual ~IPatternRuntimePlayer() = default;
    virtual void setActivePattern(const PatternState* pattern) noexcept = 0; // service/control
    virtual void processBlock(const TransportRtSnapshot& transport,
                              IRtCommandQueue& outRtQueue) noexcept = 0;     // RT-thread
};

} // namespace avantgarde

