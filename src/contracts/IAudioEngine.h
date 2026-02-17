#pragma once
#include <memory>
#include "ITrack.h"
#include "IRtExtension.h"
#include "IAudioRecorder.h"
#include "ITransport.h"
#include "types.h"

/**
 * @file IAudioEngine.h
 * @brief Центральное RT-ядро: треки, команды, обработка блока.
 *
 * Единственная аудио-нить вызывает processBlock(), внутри которой
 * выполняется swapBuffers(), применение команд и прогон графа.
*/
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

        virtual void setMasterRecordSink(IRtRecordSink* sink) noexcept = 0;

        virtual void setTransportBridge(ITransportBridge* t) noexcept = 0;

    };

} // namespace avantgarde
