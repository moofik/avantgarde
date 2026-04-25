#include "module/PhraseCapture.h"

#include <algorithm>

namespace avantgarde {

void PhraseCapture::init(double sampleRate, uint32_t ringCapacity) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    ringCapacity_ = std::max<uint32_t>(1U, ringCapacity);
    ringL_.assign(ringCapacity_, 0.0f);
    ringR_.assign(ringCapacity_, 0.0f);
    writePos_ = 0U;
    validHistorySamples_ = 0U;
}

void PhraseCapture::reset() noexcept {
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writePos_ = 0U;
    validHistorySamples_ = 0U;
}

void PhraseCapture::pushSample(float inL, float inR) noexcept {
    if (ringCapacity_ == 0U || ringL_.empty() || ringR_.empty()) {
        return;
    }
    ringL_[writePos_] = inL;
    ringR_[writePos_] = inR;
    writePos_ = (writePos_ + 1U >= ringCapacity_) ? 0U : writePos_ + 1U;
    if (validHistorySamples_ < ringCapacity_) {
        ++validHistorySamples_;
    }
}

CapturedPhrase PhraseCapture::capturePhraseAtSample(uint64_t quantizedSample,
                                                    uint64_t nowSample,
                                                    uint32_t phraseLengthSamples) const noexcept {
    CapturedPhrase out{};
    if (ringCapacity_ == 0U) {
        return out;
    }
    const uint32_t history = std::max<uint32_t>(1U, validHistorySamples_);
    const uint32_t wanted = std::max<uint32_t>(1U, std::min<uint32_t>(phraseLengthSamples, ringCapacity_));
    const uint32_t safeLen = std::max<uint32_t>(1U, std::min<uint32_t>(wanted, history));

    const uint64_t delay = std::min<uint64_t>((nowSample >= quantizedSample) ? (nowSample - quantizedSample) : 0ULL,
                                              static_cast<uint64_t>(ringCapacity_ - 1U));
    const uint32_t phraseEnd = wrapIndex_(
        static_cast<int64_t>(writePos_) - 1LL - static_cast<int64_t>(delay),
        ringCapacity_);
    const uint32_t start = wrapIndex_(
        static_cast<int64_t>(phraseEnd) - static_cast<int64_t>(safeLen - 1U),
        ringCapacity_);
    out.start = start;
    out.length = safeLen;
    out.valid = true;
    return out;
}

uint32_t PhraseCapture::wrapIndex_(int64_t i, uint32_t size) noexcept {
    if (size == 0U) {
        return 0U;
    }
    const int64_t m = static_cast<int64_t>(size);
    int64_t r = i % m;
    if (r < 0) {
        r += m;
    }
    return static_cast<uint32_t>(r);
}

} // namespace avantgarde

