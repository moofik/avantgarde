#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "types.h"


namespace avantgarde {


// Описание устройств платформы (CoreAudio / ALSA / JACK / PipeWire)
    struct AudioDeviceInfo {
        std::string id; // "default", "BuiltInOutput", "hw:0,0", ...
        std::string name; // человекочитаемое
        int maxInput = 0;
        int maxOutput = 2;
        int defaultSampleRate = 48000;
        bool isDefault = false;
    };


// Конфигурация потока: частота, размер блока, каналы
    struct StreamConfig {
        int sampleRate = 48000;
        int blockFrames = 256; // предпочтительно степень двойки
        int numInput = 0;
        int numOutput = 2;
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
        virtual int sampleRate() const noexcept = 0;
        virtual int blockFrames() const noexcept = 0;
        virtual int numInput() const noexcept = 0;
        virtual int numOutput() const noexcept = 0;
        virtual uint64_t totalCallbacks() const noexcept = 0;
        virtual uint64_t xruns() const noexcept = 0;
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