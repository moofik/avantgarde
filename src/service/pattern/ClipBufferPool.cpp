#include "service/pattern/ClipBufferPool.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace avantgarde {
namespace {

static inline uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static inline uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
static inline bool read_exact(std::ifstream& f, void* dst, std::size_t n) {
    f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    return f.good();
}

struct WavFmt {
    uint16_t audioFormat{0};   // 1=PCM, 3=float32
    uint16_t numChannels{0};   // 1..2
    uint32_t sampleRate{0};
    uint16_t bitsPerSample{0}; // 16/24/32
    uint16_t blockAlign{0};
};

bool decode_wav_to_shared_planar(const char* path, SharedClipBuffer& out, std::string* errorOut) {
    out = SharedClipBuffer{};
    if (!path) {
        if (errorOut) *errorOut = "path is null";
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        if (errorOut) *errorOut = "cannot open file";
        return false;
    }

    uint8_t riff[12];
    if (!read_exact(f, riff, 12)) {
        if (errorOut) *errorOut = "bad riff header";
        return false;
    }
    if (std::memcmp(riff + 0, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        if (errorOut) *errorOut = "not RIFF/WAVE";
        return false;
    }

    bool haveFmt = false;
    bool haveData = false;
    WavFmt fmt{};
    std::vector<uint8_t> dataBytes;

    while (f.good() && !(haveFmt && haveData)) {
        uint8_t chdr[8];
        if (!read_exact(f, chdr, 8)) break;
        const uint32_t csize = read_u32_le(chdr + 4);
        const bool pad = (csize & 1u) != 0;

        if (std::memcmp(chdr + 0, "fmt ", 4) == 0) {
            if (csize < 16) {
                if (errorOut) *errorOut = "invalid fmt chunk size";
                return false;
            }
            std::vector<uint8_t> buf(csize);
            if (!read_exact(f, buf.data(), csize)) {
                if (errorOut) *errorOut = "cannot read fmt chunk";
                return false;
            }
            fmt.audioFormat = read_u16_le(buf.data() + 0);
            fmt.numChannels = read_u16_le(buf.data() + 2);
            fmt.sampleRate = read_u32_le(buf.data() + 4);
            fmt.blockAlign = read_u16_le(buf.data() + 12);
            fmt.bitsPerSample = read_u16_le(buf.data() + 14);
            haveFmt = true;
        } else if (std::memcmp(chdr + 0, "data", 4) == 0) {
            dataBytes.resize(csize);
            if (csize > 0 && !read_exact(f, dataBytes.data(), csize)) {
                if (errorOut) *errorOut = "cannot read data chunk";
                return false;
            }
            haveData = true;
        } else {
            f.seekg(static_cast<std::streamoff>(csize), std::ios::cur);
            if (!f.good()) {
                if (errorOut) *errorOut = "cannot skip unknown chunk";
                return false;
            }
        }

        if (pad) {
            f.seekg(1, std::ios::cur);
            if (!f.good()) {
                if (errorOut) *errorOut = "cannot skip pad byte";
                return false;
            }
        }
    }

    if (!haveFmt || !haveData) {
        if (errorOut) *errorOut = "missing fmt/data chunk";
        return false;
    }

    if (!(fmt.audioFormat == 1 || fmt.audioFormat == 3)) {
        if (errorOut) *errorOut = "unsupported wav format";
        return false;
    }
    if (!(fmt.numChannels == 1 || fmt.numChannels == 2)) {
        if (errorOut) *errorOut = "unsupported channel count";
        return false;
    }
    if (!(fmt.bitsPerSample == 16 || fmt.bitsPerSample == 24 || fmt.bitsPerSample == 32)) {
        if (errorOut) *errorOut = "unsupported bit depth";
        return false;
    }
    if (fmt.blockAlign == 0) {
        if (errorOut) *errorOut = "invalid blockAlign";
        return false;
    }

    const uint32_t bytesPerFrame = fmt.blockAlign;
    const uint32_t totalFrames = static_cast<uint32_t>(dataBytes.size() / bytesPerFrame);
    if (totalFrames == 0) {
        if (errorOut) *errorOut = "empty audio data";
        return false;
    }

    std::unique_ptr<float[]> ch0{new (std::nothrow) float[totalFrames]};
    if (!ch0) {
        if (errorOut) *errorOut = "alloc ch0 failed";
        return false;
    }
    std::unique_ptr<float[]> ch1{};
    if (fmt.numChannels == 2) {
        ch1.reset(new (std::nothrow) float[totalFrames]);
        if (!ch1) {
            if (errorOut) *errorOut = "alloc ch1 failed";
            return false;
        }
    }

    const uint8_t* src = dataBytes.data();
    auto clamp1 = [](float x) -> float {
        if (x > 1.0f) return 1.0f;
        if (x < -1.0f) return -1.0f;
        return x;
    };

    if (fmt.audioFormat == 3 && fmt.bitsPerSample == 32) {
        for (uint32_t i = 0; i < totalFrames; ++i) {
            const float* fr = reinterpret_cast<const float*>(src + i * bytesPerFrame);
            ch0[i] = clamp1(fr[0]);
            if (fmt.numChannels == 2) ch1[i] = clamp1(fr[1]);
        }
    } else if (fmt.audioFormat == 1 && fmt.bitsPerSample == 16) {
        for (uint32_t i = 0; i < totalFrames; ++i) {
            const uint8_t* fr = src + i * bytesPerFrame;
            int16_t s0 = static_cast<int16_t>(read_u16_le(fr + 0));
            ch0[i] = static_cast<float>(s0) / 32768.0f;
            if (fmt.numChannels == 2) {
                int16_t s1 = static_cast<int16_t>(read_u16_le(fr + 2));
                ch1[i] = static_cast<float>(s1) / 32768.0f;
            }
        }
    } else if (fmt.audioFormat == 1 && fmt.bitsPerSample == 24) {
        auto read_s24 = [](const uint8_t* p) -> int32_t {
            int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
            if (v & 0x00800000) v |= 0xFF000000;
            return v;
        };
        for (uint32_t i = 0; i < totalFrames; ++i) {
            const uint8_t* fr = src + i * bytesPerFrame;
            int32_t s0 = read_s24(fr + 0);
            ch0[i] = static_cast<float>(s0) / 8388608.0f;
            if (fmt.numChannels == 2) {
                int32_t s1 = read_s24(fr + 3);
                ch1[i] = static_cast<float>(s1) / 8388608.0f;
            }
        }
    } else {
        if (errorOut) *errorOut = "unsupported PCM variant";
        return false;
    }

    out.sampleRate = static_cast<int>(fmt.sampleRate);
    out.channels = static_cast<int>(fmt.numChannels);
    out.frames = static_cast<int>(totalFrames);
    out.ch0 = std::shared_ptr<const float[]>(ch0.release(), std::default_delete<float[]>());
    out.ch1 = (out.channels == 2)
              ? std::shared_ptr<const float[]>(ch1.release(), std::default_delete<float[]>())
              : std::shared_ptr<const float[]>{};
    if (!out.valid()) {
        if (errorOut) *errorOut = "decoded buffer invalid";
        return false;
    }
    return true;
}

} // namespace

bool ClipBufferPool::loadFromFile(uint32_t clipRefId, const std::string& path, std::string* errorOut) {
    if (clipRefId == 0) {
        if (errorOut) *errorOut = "clipRefId=0 is reserved";
        return false;
    }
    if (path.empty()) {
        if (errorOut) *errorOut = "path is empty";
        return false;
    }
    SharedClipBuffer decoded{};
    if (!decode_wav_to_shared_planar(path.c_str(), decoded, errorOut)) {
        return false;
    }
    buffers_[clipRefId] = std::move(decoded);
    return true;
}

bool ClipBufferPool::put(uint32_t clipRefId, const SharedClipBuffer& buffer) {
    if (clipRefId == 0 || !buffer.valid()) {
        return false;
    }
    buffers_[clipRefId] = buffer;
    return true;
}

bool ClipBufferPool::contains(uint32_t clipRefId) const noexcept {
    return buffers_.find(clipRefId) != buffers_.end();
}

bool ClipBufferPool::get(uint32_t clipRefId, SharedClipBuffer& out) const noexcept {
    const auto it = buffers_.find(clipRefId);
    if (it == buffers_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool ClipBufferPool::erase(uint32_t clipRefId) noexcept {
    return buffers_.erase(clipRefId) > 0;
}

std::size_t ClipBufferPool::size() const noexcept {
    return buffers_.size();
}

bool ClipBufferPool::bindClipToTrack(IClipTrack& track, uint32_t slot, uint32_t clipRefId) const {
    SharedClipBuffer b{};
    if (!get(clipRefId, b)) {
        return false;
    }
    return track.loadSlotFromBuffer(slot, b);
}

} // namespace avantgarde

