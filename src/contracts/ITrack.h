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
#include "IParameterized.h"


namespace avantgarde {


// Трек владеет модулями и последовательно прогоняет через них сигнал.
    struct ITrack : IParameterized {
        virtual ~ITrack() = default;
        // Проверка базовой готовности трека к работе.
        // Используется control-слоем как легкий guard перед non-RT операциями.
        [[nodiscard]] virtual bool healthcheck() const noexcept = 0;
        virtual void addModule(std::unique_ptr<IAudioModule> mod) = 0; // вне RT
        virtual IAudioModule* getModule(std::size_t index) = 0; // конфигурация/снапшоты
        virtual void process(const AudioProcessContext& ctx) = 0; // RT‑safe
        // NEW: узкое RT-API для адресных команд (ParamSet, NoteOn/Off, ClipTrigger и т.п.)
        virtual void onRtCommand(const RtCommand& cmd) noexcept = 0;   // RT-safe, без аллокаций

        // По умолчанию "чистый" трек может не экспонировать параметры.
        // ClipTrack/другие parameterized-track реализации должны это переопределять.
        std::size_t getParamCount() const override { return 0; }
        float getParam(std::size_t) const override { return 0.0f; }
        void setParam(std::size_t, float) override {}
        const ParamMeta& getParamMeta(std::size_t) const override {
            static const ParamMeta kNoMeta{};
            return kNoMeta;
        }
    };


} // namespace avantgarde
