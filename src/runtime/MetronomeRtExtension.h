#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "contracts/IRtExtension.h"

namespace avantgarde {

/**
 * @brief RT-safe метроном, синхронизированный с transport sampleTime.
 *
 * Ключевые свойства:
 * - работает строго в аудио-потоке (onBlockEnd),
 * - не использует аллокации и блокировки в RT,
 * - шаг сетки фиксирован на 1/16 (1/4 beat),
 * - акцентирует начало такта и доли.
 */
class MetronomeRtExtension final : public IRtExtension {
public:
    /**
     * @param sampleRate Частота дискретизации проекта.
     * @param numOutChannels Число выходных каналов, в которые микшировать клик.
     */
    MetronomeRtExtension(double sampleRate, uint32_t numOutChannels) noexcept;

    /**
     * @brief Включить/выключить метроном.
     * Можно вызывать из control-потока.
     */
    void setEnabled(bool enabled) noexcept;

    /**
     * @brief Текущее состояние метронома (control-снапшот).
     */
    [[nodiscard]] bool enabled() const noexcept;

    void onBlockBegin(const AudioProcessContext&) noexcept override {}
    void onBlockEnd(const AudioProcessContext& ctx) noexcept override;

private:
    struct Voice final {
        bool active{false};
        float phase{0.0f};
        float phaseInc{0.0f};
        float env{0.0f};
        float decayPerSample{0.0f};
        float gain{0.0f};
    };

    enum class TickAccent : uint8_t {
        Bar = 0,
        Beat,
        Subdivision
    };

    static constexpr std::size_t kMaxVoices = 8U;
    static constexpr std::size_t kMaxTicksPerBlock = 16U;

    void triggerVoice_(TickAccent accent) noexcept;
    float renderVoicesSample_() noexcept;
    static TickAccent classifyTick_(uint64_t tickIndex, uint8_t tsNum, uint8_t tsDen) noexcept;

    std::array<Voice, kMaxVoices> voices_{};
    std::atomic<bool> enabled_{false};
    double sampleRate_{48000.0};
    uint32_t numOutChannels_{2};
    std::size_t nextVoice_{0U};
};

} // namespace avantgarde

