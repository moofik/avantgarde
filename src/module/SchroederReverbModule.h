#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/IAudioModule.h"

namespace avantgarde {

// Классический Schroeder/Moorer-реверб в stereo.
// Цель:
// - дать "нормальный" базовый реверб без внешних зависимостей;
// - сохранить RT-безопасность (без аллокаций в process()).
class SchroederReverbModule final : public IAudioModule {
public:
    enum : uint16_t {
        P_WET = 0,   // 0..1
        P_ROOM = 1,  // 0..1 (feedback/room size)
        P_DAMP = 2,  // 0..1 (HF damping)
        P_WIDTH = 3, // 0..1 (stereo width)
        NUM_PARAMS = 4
    };

    SchroederReverbModule() noexcept;

    // ---- IAudioModule ----
    void init(double sampleRate, std::size_t maxFrames) override;
    void beginBlock() noexcept override;
    void process(const AudioProcessContext& ctx) noexcept override;
    void reset() override;

    // ---- IParameterized ----
    std::size_t getParamCount() const override;
    float getParam(std::size_t index) const override;
    void setParam(std::size_t index, float value) override;
    const ParamMeta& getParamMeta(std::size_t index) const override;

private:
    struct Params {
        float wet{0.25f};
        float room{0.65f};
        float damp{0.30f};
        float width{0.85f};
    };

    // Одна comb-линия с встроенным low-pass в feedback-контуре.
    struct Comb {
        std::vector<float> buf{};
        std::size_t idx{0};
        float lpState{0.0f};
    };

    // Одна allpass-линия.
    struct Allpass {
        std::vector<float> buf{};
        std::size_t idx{0};
    };

    // Обработать один сэмпл через comb.
    static float processComb_(Comb& c, float in, float feedback, float dampA, float dampB) noexcept;
    // Обработать один сэмпл через allpass.
    static float processAllpass_(Allpass& a, float in, float feedback) noexcept;

    // Пересчитать внутренние коэффициенты из snapshot параметров.
    void recalcFromParams_(const Params& p) noexcept;
    // Настроить длины delay-линий под текущий sampleRate.
    void configureDelayLines_();
    // Нормализация/кламп.
    static float clamp01_(float v) noexcept;

    double sampleRate_{48000.0};

    // write_ получает setParam() (control path), read_ обновляется в beginBlock() (RT path).
    std::atomic<Params> write_{Params{}};
    Params read_{};

    // Коэффициенты, вычисленные из read_.
    float wet1_{0.2f};
    float wet2_{0.05f};
    float dry_{0.75f};
    float feedback_{0.80f};
    float dampA_{0.35f};
    float dampB_{0.65f};

    // Freeverb-подобная топология:
    // 4 comb + 2 allpass на канал (достаточно для "музыкального" хвоста в MVP).
    std::array<Comb, 4> combL_{};
    std::array<Comb, 4> combR_{};
    std::array<Allpass, 2> apL_{};
    std::array<Allpass, 2> apR_{};

    std::array<ParamMeta, NUM_PARAMS> meta_{};
};

} // namespace avantgarde

