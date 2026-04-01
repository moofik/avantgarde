#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "contracts/IAudioModule.h"
#include "contracts/IUi.h"
#include "contracts/IPlatform.h"
#include "contracts/ITransport.h"

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

    // Track-local операции (mute/arm/speed/загрузка клипа).
    bool setTrackMuted(uint8_t track, bool muted) noexcept;
    bool setTrackArmed(uint8_t track, bool armed) noexcept;
    bool setTrackSpeed(uint8_t track, float speed) noexcept;
    // Добавить FX-модуль в конец цепочки выбранного трека (вне RT).
    bool addFxToTrack(uint8_t track, std::unique_ptr<IAudioModule> module) noexcept;
    // Удалить FX-модуль из цепочки выбранного трека по индексу (вне RT).
    bool removeFxFromTrack(uint8_t track, uint8_t fxSlot) noexcept;
    // Установить параметр FX-слота (RT-safe через ParamSet команду).
    bool setFxParam(uint8_t track, uint8_t fxSlot, uint16_t paramIndex, float normalizedValue) noexcept;
    bool loadSampleToTrack(uint8_t track, const std::string& path, std::string& clipNameOut) noexcept;
    // Preview-голос (скрытый отдельный трек).
    void previewRequest(const std::string& path) noexcept;
    void previewStop() noexcept;

private:
    // PImpl: прячем concrete runtime/platform детали из заголовка.
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace avantgarde
