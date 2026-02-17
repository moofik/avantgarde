#include "contracts/IAudioModule.h"
#include <cmath>
#include <algorithm>

namespace avantgarde {

    class OnePoleHPFModule final : public IAudioModule {
    public:
        enum : uint16_t { CUTOFF = 0, NUM_PARAMS = 1 };

        // ---- константы параметров ----
        static constexpr std::size_t kParamCount   = 1;
        static constexpr std::size_t kParamCutoff  = 0; // 0..1 normalized

        explicit OnePoleHPFModule() noexcept {
            meta_[CUTOFF] = { "Cutoff", 0.0f, 1.0f, false, "Norm" };
        };

        // ---- IAudioModule ----
        void init(double sampleRate, std::size_t /*maxFrames*/) override {
            fs_ = (sampleRate > 0.0 ? sampleRate : 48000.0);
            needRecalc_ = true;
            reset();
        }

        void reset() noexcept override {
            prevX_ = 0.0f;
            prevY_ = 0.0f;
            if (needRecalc_) recalcCoeff_();
        }

        void process(const AudioProcessContext& ctx) noexcept override {
            if (needRecalc_) { recalcCoeff_(); needRecalc_ = false; }

            const float* in  = ctx.in  ? ctx.in[0]  : nullptr;
            float*       out = ctx.out ? ctx.out[0] : nullptr;
            if (!in || !out) return;

            float a  = a_;
            float px = prevX_;
            float py = prevY_;

            for (std::size_t i = 0; i < ctx.nframes; ++i) {
                const float x = in[i];
                const float y = a * py + a * (x - px);
                out[i] = y;
                px = x;
                py = y;
            }
            prevX_ = px;
            prevY_ = py;
        }

        // ---- параметрический интерфейс по контракту ----
        std::size_t getParamCount() const noexcept override {
            return kParamCount;
        }

        float getParam(std::size_t index) const noexcept override {
            switch (index) {
                case kParamCutoff: return cutoff01_;
                default:           return 0.0f;
            }
        }

        void setParam(std::size_t index, float value) noexcept override {
            switch (index) {
                case kParamCutoff:
                    cutoff01_ = std::clamp(value, 0.0f, 1.0f);
                    needRecalc_ = true; // посчитаем коэффициент вне inner loop
                    break;
                default:
                    break; // игнор неизвестных
            }
        }

        const ParamMeta& getParamMeta(std::size_t idx) const override { return meta_[idx]; }

        // удобный шорткат, если хочешь обращаться напрямую
        void setCutoff01(float v) noexcept { setParam(kParamCutoff, v); }
        float cutoff01() const noexcept    { return getParam(kParamCutoff); }

    private:
        // ———— внутренности ————
        static constexpr float HPF_MIN_HZ = 10.0f;
        static constexpr float HPF_MAX_HZ = 20000.0f;

        float mapNormToHz_(float t) const noexcept {
            const float nyq45 = float(0.45 * fs_);
            const float fmax  = std::min(HPF_MAX_HZ, nyq45 > 10.f ? nyq45 : HPF_MAX_HZ);
            const float fmin  = std::max(1.0f, std::min(
                    HPF_MIN_HZ, fmax * 0.5f));
            const float lnMin = std::log(fmin);
            const float lnMax = std::log(fmax);
            const float lnF   = lnMin + (lnMax - lnMin) * std::clamp(t, 0.0f, 1.0f);
            return std::exp(lnF);
        }

        void recalcCoeff_() noexcept {
            const float fc = mapNormToHz_(cutoff01_);
            const float x  = -2.0f * 3.14159265358979323846f * fc / float(fs_);
            a_ = std::exp(x);
            if (!std::isfinite(a_) || a_ < 0.0f || a_ > 1.0f) a_ = 0.0f;
        }

        // state
        double fs_     = 48000.0;
        float  cutoff01_= 0.5f;
        float  a_      = 0.0f;
        float  prevX_  = 0.0f;
        float  prevY_  = 0.0f;
        bool   needRecalc_ = true;
        std::array<ParamMeta, NUM_PARAMS> meta_;
    };

} // namespace avantgarde
