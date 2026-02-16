#pragma once
/**
 * @file ITrack.h
 * @brief Цепочка FX одного трека (композиция модулей).
 *
 * Track владеет модулями (composition) и обеспечивает их последовательный вызов.
*/
#include <memory>
#include <vector>
#include "IAudioModule.h"


namespace avantgarde {


// Трек владеет модулями и последовательно прогоняет через них сигнал.
    struct ITrack {
        virtual ~ITrack() = default;
        virtual void addModule(std::unique_ptr<IAudioModule> mod) = 0; // вне RT
        virtual IAudioModule* getModule(std::size_t index) = 0; // конфигурация/снапшоты
        virtual void process(const AudioProcessContext& ctx) = 0; // RT‑safe
        // NEW: узкое RT-API для адресных команд (ParamSet, NoteOn/Off, ClipTrigger и т.п.)
        virtual void onRtCommand(const RtCommand& cmd) noexcept = 0;   // RT-safe, без аллокаций
    };


} // namespace avantgarde