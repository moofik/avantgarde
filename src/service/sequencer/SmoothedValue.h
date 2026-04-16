#pragma once

#include <algorithm>
#include <cstdint>

namespace avantgarde {

/**
 * @brief Линейный сглаживатель значения во времени (в sample-домене).
 *
 * Простой utility-класс для control-layer:
 * - хранит текущее, целевое и временное окно перехода;
 * - позволяет спрашивать "какое значение должно быть в момент nowSample";
 * - не знает ничего про UI/engine/параметры, только про математику перехода.
 *
 * Класс пригодится для:
 * - loop-reset параметров секвенсора;
 * - будущего crossfade/scene-slider (octatrack-like переходы);
 * - любых не-RT интерполяций в приложении.
 */
class SmoothedValue final {
public:
    /// Жестко выставляет значение и сбрасывает активную рампу.
    void snap(float value) noexcept {
        current_ = value;
        target_ = value;
        startSample_ = 0U;
        endSample_ = 0U;
        active_ = false;
    }

    /// Запускает линейную рампу current_ -> target за durationSamples.
    void startRamp(float target,
                   uint64_t nowSample,
                   uint64_t durationSamples) noexcept {
        startValue_ = current_;
        target_ = target;
        startSample_ = nowSample;
        endSample_ = nowSample + std::max<uint64_t>(1U, durationSamples);
        active_ = true;
    }

    /// Возвращает значение на текущем sample и обновляет внутренний current_.
    float valueAt(uint64_t nowSample) noexcept {
        if (!active_) {
            return current_;
        }
        if (nowSample <= startSample_) {
            current_ = startValue_;
            return current_;
        }
        if (nowSample >= endSample_) {
            current_ = target_;
            active_ = false;
            return current_;
        }
        const float denom = static_cast<float>(std::max<uint64_t>(1U, endSample_ - startSample_));
        const float t = static_cast<float>(nowSample - startSample_) / denom;
        current_ = startValue_ + (target_ - startValue_) * std::clamp(t, 0.0f, 1.0f);
        return current_;
    }

    bool isActive() const noexcept { return active_; }
    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }

private:
    float current_{0.0f};
    float startValue_{0.0f};
    float target_{0.0f};
    uint64_t startSample_{0U};
    uint64_t endSample_{0U};
    bool active_{false};
};

} // namespace avantgarde

