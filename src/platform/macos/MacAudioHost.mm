// src/platform/macos/MacAudioHost.mm  (Objective-C++)
//
// Платформенный слой аудио для macOS:
// - реализует IAudioHost/IAudioStream поверх CoreAudio HAL Output AudioUnit
// - изолирует все Apple API внутри src/platform
// - экспортирует только контрактный entrypoint createDefaultAudioHost()

#include "contracts/IPlatform.h"
#include <atomic>
#include <memory>
#include <vector>
#include <string>

#import <AudioUnit/AudioUnit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

namespace avantgarde {

// -------- helpers --------

    static AudioDeviceID DefaultInputDevice() {
        AudioDeviceID dev = kAudioObjectUnknown;
        UInt32 size = sizeof(dev);
        AudioObjectPropertyAddress addr{
                kAudioHardwarePropertyDefaultInputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev);
        return dev;
    }

    static AudioDeviceID DefaultOutputDevice() {
        AudioDeviceID dev = kAudioObjectUnknown;
        UInt32 size = sizeof(dev);
        AudioObjectPropertyAddress addr{
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev);
        return dev;
    }

    static AudioStreamBasicDescription MakeASBD_F32_NonInterleaved(int sr, int ch) {
        AudioStreamBasicDescription asbd{};
        asbd.mSampleRate       = (Float64)sr;
        asbd.mFormatID         = kAudioFormatLinearPCM;
        asbd.mFormatFlags      = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        asbd.mFramesPerPacket  = 1;
        asbd.mChannelsPerFrame = (UInt32)ch;
        asbd.mBitsPerChannel   = 8 * sizeof(float);
        asbd.mBytesPerFrame    = sizeof(float);
        asbd.mBytesPerPacket   = sizeof(float);
        return asbd;
    }

// -------- stream --------
// Реализация одного открытого аудио-потока.
// Жизненный цикл:
// 1) start() -> setup AudioUnit + запуск callback
// 2) RenderCB() вызывается в аудио-потоке и передает буферы в AudioEngine
// 3) stop()/close() останавливают и освобождают системные ресурсы

    class MacAudioStream final : public IAudioStream {
    public:
        // cfg хранится как фактическая конфигурация потока.
        // inDev/outDev и notify колбэки подготовлены для будущего расширения enumerate.
        MacAudioStream(const StreamConfig& cfg,
                       AudioDeviceID inDev,
                       AudioDeviceID outDev,
                       NonRtNotifyCb notify,
                       void* notifyUser)
                : cfg_(cfg), inDev_(inDev), outDev_(outDev), notify_(notify), notifyUser_(notifyUser) {}

        bool start(AudioRenderCb render, void* user) noexcept override {
                render_ = render;
                user_   = user;

                if (!setupUnit()) return false;
                if (AudioUnitInitialize(unit_) != noErr) return false;
                if (AudioOutputUnitStart(unit_) != noErr) return false;

                running_.store(true, std::memory_order_release);
                return true;
        }

        void stop() noexcept override {
                running_.store(false, std::memory_order_release);
                if (unit_) AudioOutputUnitStop(unit_);
        }

        void close() noexcept override {
                stop();
                if (unit_) {
                    AudioUnitUninitialize(unit_);
                    AudioComponentInstanceDispose(unit_);
                    unit_ = nullptr;
                }
                freeInputABL();
        }

        int sampleRate()  const noexcept override { return cfg_.sampleRate; }
        int blockFrames() const noexcept override { return cfg_.blockFrames; }
        int numInput()    const noexcept override { return cfg_.numInput; }
        int numOutput()   const noexcept override { return cfg_.numOutput; }
        uint64_t totalCallbacks() const noexcept override { return totalCb_.load(); }
        uint64_t xruns()          const noexcept override { return xruns_.load(); }

        ~MacAudioStream() override { close(); }

    private:
        // CoreAudio render callback.
        // ВАЖНО: вызывается в RT-потоке, никаких блокировок/аллоков в "горячем" пути.
        static OSStatus RenderCB(void* inRefCon,
                                 AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 /*inBusNumber*/,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData) {
            auto* self = static_cast<MacAudioStream*>(inRefCon);
            if (!self || !self->running_.load(std::memory_order_acquire)) return noErr;

            self->totalCb_.fetch_add(1, std::memory_order_relaxed);

            // 1) output pointers from ioData (planar)
            self->outPtrs_.clear();
            self->outPtrs_.reserve(ioData->mNumberBuffers);
            for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
                self->outPtrs_.push_back((float*)ioData->mBuffers[i].mData);
            }

            // 2) input: pull from input bus via AudioUnitRender (only if cfg_.numInput > 0)
            self->inPtrs_.clear();
            if (self->cfg_.numInput > 0) {
                // Ensure ABL allocated for requested frames
                if (!self->inAbl_ || (int)inNumberFrames > self->inAblFrames_) {
                    self->allocInputABL((int)inNumberFrames);
                }

                OSStatus st = AudioUnitRender(self->unit_,
                                              nullptr,
                                              inTimeStamp,
                                              1, // INPUT bus for HAL output is usually bus 1
                                              inNumberFrames,
                                              self->inAbl_);
                if (st != noErr) {
                    self->xruns_.fetch_add(1, std::memory_order_relaxed);
                    // on error: treat input as silence
                } else {
                    for (UInt32 i = 0; i < self->inAbl_->mNumberBuffers; ++i) {
                        self->inPtrs_.push_back((const float*)self->inAbl_->mBuffers[i].mData);
                    }
                }
            }

            // 3) build AudioProcessContext
            AudioProcessContext ctx{};
            ctx.in      = self->inPtrs_.empty() ? nullptr : self->inPtrs_.data();
            ctx.out     = self->outPtrs_.data();
            ctx.numOut  = static_cast<uint32_t>(ioData ? ioData->mNumberBuffers : 0U);
            ctx.nframes = (std::size_t)inNumberFrames;

            // 4) Передаем сформированный контекст в движок.
            if (self->render_) self->render_(ctx, self->user_);
            return noErr;
        }

        // Конфигурирует HAL Output AudioUnit: каналы, формат, callback, device.
        bool setupUnit() {
            // HAL Output AudioUnit
            AudioComponentDescription desc{};
            desc.componentType         = kAudioUnitType_Output;
            desc.componentSubType      = kAudioUnitSubType_HALOutput;
            desc.componentManufacturer = kAudioUnitManufacturer_Apple;

            AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
            if (!comp) return false;
            if (AudioComponentInstanceNew(comp, &unit_) != noErr) return false;

            // Enable output on bus 0
            UInt32 enableIO = 1;
            if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                     kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO)) != noErr)
                return false;

            // Enable input on bus 1 if requested
            if (cfg_.numInput > 0) {
                if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                         kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO)) != noErr)
                    return false;
            } else {
                UInt32 disableIO = 0;
                AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                     kAudioUnitScope_Input, 1, &disableIO, sizeof(disableIO));
            }

            // Select devices
            // NOTE: On macOS HALOutput uses kAudioOutputUnitProperty_CurrentDevice
            // and you can only set ONE device (combined input/output) unless you do aggregate devices.
            // For MVP: use output device as "current" and accept that input is from default input.
            AudioDeviceID dev = outDev_ ? outDev_ : DefaultOutputDevice();
            AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice,
                                 kAudioUnitScope_Global, 0, &dev, sizeof(dev));

            // Set stream formats (non-interleaved float32)
            const auto outAsbd = MakeASBD_F32_NonInterleaved(cfg_.sampleRate, cfg_.numOutput);
            if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                     kAudioUnitScope_Input, 0, &outAsbd, sizeof(outAsbd)) != noErr)
                return false;

            if (cfg_.numInput > 0) {
                const auto inAsbd = MakeASBD_F32_NonInterleaved(cfg_.sampleRate, cfg_.numInput);
                if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                         kAudioUnitScope_Output, 1, &inAsbd, sizeof(inAsbd)) != noErr)
                    return false;
            }

            // Set render callback
            AURenderCallbackStruct cb{};
            cb.inputProc       = &RenderCB;
            cb.inputProcRefCon = this;
            if (AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                                     kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr)
                return false;

            return true;
        }

        // Выделение AudioBufferList под входные каналы.
        // Хранится отдельно, чтобы переиспользовать между callback-вызовами.
        void allocInputABL(int frames) {
            freeInputABL();
            inAblFrames_ = frames;

            const int ch = cfg_.numInput;
            // allocate ABL with ch buffers
            const size_t ablSize = offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * ch;
            inAbl_ = (AudioBufferList*)malloc(ablSize);
            inAbl_->mNumberBuffers = (UInt32)ch;

            for (int i = 0; i < ch; ++i) {
                inAbl_->mBuffers[i].mNumberChannels = 1;
                inAbl_->mBuffers[i].mDataByteSize   = (UInt32)(frames * sizeof(float));
                inAbl_->mBuffers[i].mData           = malloc(frames * sizeof(float));
            }
        }

        // Освобождение входного AudioBufferList.
        void freeInputABL() {
            if (!inAbl_) return;
            for (UInt32 i = 0; i < inAbl_->mNumberBuffers; ++i) {
                if (inAbl_->mBuffers[i].mData) free(inAbl_->mBuffers[i].mData);
            }
            free(inAbl_);
            inAbl_ = nullptr;
            inAblFrames_ = 0;
        }

    private:
        // Пользовательская конфигурация потока.
        StreamConfig cfg_{};
        // Выбранные устройства (MVP: дефолтные).
        AudioDeviceID inDev_ = 0;
        AudioDeviceID outDev_ = 0;
        // Канал уведомлений вне RT (пока зарезервирован, не используется в MVP).
        NonRtNotifyCb notify_ = nullptr;
        void* notifyUser_ = nullptr;

        // Экземпляр CoreAudio AudioUnit.
        AudioComponentInstance unit_ = nullptr;

        // Флаги/метрики, читаемые из non-RT слоя.
        std::atomic<bool> running_{false};
        std::atomic<uint64_t> totalCb_{0};
        std::atomic<uint64_t> xruns_{0};

        // Callback движка и user pointer для render thunk.
        AudioRenderCb render_ = nullptr;
        void* user_ = nullptr;

        // Временные векторы указателей каналов (planar float).
        std::vector<const float*> inPtrs_;
        std::vector<float*> outPtrs_;

        // Буфер-лист входа для AudioUnitRender на input bus.
        AudioBufferList* inAbl_ = nullptr;
        int inAblFrames_ = 0;
    };

// -------- host --------
// Фабрика/входная точка platform-слоя: создает потоки и перечисляет устройства.

    class MacAudioHost final : public IAudioHost {
    public:
        std::vector<AudioDeviceInfo> enumerate() override {
            // MVP: return only "default" devices
            // (полную enumerate можно сделать позже, но контракт требует внятный список)
            std::vector<AudioDeviceInfo> out;
            AudioDeviceInfo defOut{};
            defOut.id = "default";
            defOut.name = "Default (CoreAudio)";
            defOut.maxInput = 2;
            defOut.maxOutput = 2;
            defOut.defaultSampleRate = 48000;
            defOut.isDefault = true;
            out.push_back(defOut);
            return out;
        }

        std::unique_ptr<IAudioStream> openStream(const StreamConfig& cfg,
                                                 const std::string& inputDeviceId,
                                                 const std::string& outputDeviceId,
                                                 NonRtNotifyCb onNotify = nullptr,
                                                 void* notifyUser = nullptr) override {
            // MVP: map "default" -> default devices
            (void)inputDeviceId; (void)outputDeviceId;
            AudioDeviceID inDev  = DefaultInputDevice();
            AudioDeviceID outDev = DefaultOutputDevice();
            return std::make_unique<MacAudioStream>(cfg, inDev, outDev, onNotify, notifyUser);
        }
    };

    // Контрактный entrypoint: отдает дефолтный хост текущей платформы.
    std::shared_ptr<IAudioHost> createDefaultAudioHost() {
        return std::make_shared<MacAudioHost>();
    }

} // namespace avantgarde
