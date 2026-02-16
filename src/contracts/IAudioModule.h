#pragma once
#include "IParameterized.h"


namespace avantgarde {


// Любой FX/процессор графа. Вся подготовка — в init(); process() — строго RT‑safe.
    struct IAudioModule : IParameterized {
        virtual ~IAudioModule() = default;
        virtual void init(double sampleRate, std::size_t maxFrames) = 0; // вне RT
        virtual void process(const AudioProcessContext& ctx) = 0; // RT, no‑throw
        virtual void reset() = 0; // сброс внутренних состояний
    };


} // namespace avantgarde