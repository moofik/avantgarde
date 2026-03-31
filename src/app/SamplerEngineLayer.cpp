#include "app/SamplerEngineLayer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "contracts/IAudioEngine.h"
#include "contracts/IClipTrack.h"
#include "contracts/IParameterized.h"
#include "contracts/IPlatform.h"
#include "contracts/types.h"
#include "contracts/ids.h"
#include "control/ControlCommandDispatcher.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/TransportBridgeDualBuffer.h"

// Concrete runtime/platform impls are compiled into this TU intentionally.
#include "platform/macos/MacAudioHost.mm"
#include "runtime/AudioEngine.cpp"
#include "runtime/ClipTrack.cpp"
#include "runtime/ParamBridgeDualBuffer.cpp"
#include "runtime/RtCommandQueueSPSC.cpp"

namespace avantgarde {
namespace {

// Третий (скрытый) трек используется под preview.
constexpr int16_t kPreviewTrackId = 2;
// В текущем MVP UI/движок держит 2 пользовательских трека.
constexpr uint8_t kTrackCount = 2;

// Резолвер ParamBridge использует эти указатели для адресации модулей.
std::array<ITrack*, kTrackCount> gParamTracks{};

uint8_t clampTrack(uint8_t track) noexcept {
    return (track >= kTrackCount) ? static_cast<uint8_t>(kTrackCount - 1) : track;
}

IParameterized* resolveParamTarget(Target target) noexcept {
    if (target.trackId < 0 || static_cast<std::size_t>(target.trackId) >= gParamTracks.size()) {
        return nullptr;
    }
    if (target.slotId < 0) {
        return nullptr;
    }
    ITrack* tr = gParamTracks[static_cast<std::size_t>(target.trackId)];
    if (!tr) {
        return nullptr;
    }
    return tr->getModule(static_cast<std::size_t>(target.slotId));
}

std::string clipNameFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    if (const std::size_t pos = path.find_last_of("/\\"); pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

void renderThunk(AudioProcessContext& ctx, void* user) noexcept {
    // Колбэк аудиохоста -> в processBlock движка.
    auto* engine = static_cast<IAudioEngine*>(user);
    engine->processBlock(ctx);
}

} // namespace

struct SamplerEngineLayer::Impl {
    // Platform host (CoreAudio на macOS).
    MacAudioHost host{};
    // UI -> Scheduler очередь.
    RtCommandQueueSPSC qUi{};
    // Scheduler -> Engine RT очередь.
    RtCommandQueueSPSC qRt{};
    // Control->RT мост параметров.
    ParamBridgeDualBuffer pb{10};
    // Основной аудиодвижок.
    AudioEngine engine{&qRt, &pb};
    // Транспорт с двойной буферизацией.
    TransportBridgeDualBuffer transport{};
    // RT extension для квантизации команд.
    std::unique_ptr<QuantizedSchedulerRtExtension> scheduler{};
    // Отправка квантованных команд.
    ControlCommandDispatcher controlDispatcher{&qUi};
    // Отправка immediate команд в обход scheduler (preview).
    ControlCommandDispatcher immediateDispatcher{&qRt};
    // Сырые указатели на track instances (для non-RT операций загрузки).
    std::array<IClipTrack*, kTrackCount> clipCtl{nullptr, nullptr};
    // Указатель на preview-голос.
    IClipTrack* previewClip{nullptr};
    // Активный аудиострим.
    std::unique_ptr<IAudioStream> stream{};
    // Параметры открытия стрима.
    StreamConfig streamCfg{};
    // Guard-флаги жизненного цикла.
    bool initialized{false};
    bool running{false};
};

SamplerEngineLayer::SamplerEngineLayer()
    : impl_(new Impl()) {}

SamplerEngineLayer::~SamplerEngineLayer() {
    stop();
    delete impl_;
    impl_ = nullptr;
}

bool SamplerEngineLayer::init(const SamplerEngineConfig& config,
                              SamplerEngineBootstrap& bootstrap,
                              std::string& errorOut) {
    if (!impl_) {
        errorOut = "engine impl is null";
        return false;
    }
    if (config.track0Path.empty()) {
        errorOut = "track0 path is empty";
        return false;
    }

    impl_->engine.setSampleRate(config.sampleRate);

    // Создаем рабочие треки и preview-голос.
    auto clip0 = std::make_unique<ClipTrackImpl>();
    IClipTrack* clipPtr0 = clip0.get();
    auto clip1 = std::make_unique<ClipTrackImpl>();
    IClipTrack* clipPtr1 = clip1.get();
    auto previewClip = std::make_unique<ClipTrackImpl>();
    IClipTrack* previewClipPtr = previewClip.get();

    // T1 обязателен к загрузке.
    if (!clipPtr0->loadSlotFromFile(0, config.track0Path.c_str())) {
        errorOut = "loadSlotFromFile failed: " + config.track0Path;
        return false;
    }
    (void)clipPtr0->setSlotLooping(0, true);

    // T2 опционален.
    bool track1Loaded = false;
    if (config.hasTrack1) {
        track1Loaded = clipPtr1->loadSlotFromFile(0, config.track1Path.c_str());
        if (track1Loaded) {
            (void)clipPtr1->setSlotLooping(0, true);
        }
    }

    // Публикуем треки для resolver'а параметров.
    impl_->clipCtl[0] = clipPtr0;
    impl_->clipCtl[1] = clipPtr1;
    impl_->previewClip = previewClipPtr;
    gParamTracks[0] = clipPtr0;
    gParamTracks[1] = clipPtr1;
    impl_->pb.setResolver(&resolveParamTarget);

    // Регистрируем треки в engine (порядок важен: [T1, T2, Preview]).
    impl_->engine.registerTrack(std::move(clip0));
    impl_->engine.registerTrack(std::move(clip1));
    impl_->engine.registerTrack(std::move(previewClip));

    // Включаем транспорт и scheduler extension.
    impl_->engine.setTransportBridge(&impl_->transport);
    impl_->scheduler = std::make_unique<QuantizedSchedulerRtExtension>(
        &impl_->qUi, &impl_->qRt, &impl_->transport, config.sampleRate);
    impl_->engine.addRtExtension(impl_->scheduler.get());

    // Инициализируем стартовое состояние транспорта.
    bootstrap.transport.playing = false;
    bootstrap.transport.bpm = 120.0f;
    bootstrap.transport.tsNum = 4;
    bootstrap.transport.tsDen = 4;
    bootstrap.transport.quant = QuantizeMode::Bar;
    bootstrap.transport.activeTrack = 0;

    // Синхронизируем transport в control + RT слоях.
    impl_->transport.setTempo(bootstrap.transport.bpm);
    impl_->transport.setTimeSignature(bootstrap.transport.tsNum, bootstrap.transport.tsDen);
    impl_->transport.setQuantize(bootstrap.transport.quant);
    impl_->transport.setPlaying(bootstrap.transport.playing);

    // Стартовые команды идут через стандартный dispatcher-путь.
    impl_->controlDispatcher.setQuantizeMode(bootstrap.transport.quant);
    impl_->controlDispatcher.setTempoBpm(bootstrap.transport.bpm);
    impl_->controlDispatcher.setTimeSignature(bootstrap.transport.tsNum, bootstrap.transport.tsDen);

    // Preview: тише и без loop по умолчанию.
    impl_->immediateDispatcher.sendTrackParamSet(kPreviewTrackId, TrackParamId::Gain01, 0.25f);
    impl_->immediateDispatcher.sendTrackParamSet(kPreviewTrackId, TrackParamId::LoopEnabled, kRtValueOff);

    // Готовим стартовый UI state треков.
    UiTrackStateView track0{};
    track0.id = 0;
    track0.state = UiTrackState::Stopped;
    track0.bars = 4;
    track0.stretchRatio = 1.0f;
    track0.gain01 = 1.0f;
    track0.loop = true;
    track0.fxCount = 0;
    track0.clipName = clipNameFromPath(config.track0Path);
    bootstrap.tracks[0] = track0;

    UiTrackStateView track1{};
    track1.id = 1;
    track1.state = track1Loaded ? UiTrackState::Stopped : UiTrackState::Empty;
    track1.bars = 4;
    track1.stretchRatio = 1.0f;
    track1.gain01 = 1.0f;
    track1.loop = track1Loaded;
    track1.fxCount = 0;
    track1.clipName = track1Loaded ? clipNameFromPath(config.track1Path) : std::string{};
    bootstrap.tracks[1] = track1;

    // Конфиг потока хоста сохраняем до этапа start().
    impl_->streamCfg = StreamConfig{
        .sampleRate = static_cast<int>(config.sampleRate),
        .blockFrames = config.blockFrames,
        .numInput = config.numInput,
        .numOutput = config.numOutput
    };
    impl_->initialized = true;
    return true;
}

bool SamplerEngineLayer::start(std::string& errorOut) {
    if (!impl_ || !impl_->initialized) {
        errorOut = "engine is not initialized";
        return false;
    }
    if (impl_->running) {
        return true;
    }

    // Открытие физического устройства и запуск RT колбэка.
    impl_->stream = impl_->host.openStream(impl_->streamCfg, "default", "default");
    if (!impl_->stream) {
        errorOut = "openStream failed";
        return false;
    }
    impl_->engine.setNumOutput(static_cast<uint32_t>(impl_->stream->numOutput()));
    if (!impl_->stream->start(&renderThunk, &impl_->engine)) {
        errorOut = "stream->start failed";
        impl_->stream.reset();
        return false;
    }

    impl_->running = true;
    return true;
}

void SamplerEngineLayer::stop() noexcept {
    if (!impl_) {
        return;
    }
    // Сначала гасим preview, затем останавливаем стрим.
    previewStop();
    if (impl_->running && impl_->stream) {
        impl_->stream->stop();
        impl_->stream->close();
        impl_->stream.reset();
    }
    impl_->running = false;
}

SamplerEngineTelemetry SamplerEngineLayer::telemetryAndResetOverflow() noexcept {
    SamplerEngineTelemetry out{};
    if (!impl_ || !impl_->stream) {
        return out;
    }
    out.totalCallbacks = impl_->stream->totalCallbacks();
    out.xruns = impl_->stream->xruns();
    out.blockFrames = static_cast<uint32_t>(impl_->stream->blockFrames());
    // overflow флаги читаются и сразу сбрасываются.
    out.rtQueueOverflow =
        impl_->qUi.overflowFlagAndReset() ||
        impl_->qRt.overflowFlagAndReset() ||
        (impl_->scheduler ? impl_->scheduler->overflowFlagAndReset() : false);
    return out;
}

void SamplerEngineLayer::setTransportPlaying(bool playing) noexcept {
    if (!impl_) {
        return;
    }
    impl_->transport.setPlaying(playing);
}

void SamplerEngineLayer::setTempo(float bpm) noexcept {
    if (!impl_) {
        return;
    }
    // Обновляем и queue-команду, и control-копию транспорта.
    (void)impl_->controlDispatcher.setTempoBpm(bpm);
    impl_->transport.setTempo(bpm);
}

void SamplerEngineLayer::setQuantize(QuantizeMode q) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setQuantizeMode(q);
    impl_->transport.setQuantize(q);
}

void SamplerEngineLayer::setTimeSignature(uint8_t num, uint8_t den) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setTimeSignature(num, den);
    impl_->transport.setTimeSignature(num, den);
}

bool SamplerEngineLayer::playTrack(uint8_t track) noexcept {
    if (!impl_) {
        return false;
    }
    const uint8_t t = clampTrack(track);
    return impl_->controlDispatcher.sendPlay(static_cast<int16_t>(t), kRtClipSlot0);
}

bool SamplerEngineLayer::stopTrack(uint8_t track) noexcept {
    if (!impl_) {
        return false;
    }
    const uint8_t t = clampTrack(track);
    return impl_->controlDispatcher.sendStop(static_cast<int16_t>(t), kRtClipSlot0);
}

bool SamplerEngineLayer::setTrackSpeed(uint8_t track, float speed) noexcept {
    if (!impl_) {
        return false;
    }
    const uint8_t t = clampTrack(track);
    return impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::PlaybackInc, speed);
}

bool SamplerEngineLayer::loadSampleToTrack(uint8_t track,
                                           const std::string& path,
                                           std::string& clipNameOut) noexcept {
    if (!impl_ || path.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track);
    if (!impl_->clipCtl[t]) {
        return false;
    }
    // Load и loop-настройка строго вне RT.
    if (!impl_->clipCtl[t]->loadSlotFromFile(0, path.c_str())) {
        return false;
    }
    (void)impl_->clipCtl[t]->setSlotLooping(0, true);
    clipNameOut = clipNameFromPath(path);
    return true;
}

void SamplerEngineLayer::previewRequest(const std::string& path) noexcept {
    if (!impl_) {
        return;
    }
    // Любой новый preview начинается с мгновенного stop предыдущего.
    (void)impl_->immediateDispatcher.sendStop(kPreviewTrackId, kRtClipSlot0);
    if (path.empty() || !impl_->previewClip) {
        return;
    }
    if (!impl_->previewClip->loadSlotFromFile(0, path.c_str())) {
        return;
    }
    (void)impl_->previewClip->setSlotLooping(0, false);
    (void)impl_->immediateDispatcher.sendPlay(kPreviewTrackId, kRtClipSlot0);
}

void SamplerEngineLayer::previewStop() noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->immediateDispatcher.sendStop(kPreviewTrackId, kRtClipSlot0);
}

} // namespace avantgarde
