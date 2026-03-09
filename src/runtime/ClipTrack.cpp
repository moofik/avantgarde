// src/runtime/tracks/ClipTrackImpl.h
#pragma once

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "contracts/ids.h"
#include "contracts/IClipTrack.h" // IClipTrack, ITrack, RtCommand, AudioProcessContext, CmdId

namespace avantgarde {

// ============================================================
// Minimal WAV decoder (RIFF/WAVE, little-endian)
// Supports:
//  - PCM (fmt=1): 16-bit, 24-bit
//  - IEEE float (fmt=3): 32-bit float
//  - channels: 1 or 2
// Output:
//  - planar float32 buffers (ch0, ch1)
// ============================================================

    namespace detail_wav {

        static inline uint16_t read_u16_le(const uint8_t* p) {
            return (uint16_t)(p[0] | (p[1] << 8));
        }
        static inline uint32_t read_u32_le(const uint8_t* p) {
            return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
        }
        static inline bool read_exact(std::ifstream& f, void* dst, std::size_t n) {
            f.read(reinterpret_cast<char*>(dst), (std::streamsize)n);
            return f.good();
        }

        struct WavFmt {
            uint16_t audioFormat = 0;     // 1=PCM, 3=IEEE float
            uint16_t numChannels = 0;     // 1..2 (в MVP)
            uint32_t sampleRate  = 0;
            uint16_t bitsPerSample = 0;   // 16/24/32
            uint16_t blockAlign = 0;      // bytes per frame
        };

        static inline bool decode_wav_to_planar_f32(
                const char* path,
                int& outSampleRate,
                int& outChannels,
                int& outFrames,
                std::unique_ptr<float[]>& outCh0,
                std::unique_ptr<float[]>& outCh1
        ) {
            outSampleRate = 0; outChannels = 0; outFrames = 0;
            outCh0.reset(); outCh1.reset();
            if (!path) return false;

            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) return false;

            // RIFF header: "RIFF" + size + "WAVE"
            uint8_t riff[12];
            if (!read_exact(f, riff, 12)) return false;
            if (std::memcmp(riff + 0, "RIFF", 4) != 0) return false;
            if (std::memcmp(riff + 8, "WAVE", 4) != 0) return false;

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
                    if (csize < 16) return false;
                    std::vector<uint8_t> buf(csize);
                    if (!read_exact(f, buf.data(), csize)) return false;

                    fmt.audioFormat   = read_u16_le(buf.data() + 0);
                    fmt.numChannels   = read_u16_le(buf.data() + 2);
                    fmt.sampleRate    = read_u32_le(buf.data() + 4);
                    fmt.blockAlign    = read_u16_le(buf.data() + 12);
                    fmt.bitsPerSample = read_u16_le(buf.data() + 14);

                    haveFmt = true;
                }
                else if (std::memcmp(chdr + 0, "data", 4) == 0) {
                    dataBytes.resize(csize);
                    if (csize > 0) {
                        if (!read_exact(f, dataBytes.data(), csize)) return false;
                    }
                    haveData = true;
                }
                else {
                    f.seekg((std::streamoff)csize, std::ios::cur);
                    if (!f.good()) return false;
                }

                if (pad) {
                    f.seekg(1, std::ios::cur);
                    if (!f.good()) return false;
                }
            }

            if (!haveFmt || !haveData) return false;

            // Validate
            if (!(fmt.audioFormat == 1 || fmt.audioFormat == 3)) return false;
            if (!(fmt.numChannels == 1 || fmt.numChannels == 2)) return false;
            if (!(fmt.bitsPerSample == 16 || fmt.bitsPerSample == 24 || fmt.bitsPerSample == 32)) return false;
            if (fmt.blockAlign == 0) return false;

            const uint32_t bytesPerFrame = fmt.blockAlign;
            const uint32_t totalFrames = (uint32_t)(dataBytes.size() / bytesPerFrame);
            if (totalFrames == 0) return false;
            outSampleRate = (int)fmt.sampleRate;
            outChannels   = (int)fmt.numChannels;
            outFrames     = (int)totalFrames;

            outCh0.reset(new (std::nothrow) float[totalFrames]);
            if (!outCh0) return false;
            if (outChannels == 2) {
                outCh1.reset(new (std::nothrow) float[totalFrames]);
                if (!outCh1) return false;
            }

            const uint8_t* src = dataBytes.data();
            const int ch = outChannels;

            auto clamp1 = [](float x) -> float {
                if (x > 1.0f) return 1.0f;
                if (x < -1.0f) return -1.0f;
                return x;
            };

            // IEEE float32
            if (fmt.audioFormat == 3 && fmt.bitsPerSample == 32) {
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    const float* fr = reinterpret_cast<const float*>(src + i * bytesPerFrame);
                    outCh0[i] = clamp1(fr[0]);
                    if (ch == 2) outCh1[i] = clamp1(fr[1]);
                }
                return true;
            }

            // PCM16
            if (fmt.audioFormat == 1 && fmt.bitsPerSample == 16) {
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    const uint8_t* fr = src + i * bytesPerFrame;
                    int16_t s0 = (int16_t)read_u16_le(fr + 0);
                    outCh0[i] = (float)s0 / 32768.0f;
                    if (ch == 2) {
                        int16_t s1 = (int16_t)read_u16_le(fr + 2);
                        outCh1[i] = (float)s1 / 32768.0f;
                    }
                }
                return true;
            }

            // PCM24
            if (fmt.audioFormat == 1 && fmt.bitsPerSample == 24) {
                auto read_s24 = [](const uint8_t* p) -> int32_t {
                    int32_t v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
                    if (v & 0x00800000) v |= 0xFF000000; // sign extend
                    return v;
                };

                for (uint32_t i = 0; i < totalFrames; ++i) {
                    const uint8_t* fr = src + i * bytesPerFrame;
                    int32_t s0 = read_s24(fr + 0);
                    outCh0[i] = (float)s0 / 8388608.0f; // 2^23
                    if (ch == 2) {
                        int32_t s1 = read_s24(fr + 3);
                        outCh1[i] = (float)s1 / 8388608.0f;
                    }
                }
                return true;
            }

            // PCM32 int not supported in MVP
            return false;
        }

    } // namespace detail_wav

    namespace detail_interp {

        static inline float clampf(float x, float lo, float hi) noexcept {
            return std::min(std::max(x, lo), hi);
        }

        static inline int wrapIndex(int idx, int len) noexcept {
            while (idx < 0) idx += len;
            while (idx >= len) idx -= len;
            return idx;
        }

        static inline float cubicHermite(float y0, float y1, float y2, float y3, float t) noexcept {
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + c0;
        }

        static inline float sampleCubic(const float* src, int len, double phase, bool loop) noexcept {
            if (!src || len <= 0) return 0.0f;
            if (len == 1) return src[0];

            double ph = phase;
            if (loop) {
                while (ph < 0.0) ph += static_cast<double>(len);
                while (ph >= static_cast<double>(len)) ph -= static_cast<double>(len);
            } else {
                if (ph <= 0.0) return src[0];
                const double maxPh = static_cast<double>(len - 1);
                if (ph >= maxPh) return src[len - 1];
            }

            const int i1 = static_cast<int>(ph);
            const float frac = static_cast<float>(ph - static_cast<double>(i1));

            int i0 = i1 - 1;
            int i2 = i1 + 1;
            int i3 = i1 + 2;

            if (loop) {
                i0 = wrapIndex(i0, len);
                i2 = wrapIndex(i2, len);
                i3 = wrapIndex(i3, len);
            } else {
                i0 = std::max(0, i0);
                i2 = std::min(len - 1, i2);
                i3 = std::min(len - 1, i3);
            }

            return cubicHermite(src[i0], src[i1], src[i2], src[i3], frac);
        }

    } // namespace detail_interp

// ============================================================
// ClipTrackImpl (MVP)
// - 1 slot
// - Play/Stop via onRtCommand
// - gain/loop/speed via ParamSet (index 0/1/2) OR via control methods
// ============================================================

    class ClipTrackImpl final : public IClipTrack {
    public:
        ClipTrackImpl() = default;
        ~ClipTrackImpl() override = default;

        // ---- ITrack ----
        void addModule(std::unique_ptr<IAudioModule> /*mod*/) override {
            // MVP: ClipTrack без FX-цепочки. Можно позже добавить хранение модулей.
        }

        IAudioModule* getModule(std::size_t /*index*/) override {
            return nullptr;
        }

        void process(const AudioProcessContext& ctx) override {
            // RT boundary: apply any pending control updates
            rtApplyPending_();

            if (!ctx.out || ctx.nframes == 0) return;

            // Контракт не даёт numOut в ctx.
            // Для MVP считаем, что host даёт хотя бы 1 канал; второй — опционально.
            float* out0 = ctx.out[0];
            float* out1 = nullptr;
            // аккуратно: out[1] может быть невалиден, но в нормальном host cfg.numOutput=2
            // Если хочешь совсем безопасно — держи соглашение "всегда 2 канала".
            if (ctx.out[1]) out1 = ctx.out[1];

            const ClipBuffer* clip = rt_.clip;
            if (!rt_.playing || !clip || clip->frames <= 0 || !out0) return;

            const float* c0 = clip->ch[0];
            const float* c1 = (clip->channels == 2) ? clip->ch[1] : nullptr;

            double ph = rt_.playhead;
            const int len = clip->frames;
            const double lenD = static_cast<double>(len);
            const float g = rt_.gain;
            const bool loop = rt_.loop;
            const double inc = static_cast<double>(detail_interp::clampf(rt_.playbackInc, 0.05f, 8.0f));
            const int n = (int)ctx.nframes;

            // Mix (adds into out)
            for (int i = 0; i < n; ++i) {
                if (!loop && ph >= lenD) {
                    rt_.playing = false;
                    ph = 0.0;
                    break;
                }

                while (loop && ph >= lenD) ph -= lenD;

                const float src0 = detail_interp::sampleCubic(c0, len, ph, loop);
                const float s0 = src0 * g;
                out0[i] += s0;

                if (out1) {
                    const float src1 = c1 ? detail_interp::sampleCubic(c1, len, ph, loop) : src0;
                    const float s1 = src1 * g; // mono -> dual mono
                    out1[i] += s1;
                }

                ph += inc;
            }

            rt_.playhead = ph;
        }

        void onRtCommand(const RtCommand& cmd) noexcept override {
            rtApplyPending_();

            const CmdId cid = static_cast<CmdId>(cmd.id);

            // В RtCommand есть поля track/slot:
            // - engine обычно маршрутизирует по track, но на всякий случай считаем:
            //   если cmd.slot == -1 -> применяем к слоту 0 (MVP).
            const uint32_t slot = (cmd.slot < 0) ? 0u : (uint32_t)cmd.slot;
            if (slot != 0u) return; // MVP: один слот

            switch (cid) {
                case CmdId::Play: {
                    if (rt_.clip && rt_.clip->frames > 0) {
                        rt_.playhead = 0.0;
                        rt_.playing = true;
                    }
                } break;

                case CmdId::Stop: {
                    rt_.playing = false;
                    rt_.playhead = 0.0;
                } break;

                case CmdId::ParamSet: {
                    // контракт: RtCommand.index + RtCommand.value
                    // значения параметров по договору нормализованы [0..1]
                    if (cmd.index == 0) {
                        // gain (0..1) — MVP
                        rt_.gain = detail_interp::clampf(cmd.value, 0.0f, 1.0f);
                    } else if (cmd.index == 1) {
                        // loop bool
                        rt_.loop = (cmd.value >= 0.5f);
                    } else if (cmd.index == 2) {
                        // varispeed playback increment (1.0 = normal)
                        rt_.playbackInc = detail_interp::clampf(cmd.value, 0.05f, 8.0f);
                    }
                } break;

                case CmdId::RecArm:
                case CmdId::RecDisarm:
                case CmdId::Overdub:
                case CmdId::Clear:
                case CmdId::StopQuantized:
                case CmdId::QuantizeMode:
                default:
                    // MVP: не реализуем, но не падаем
                    break;
            }
        }

        // ---- IClipTrack ----
        uint32_t numSlots() const noexcept override { return 1; }

        bool loadSlotFromFile(uint32_t slot, const char* path) override {
            if (slot != 0u) return false;

            int sr = 0, ch = 0, frames = 0;
            std::unique_ptr<float[]> c0, c1;
            if (!detail_wav::decode_wav_to_planar_f32(path, sr, ch, frames, c0, c1)) {
                return false;
            }

            auto b = std::make_shared<ClipBuffer>();
            b->sampleRate = sr;
            b->channels   = ch;
            b->frames     = frames;
            b->ch0        = std::move(c0);
            b->ch1        = std::move(c1);
            b->ch[0]      = b->ch0.get();
            b->ch[1]      = (ch == 2) ? b->ch1.get() : nullptr;

            // Контракт: если слот был в воспроизведении, реализация должна безопасно остановить.
            // Мы останавливаем RT на границе блока через pendingPublish_.
            publishClip_(std::move(b));
            return true;
        }

        bool clearSlot(uint32_t slot) override {
            if (slot != 0u) return false;

            clipCtl_.reset();
            pendingClip_.store(nullptr, std::memory_order_release);
            pendingClear_.store(true, std::memory_order_release);
            return true;
        }

        bool armRecordSlot(uint32_t slot, bool on) override {
            if (slot != 0u) return false;
            // MVP: только флаг armed (память под запись пока не готовим)
            recArmed_.store(on, std::memory_order_relaxed);
            return true;
        }

        bool setSlotLengthInBars(uint32_t slot, uint32_t bars) override {
            if (slot != 0u) return false;
            if (bars < 1u) return false;

            slotBars_.store(bars, std::memory_order_relaxed);
            pendingPlaybackInc_.store(computePlaybackIncForBars_(), std::memory_order_release);
            return true;
        }

        bool setSlotLooping(uint32_t slot, bool loop) override {
            if (slot != 0u) return false;

            // Контракт: только вне RT. Мы публикуем флаг в RT через pendingLoop_.
            pendingLoop_.store(loop ? 1u : 0u, std::memory_order_release);
            return true;
        }

    private:
        struct ClipBuffer {
            int sampleRate = 0;
            int channels = 0; // 1 or 2
            int frames   = 0;

            std::unique_ptr<float[]> ch0;
            std::unique_ptr<float[]> ch1;
            const float* ch[2] = {nullptr, nullptr};
        };

        struct RtState {
            const ClipBuffer* clip = nullptr;
            double playhead = 0.0;
            float playbackInc = 1.0f;
            float gain = 1.0f;     // normalized 0..1 (MVP)
            bool playing = false;
            bool loop = false;
        };

    private:
        float computePlaybackIncForBars_() const noexcept {
            const ClipBuffer* clip = clipCtl_.get();
            if (!clip || clip->frames <= 0 || clip->sampleRate <= 0) {
                return 1.0f;
            }

            const uint32_t bars = std::max<uint32_t>(1, slotBars_.load(std::memory_order_relaxed));
            const float bpm = 120.0f;
            const uint8_t tsNum = 4;
            const uint8_t tsDen = 4;

            const double beatsPerBar = static_cast<double>(tsNum) * 4.0 / static_cast<double>(tsDen);
            const double targetFrames =
                    static_cast<double>(bars) * beatsPerBar * (60.0 / static_cast<double>(bpm)) *
                    static_cast<double>(clip->sampleRate);

            if (targetFrames <= 1.0) {
                return 1.0f;
            }

            const float inc = static_cast<float>(static_cast<double>(clip->frames) / targetFrames);
            return detail_interp::clampf(inc, 0.05f, 8.0f);
        }

        void publishClip_(std::shared_ptr<ClipBuffer>&& b) {
            // control thread only
            clipCtl_ = std::move(b);
            pendingClip_.store(clipCtl_.get(), std::memory_order_release);
            pendingClear_.store(false, std::memory_order_release);
        }

        void rtApplyPending_() noexcept {
            // Apply pending clear
            if (pendingClear_.exchange(false, std::memory_order_acq_rel)) {
                rt_.clip = nullptr;
                rt_.playing = false;
                rt_.playhead = 0.0;
            }

            // Apply pending clip publish
            if (const ClipBuffer* p = pendingClip_.exchange(nullptr, std::memory_order_acq_rel)) {
                rt_.clip = p;
                rt_.playing = false;  // безопасно: при смене клипа останавливаем
                rt_.playhead = 0.0;
            }

            // Apply pending loop set (from control method)
            const uint32_t pl = pendingLoop_.exchange(0xFFFFFFFFu, std::memory_order_acq_rel);
            if (pl != 0xFFFFFFFFu) {
                rt_.loop = (pl != 0u);
            }

            const float pendingInc = pendingPlaybackInc_.exchange(-1.0f, std::memory_order_acq_rel);
            if (pendingInc > 0.0f) {
                rt_.playbackInc = detail_interp::clampf(pendingInc, 0.05f, 8.0f);
            }
        }

    private:
        std::shared_ptr<ClipBuffer> clipCtl_; // “флешка с аудио”, которую держит control-мир.

        // В RT ждёт новый клип, который надо сделать текущим источником аудио
        // либо nullptr = “нет нового клипа”
        // либо указатель на ClipBuffer, который лежит внутри clipCtl_
        std::atomic<const ClipBuffer*> pendingClip_{nullptr};

        // Запрошен reset/очистка слота/воспроизведения в RT
        // То есть это “паническая кнопка”: остановить и убрать клип из RT.
        std::atomic<bool> pendingClear_{false};

        // 0xFFFFFFFF = "ничего не менять"; иначе 0/1
        std::atomic<uint32_t> pendingLoop_{0xFFFFFFFFu}; // В RT ждёт обновление параметра loop
        std::atomic<float> pendingPlaybackInc_{-1.0f};

        // record-arm (MVP)
        std::atomic<bool> recArmed_{false};
        std::atomic<uint32_t> slotBars_{4};

        RtState rt_{}; // Текущее состояние плеера
    };

} // namespace avantgarde
