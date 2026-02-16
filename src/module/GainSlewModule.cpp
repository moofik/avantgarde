#include "contracts/IAudioModule.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace avantgarde {

    class GainSlewModule final : public IAudioModule {
    public:
        enum : uint16_t { P_GAIN = 0, NUM_PARAMS = 1 };
        enum class SlewMode : uint8_t { PerBlocks, FixedMs };

        explicit GainSlewModule(SlewMode m = SlewMode::PerBlocks, uint8_t blocks = 1, float ms = 0.0f) noexcept
                : mode_(m), blocks_(blocks ? blocks : 1), ms_(ms) {
            meta_[P_GAIN] = { "Gain", 0.0f, 1.0f, false, "x" };
        }

        void init(double sampleRate, std::size_t /*maxFrames*/) override {
            sr_ = sampleRate;
            write_.store(Params{1.0f}, std::memory_order_relaxed);
            read_   = write_.load(std::memory_order_relaxed);
            gState_ = read_.gain;
            rampActive_ = false;
            step_ = 0.0f;
        }

        void beginBlock() noexcept override {
            // Снимок целей на блок
            const Params snap = write_.load(std::memory_order_relaxed);
            const float newTarget = snap.gain;

            // если цель изменилась — стартуем НОВУЮ рампу
            if (newTarget != target_) {
                target_ = newTarget;
                initialStart_   = gState_;      // фиксируем точку старта ОДИН раз
                samplesDone_    = 0;
                totalSamplesToGo_ = 0;          // посчитаем в process(), т.к. нужен nframes/sr
                rampInitPending_ = (initialStart_ != target_);
                rampActive_       = rampInitPending_;
            } else {
                // цель прежняя — либо рампа продолжается, либо она уже завершилась
                rampActive_ = (gState_ != target_);
                // rampInitPending_ НЕ трогаем: если новая рампа не стартовала — не пересчитываем totals
            }
            // step_ посчитаем в process()
        }

        void process(const AudioProcessContext& ctx) noexcept override {
            // ленивый расчёт длительности/шага рампы (только один раз на старт рампы)
            if (rampInitPending_) {
                if (mode_ == SlewMode::PerBlocks) {
                    totalSamplesToGo_ = std::max<uint32_t>(1u, static_cast<uint32_t>(blocks_) * static_cast<uint32_t>(ctx.nframes));
                } else { // FixedMs
                    totalSamplesToGo_ = std::max<uint32_t>(1u, static_cast<uint32_t>((ms_ / 1000.f) * static_cast<float>(sr_)));
                }
                step_ = (target_ - initialStart_) / static_cast<float>(totalSamplesToGo_);
                samplesDone_ = 0;
                rampInitPending_ = false;
                rampActive_ = (initialStart_ != target_);
            }

            float g = gState_;

            for (std::size_t i = 0; i < ctx.nframes; ++i) {
                if (rampActive_) {
                    const uint32_t remaining = (samplesDone_ >= totalSamplesToGo_) ? 0u : (totalSamplesToGo_ - samplesDone_);
                    if (remaining <= 1u) {
                        g = target_;
                        samplesDone_ = totalSamplesToGo_;
                        rampActive_ = false;
                        step_ = 0.f;
                    } else {
                        g += step_;
                        ++samplesDone_;
                    }
                }

                const float inL = ctx.in[0][i];
                const float inR = ctx.in[1] ? ctx.in[1][i] : inL;

                const float outL = inL * g;
                const float outR = inR * g;

                ctx.out[0][i] = outL;
                if (ctx.out[1]) ctx.out[1][i] = outR;
            }

            gState_ = g;
        }

        void reset() override {
            read_    = write_.load(std::memory_order_relaxed);
            gState_  = read_.gain;
            target_  = gState_;
            initialStart_ = gState_;
            step_    = 0.0f;
            samplesDone_ = 0;
            totalSamplesToGo_ = 0;
            rampInitPending_ = false;
            rampActive_ = false;
        }

        // ===== IParameterized =====
        std::size_t getParamCount() const override { return NUM_PARAMS; }
        float getParam(std::size_t idx) const noexcept override { return (idx==P_GAIN)? read_.gain : 0.f; }
        void  setParam(std::size_t idx, float v) noexcept override { if (idx==P_GAIN) write_.store(Params{v}, std::memory_order_relaxed); }
        const ParamMeta& getParamMeta(std::size_t idx) const override { return meta_[idx]; }

        // Тюнинг политики извне (по желанию)
        void setSlewBlocks(uint8_t n) noexcept { mode_ = SlewMode::PerBlocks; blocks_ = n ? n : 1; }
        void setSlewMs(float ms) noexcept { mode_ = SlewMode::FixedMs; ms_ = std::max(0.f, ms); }

    private:
        struct Params { float gain{1.0f}; };

        // политика
        SlewMode mode_;
        uint8_t  blocks_{1};   // переход за N блоков
        float    ms_{0.0f};    // или фиксированное время, если нужно

        float   initialStart_{1.0f};
        uint32_t samplesDone_{0};
        uint32_t totalSamplesToGo_{0};
        bool    rampInitPending_{false};

        // состояние
        double sr_{48000.0};
        std::atomic<Params> write_{Params{1.0f}};
        Params read_{Params{1.0f}};
        std::array<ParamMeta, NUM_PARAMS> meta_;

        // рампа
        float gState_{1.0f}, start_{1.0f}, target_{1.0f}, step_{0.0f};
        bool  rampActive_{false};
    };

} // namespace avantgarde
