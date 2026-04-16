#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "contracts/IAudioModule.h"
#include "contracts/IUi.h"
#include "contracts/IPlatform.h"
#include "contracts/ITransport.h"
#include "contracts/IPattern.h"
#include "contracts/ids.h"

namespace avantgarde {

// Конфигурация аудио слоя.
struct SamplerEngineConfig {
    // Количество пользовательских треков в пуле.
    uint8_t trackCount{4};
    // Базовая частота рендера.
    double sampleRate{48000.0};
    // Размер аудио-блока.
    int blockFrames{256};
    // Число входных каналов хоста.
    int numInput{0};
    // Число выходных каналов хоста.
    int numOutput{2};
};

// Runtime метрики из аудиохоста/RT очередей.
struct SamplerEngineTelemetry {
    // Количество колбэков рендера.
    uint64_t totalCallbacks{0};
    // XRUN счетчик.
    uint64_t xruns{0};
    // Флаг overflow RT-команд (агрегированный).
    bool rtQueueOverflow{false};
    // Текущий размер блока в кадрах.
    uint32_t blockFrames{0};
};

// Изолированный слой Engine:
// инкапсулирует audio host, transport, rt-очереди и track команды.
// ВАЖНО: слой платформенно-нейтрален. Конкретный IAudioHost
// инжектируется извне (обычно в main/app bootstrap).
class SamplerEngineLayer {
public:
    SamplerEngineLayer();
    ~SamplerEngineLayer();

    // Инициализация графа движка. Начальное состояние UI отдается в bootstrapOut.
    bool init(const SamplerEngineConfig& config,
              const std::shared_ptr<IAudioHost>& audioHost,
              UiState& bootstrapOut,
              std::string& errorOut);
    // Запуск аудиострима.
    bool start(std::string& errorOut);
    // Остановка аудиострима и cleanup.
    void stop() noexcept;

    // Снять telemetry + очистить overflow-флаги очередей.
    SamplerEngineTelemetry telemetryAndResetOverflow() noexcept;

    // Глобальные transport операции.
    void setTransportPlaying(bool playing) noexcept;
    void setTempo(float bpm) noexcept;
    void setQuantize(QuantizeMode q) noexcept;
    void setTimeSignature(uint8_t num, uint8_t den) noexcept;
    void setSwing(float swing01) noexcept;
    // Включить/выключить встроенный метроном (тик по сетке 1/16).
    void setMetronomeEnabled(bool enabled) noexcept;

    // Track-local операции (mute/arm/speed/загрузка клипа).
    bool setTrackMuted(uint8_t track, bool muted) noexcept;
    bool setTrackArmed(uint8_t track, bool armed) noexcept;
    // Пресет "LOOPER" для трека:
    // true  -> Looper + IgnoreIfPlaying + ManualStop (+follow transport, loop on)
    // false -> Note   + RetriggerOnNoteOn + ByNoteOff (+one-shot note gate, loop off)
    bool setTrackLooperMode(uint8_t track, bool looperEnabled) noexcept;
    // Установить один из 4 пользовательских профилей playback-режима трека:
    // Pattern / PatternOnce / Loop / OneShot.
    bool setTrackPlaybackProfile(uint8_t track, TrackPlaybackProfileValue profile) noexcept;
    bool setTrackSpeed(uint8_t track, float speed) noexcept;
    // Включить/выключить tempo-sync для трека:
    // ON  -> playbackInc следует за transport BPM/TS и bars,
    // OFF -> playbackInc остается ручным.
    bool setTrackTempoSync(uint8_t track, bool enabled) noexcept;
    // Отправить note-on в выбранный трек (для NOTE режима/секвенсора).
    bool triggerTrackNoteOn(uint8_t track, uint8_t note, float velocity01) noexcept;
    // Отправить note-off в выбранный трек.
    bool triggerTrackNoteOff(uint8_t track, uint8_t note) noexcept;
    // Универсальная установка track-параметра по индексу ids.h.
    bool setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept;
    // Задать длину slot0 в барах (>=1) для режима stretch-to-bars.
    bool setTrackBars(uint8_t track, uint32_t bars) noexcept;
    // Добавить FX-модуль в конец цепочки выбранного трека (вне RT).
    bool addFxToTrack(uint8_t track, std::unique_ptr<IAudioModule> module) noexcept;
    // Удалить FX-модуль из цепочки выбранного трека по индексу (вне RT).
    bool removeFxFromTrack(uint8_t track, uint8_t fxSlot) noexcept;
    // Установить параметр FX-слота (RT-safe через ParamSet команду).
    bool setFxParam(uint8_t track, uint8_t fxSlot, uint16_t paramIndex, float normalizedValue) noexcept;
    // Включить/выключить FX-слот без удаления из цепочки.
    bool setFxEnabled(uint8_t track, uint8_t fxSlot, bool enabled) noexcept;
    bool loadSampleToTrack(uint8_t track, const std::string& path, std::string& clipNameOut) noexcept;
    // Предзагрузка WAV в clip-pool по стабильному clipRefId (без назначения треку).
    bool preloadClipToPool(uint32_t clipRefId, const std::string& path, std::string& errorOut) noexcept;
    // Назначить уже preloaded clipRef в слот трека без IO/декодирования.
    bool setTrackClipRef(uint8_t track, uint32_t clipRefId) noexcept;
    // Очистить загруженный сэмпл трека (slot0) без удаления FX-цепочки.
    bool clearTrackSample(uint8_t track) noexcept;
    // Preview-голос (отдельный sample-preview engine, не Track/не Transport).
    void previewRequest(const std::string& path,
                        float speed,
                        float start01,
                        float end01,
                        float gain01) noexcept;
    void previewStop() noexcept;

    // Pattern subsystem API.
    bool requestPatternSwitchRelative(int delta) noexcept;
    bool requestPatternSwitchTo(PatternId target) noexcept;
    bool processPendingPatternSwitches() noexcept;
    UiPatternState patternUiState() const noexcept;
    // Синхронизировать control/UI-кэш из live состояния движка.
    bool syncUiCache(UiTransportState& transportInOut,
                     std::vector<UiTrackStateView>& tracksInOut) const noexcept;

private:
    // Зафиксировать runtime-state активного паттерна в snapshot manager.
    bool captureActivePatternSnapshot_() noexcept;
    // PImpl: прячем concrete runtime/platform детали из заголовка.
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace avantgarde
