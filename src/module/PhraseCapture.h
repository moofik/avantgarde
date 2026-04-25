#pragma once

#include <cstdint>
#include <vector>

namespace avantgarde {

struct CapturedPhrase {
    uint32_t start{0};
    uint32_t length{0};
    bool valid{false};
};

// RT-safe helper для кольцевой памяти и захвата фразы из недавней истории.
class PhraseCapture final {
public:
    void init(double sampleRate, uint32_t ringCapacity);
    void reset() noexcept;

    // Пишет один stereo sample в ring.
    void pushSample(float inL, float inR) noexcept;

    // Возвращает "снимок" фразы, привязанный к моменту quantizedSample.
    CapturedPhrase capturePhraseAtSample(uint64_t quantizedSample,
                                         uint64_t nowSample,
                                         uint32_t phraseLengthSamples) const noexcept;

    uint32_t getWritePos() const noexcept { return writePos_; }
    uint32_t getRingCapacity() const noexcept { return ringCapacity_; }
    uint32_t getValidHistorySamples() const noexcept { return validHistorySamples_; }

    const std::vector<float>& leftRing() const noexcept { return ringL_; }
    const std::vector<float>& rightRing() const noexcept { return ringR_; }

private:
    static uint32_t wrapIndex_(int64_t i, uint32_t size) noexcept;

    double sampleRate_{48000.0};
    uint32_t ringCapacity_{0};
    uint32_t writePos_{0};
    uint32_t validHistorySamples_{0};
    std::vector<float> ringL_{};
    std::vector<float> ringR_{};
};

} // namespace avantgarde

