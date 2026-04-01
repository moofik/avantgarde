#include "app/SamplerEngineLayer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "contracts/IAudioEngine.h"
#include "contracts/IClipTrack.h"
#include "contracts/IParameterized.h"
#include "contracts/IPlatform.h"
#include "contracts/types.h"
#include "contracts/ids.h"
#include "control/ControlCommandDispatcher.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/TransportBridgeDualBuffer.h"

// Concrete runtime impls are compiled into this TU intentionally.
#include "runtime/AudioEngine.cpp"
#include "runtime/ClipTrack.cpp"
#include "runtime/ParamBridgeDualBuffer.cpp"
#include "runtime/RtCommandQueueSPSC.cpp"

namespace avantgarde {
namespace {

constexpr uint8_t kMinTrackCount = 1;
constexpr uint8_t kMaxTrackCount = 32;

// Резолвер ParamBridge использует эти указатели для адресации модулей.
std::vector<ITrack*> gParamTracks{};

uint8_t sanitizeTrackCount(uint8_t trackCount) noexcept {
    if (trackCount < kMinTrackCount) {
        return kMinTrackCount;
    }
    if (trackCount > kMaxTrackCount) {
        return kMaxTrackCount;
    }
    return trackCount;
}

uint8_t clampTrack(uint8_t track, uint8_t trackCount) noexcept {
    const uint8_t safeCount = sanitizeTrackCount(trackCount);
    return (track >= safeCount) ? static_cast<uint8_t>(safeCount - 1) : track;
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
    // Абстрактный платформенный хост, инжектируется извне.
    std::shared_ptr<IAudioHost> host{};
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
    // Количество пользовательских треков.
    uint8_t trackCount{0};
    // ID скрытого preview-трека внутри AudioEngine.
    int16_t previewTrackId{-1};
    // Сырые указатели на track instances (для non-RT операций загрузки).
    std::vector<IClipTrack*> clipCtl{};
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
                              const std::shared_ptr<IAudioHost>& audioHost,
                              UiState& bootstrapOut,
                              std::string& errorOut) {
    if (!impl_) {
        errorOut = "engine impl is null";
        return false;
    }
    if (!audioHost) {
        errorOut = "audio host is null";
        return false;
    }

    // Внедряем абстрактный host через контракт.
    // SamplerEngineLayer не знает о Mac/ALSA/JACK реализациях.
    impl_->host = audioHost;

    impl_->engine.setSampleRate(config.sampleRate);
    impl_->trackCount = sanitizeTrackCount(config.trackCount);
    impl_->previewTrackId = static_cast<int16_t>(impl_->trackCount);

    // Создаем пользовательские треки и preview-голос.
    std::vector<std::unique_ptr<IClipTrack>> userTracks(impl_->trackCount);
    std::vector<IClipTrack*> userTrackPtrs(impl_->trackCount, nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        userTracks[t] = std::make_unique<ClipTrackImpl>();
        userTrackPtrs[t] = userTracks[t].get();
    }
    auto previewClip = std::make_unique<ClipTrackImpl>();
    IClipTrack* previewClipPtr = previewClip.get();

    // Публикуем треки для resolver'а параметров.
    impl_->clipCtl.assign(impl_->trackCount, nullptr);
    gParamTracks.assign(impl_->trackCount, nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->clipCtl[t] = userTrackPtrs[t];
        gParamTracks[t] = userTrackPtrs[t];
    }
    impl_->previewClip = previewClipPtr;
    impl_->pb.setResolver(&resolveParamTarget);

    // Регистрируем треки в engine (порядок важен: [T1..Tn, Preview]).
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->engine.registerTrack(std::move(userTracks[t]));
    }
    impl_->engine.registerTrack(std::move(previewClip));

    // Включаем транспорт и scheduler extension.
    impl_->engine.setTransportBridge(&impl_->transport);
    impl_->scheduler = std::make_unique<QuantizedSchedulerRtExtension>(
        &impl_->qUi, &impl_->qRt, &impl_->transport, config.sampleRate);
    impl_->engine.addRtExtension(impl_->scheduler.get());

    // Инициализируем стартовое состояние транспорта.
    bootstrapOut.transport.playing = false;
    bootstrapOut.transport.bpm = 120.0f;
    bootstrapOut.transport.tsNum = 4;
    bootstrapOut.transport.tsDen = 4;
    bootstrapOut.transport.quant = QuantizeMode::Bar;
    bootstrapOut.transport.activeTrack = 0;

    // Синхронизируем transport в control + RT слоях.
    impl_->transport.setTempo(bootstrapOut.transport.bpm);
    impl_->transport.setTimeSignature(bootstrapOut.transport.tsNum, bootstrapOut.transport.tsDen);
    impl_->transport.setQuantize(bootstrapOut.transport.quant);
    impl_->transport.setPlaying(bootstrapOut.transport.playing);

    // Стартовые команды идут через стандартный dispatcher-путь.
    impl_->controlDispatcher.setQuantizeMode(bootstrapOut.transport.quant);
    impl_->controlDispatcher.setTempoBpm(bootstrapOut.transport.bpm);
    impl_->controlDispatcher.setTimeSignature(bootstrapOut.transport.tsNum, bootstrapOut.transport.tsDen);

    // Пользовательские треки следуют global transport и стартуют unmuted/disarmed.
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::FollowTransportEnabled, kRtValueOn);
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::MuteEnabled, kRtValueOff);
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::ArmEnabled, kRtValueOff);
    }

    // Preview-голос: отдельный one-shot режим (не следует global transport).
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::Gain01, 0.25f);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::LoopEnabled, kRtValueOff);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::FollowTransportEnabled, kRtValueOff);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::MuteEnabled, kRtValueOn);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::ArmEnabled, kRtValueOff);

    // Готовим стартовый UI state треков (без автозагрузки файлов).
    bootstrapOut.tracks.clear();
    bootstrapOut.tracks.resize(impl_->trackCount);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        UiTrackStateView track{};
        track.id = t;
        track.state = UiTrackState::Empty;
        track.bars = 4;
        track.stretchRatio = 1.0f;
        track.gain01 = 1.0f;
        track.muted = false;
        track.armed = false;
        track.loop = false;
        track.fxCount = 0;
        track.fxChainIds.clear();
        track.clipName.clear();
        bootstrapOut.tracks[t] = std::move(track);
    }

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

    if (!impl_->host) {
        errorOut = "audio host is null";
        return false;
    }

    // Открытие физического устройства и запуск RT колбэка.
    // Здесь только контракт IAudioHost, без platform include'ов.
    impl_->stream = impl_->host->openStream(impl_->streamCfg, "default", "default");
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

    // Прокидываем global Play/Stop в треки (чтобы user tracks следовали transport gate).
    RtCommand cmd{};
    cmd.id = toWireCmdId(playing ? CmdId::Play : CmdId::Stop);
    cmd.track = kRtTrackGlobal;
    cmd.slot = kRtSlotTrackParams;
    cmd.index = kRtIndexUnused;
    cmd.value = playing ? kRtValueOn : kRtValueOff;
    (void)impl_->qRt.push(cmd);
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

bool SamplerEngineLayer::setTrackMuted(uint8_t track, bool muted) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    return impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::MuteEnabled, muted ? kRtValueOn : kRtValueOff);
}

bool SamplerEngineLayer::setTrackArmed(uint8_t track, bool armed) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    return impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::ArmEnabled, armed ? kRtValueOn : kRtValueOff);
}

bool SamplerEngineLayer::setTrackSpeed(uint8_t track, float speed) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    return impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::PlaybackInc, speed);
}

bool SamplerEngineLayer::addFxToTrack(uint8_t track, std::unique_ptr<IAudioModule> module) noexcept {
    if (!impl_ || !module || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    // addModule вызывается строго вне RT.
    impl_->clipCtl[t]->addModule(std::move(module));
    return true;
}

bool SamplerEngineLayer::removeFxFromTrack(uint8_t track, uint8_t fxSlot) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    // Защита от невалидного слота до удаления.
    if (!impl_->clipCtl[t]->getModule(static_cast<std::size_t>(fxSlot))) {
        return false;
    }
    // removeModuleAt вызывается только вне RT.
    return impl_->clipCtl[t]->removeModuleAt(static_cast<std::size_t>(fxSlot));
}

bool SamplerEngineLayer::setFxParam(uint8_t track,
                                    uint8_t fxSlot,
                                    uint16_t paramIndex,
                                    float normalizedValue) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    if (!impl_->clipCtl[t]->getModule(static_cast<std::size_t>(fxSlot))) {
        return false;
    }
    return impl_->controlDispatcher.sendParamSet(
        static_cast<int16_t>(t),
        static_cast<int16_t>(fxSlot),
        paramIndex,
        normalizedValue);
}

bool SamplerEngineLayer::loadSampleToTrack(uint8_t track,
                                           const std::string& path,
                                           std::string& clipNameOut) noexcept {
    if (!impl_ || path.empty()) {
        return false;
    }
    if (impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
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
    if (impl_->previewTrackId < 0) {
        return;
    }
    (void)impl_->immediateDispatcher.sendStop(impl_->previewTrackId, kRtClipSlot0);
    (void)impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::MuteEnabled, kRtValueOn);
    if (path.empty() || !impl_->previewClip || !impl_->previewClip->healthcheck()) {
        return;
    }
    if (!impl_->previewClip->loadSlotFromFile(0, path.c_str())) {
        return;
    }
    (void)impl_->previewClip->setSlotLooping(0, false);
    (void)impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::MuteEnabled, kRtValueOff);
    (void)impl_->immediateDispatcher.sendPlay(impl_->previewTrackId, kRtClipSlot0);
}

void SamplerEngineLayer::previewStop() noexcept {
    if (!impl_) {
        return;
    }
    if (impl_->previewTrackId < 0) {
        return;
    }
    (void)impl_->immediateDispatcher.sendStop(impl_->previewTrackId, kRtClipSlot0);
    (void)impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::MuteEnabled, kRtValueOn);
}

} // namespace avantgarde
