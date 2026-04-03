#pragma once
#include "IParameterized.h"


namespace avantgarde {


// Любой FX/процессор графа. Вся подготовка — в init(); process() — строго RT‑safe.
// В process() модуль читает музыкальный контекст из полей ctx.transport* (BPM/TS/sampleTime),
// чтобы реализовать темпо-синхронные алгоритмы (delay/reverb/шаговые LFO и т.д.).
    struct IAudioModule : IParameterized {
        virtual ~IAudioModule() = default;
        virtual void init(double sampleRate, std::size_t maxFrames) = 0; // вне RT
        virtual void process(const AudioProcessContext& ctx) = 0; // RT, no‑throw
        virtual void reset() = 0; // сброс внутренних состояний
    };


} // namespace avantgarde
