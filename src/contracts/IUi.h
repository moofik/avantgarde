#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ITransport.h"
#include "IPattern.h"

namespace avantgarde {

enum class UiTrackState : uint8_t {
    Empty = 0,
    Stopped,
    Playing,
    Recording
};

// Упрощенный playback-режим трека для UI.
// Детальные launch/stop policy остаются в engine-параметрах,
// а UI в "из коробки" сценарии показывает только понятный toggle:
// Looper <-> Note.
enum class UiTrackPlaybackMode : uint8_t {
    Looper = 0,
    Note = 1
};

// Профиль пользовательского режима трека (4 понятных пресета):
// - Pattern       = note + loop on
// - PatternOnce   = note + loop off
// - Loop          = looper + loop on
// - OneShot       = looper + loop off
enum class UiTrackPlaybackProfile : uint8_t {
    Pattern = 0,
    PatternOnce = 1,
    Loop = 2,
    OneShot = 3
};

struct UiTransportState {
    bool playing{false};
    float bpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{QuantizeMode::Bar};
    // Флаг встроенного метронома (клик по сетке 1/16).
    bool metronomeEnabled{false};
    uint8_t activeTrack{0};
    uint64_t sampleTime{0};
};

struct UiTrackStateView {
    uint8_t id{0};
    UiTrackState state{UiTrackState::Empty};
    uint32_t bars{4};
    float stretchRatio{1.0f};
    float gain01{1.0f};
    // true = трек заглушен (в master не микшируется).
    bool muted{false};
    // true = трек вооружен для записи/овerdub (для будущего record flow).
    bool armed{false};
    // Основной режим трека:
    // - Looper: длинные клипы/лупы, ignore-if-playing, manual-stop.
    // - Note: note-driven playback, retrigger-on-note-on, stop-by-note-off.
    UiTrackPlaybackMode playbackMode{UiTrackPlaybackMode::Looper};
    // Высокоуровневый UX-профиль режима (4 пресета).
    UiTrackPlaybackProfile playbackProfile{UiTrackPlaybackProfile::Loop};
    bool loop{false};
    // Границы playback-региона внутри клипа в нормализованном виде [0..1].
    float trimStart01{0.0f};
    float trimEnd01{1.0f};
    uint8_t fxCount{0};
    // Канонические ID FX по слотам (слот 0 -> fxChainIds[0] и т.д.).
    // UI использует это для именования списка FX и подбора профиля параметров в редакторе.
    std::vector<std::string> fxChainIds{};
    // Состояние FX-слотов: 1 = enabled, 0 = bypass.
    // Индекс совпадает с fxChainIds/слотом.
    std::vector<uint8_t> fxEnabled{};
    std::string clipName;
    // Абсолютный/рабочий путь к загруженному сэмплу (для сервисов анализа).
    std::string clipPath;
};

struct UiTelemetryState {
    uint64_t totalCallbacks{0};
    uint64_t xruns{0};
    bool rtQueueOverflow{false};
};

// Runtime-состояние pattern-подсистемы для UI.
// Важно: это отдельный домен, не часть transport.
struct UiPatternState {
    // Текущий активный паттерн.
    PatternId activeId{kInvalidPatternId};
    // Целевой паттерн, ожидающий квантизированного switch.
    PatternId pendingId{kInvalidPatternId};
    // true, если switch заявлен и еще не применен.
    bool armed{false};
    // Количество паттернов в банке (для UI-индикации).
    uint16_t bankSize{0};
};

struct UiState {
    UiTransportState transport{};
    std::vector<UiTrackStateView> tracks{};
    UiTelemetryState telemetry{};
    UiPatternState pattern{};
};

struct IUiRenderer {
    virtual ~IUiRenderer() = default;
    virtual void render(const UiState& state) = 0;
};

} // namespace avantgarde
