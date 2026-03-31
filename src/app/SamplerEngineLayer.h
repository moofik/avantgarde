#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "contracts/IUi.h"
#include "contracts/ITransport.h"

namespace avantgarde {

// Конфигурация аудио слоя.
struct SamplerEngineConfig {
    // Путь к обязательному сэмплу трека T1.
    std::string track0Path{};
    // Путь к сэмплу трека T2 (опционально).
    std::string track1Path{};
    // Признак, что T2 должен быть загружен при старте.
    bool hasTrack1{false};
    // Базовая частота рендера.
    double sampleRate{48000.0};
    // Размер аудио-блока.
    int blockFrames{256};
    // Число входных каналов хоста.
    int numInput{0};
    // Число выходных каналов хоста.
    int numOutput{2};
};

// Начальный UI-снимок, который engine отдает application после init.
struct SamplerEngineBootstrap {
    // Стартовый transport state.
    UiTransportState transport{};
    // Стартовые состояния треков.
    std::array<UiTrackStateView, 2> tracks{};
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
class SamplerEngineLayer {
public:
    SamplerEngineLayer();
    ~SamplerEngineLayer();

    // Инициализация графа движка и загрузка стартовых сэмплов.
    bool init(const SamplerEngineConfig& config,
              SamplerEngineBootstrap& bootstrap,
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

    // Track-local операции.
    bool playTrack(uint8_t track) noexcept;
    bool stopTrack(uint8_t track) noexcept;
    bool setTrackSpeed(uint8_t track, float speed) noexcept;
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
