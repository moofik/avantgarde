#include "app/SamplerEngineLayer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/IAudioEngine.h"
#include "contracts/IClipTrack.h"
#include "contracts/IParameterized.h"
#include "contracts/IPlatform.h"
#include "contracts/types.h"
#include "contracts/ids.h"
#include "contracts/IPattern.h"
#include "control/ControlCommandDispatcher.h"
#include "app/SamplerEnginePatternApplyTarget.h"
#include "runtime/QuantizedSchedulerRtExtension.h"
#include "runtime/PatternSchedulerRtExtension.h"
#include "runtime/MetronomeRtExtension.h"
#include "runtime/TransportBridgeDualBuffer.h"
#include "service/pattern/ClipBufferPool.h"
#include "service/pattern/PatternEngine.h"
#include "service/pattern/PatternSwitchPlanApplier.h"

// Concrete runtime impls are compiled into this TU intentionally.
#include "runtime/AudioEngine.cpp"
#include "runtime/ClipTrack.cpp"
#include "runtime/ParamBridgeDualBuffer.cpp"
#include "runtime/RtCommandQueueSPSC.cpp"

namespace avantgarde {
namespace {

constexpr uint8_t kMinTrackCount = 1;
constexpr uint8_t kMaxTrackCount = 32;

// AVANTGARDE_QUANT_DEFAULT_TUNE:
// Быстрая ручка UX для дефолтов квантизации.
// Меняй эти константы, если нужно вернуть более "жесткий" BAR-синк.
constexpr QuantizeMode kDefaultTransportQuantize = QuantizeMode::Beat;
constexpr QuantizeMode kDefaultPatternSwitchQuantize = QuantizeMode::Beat;

// Резолвер ParamBridge использует эти указатели для адресации модулей.
std::vector<ITrack*> gParamTracks{};
IParameterized* gParamGlobalTransport{nullptr};

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

float tempoToNorm(float bpm) noexcept {
    constexpr float kMinTempo = 20.0f;
    constexpr float kMaxTempo = 300.0f;
    const float clamped = std::clamp(bpm, kMinTempo, kMaxTempo);
    return (clamped - kMinTempo) / (kMaxTempo - kMinTempo);
}

float tsNumToNorm(uint8_t num) noexcept {
    const int n = std::clamp<int>(static_cast<int>(num), 1, 32);
    return static_cast<float>(n - 1) / 31.0f;
}

float tsDenToNorm(uint8_t den) noexcept {
    constexpr std::array<uint8_t, 6> kAllowed{{1, 2, 4, 8, 16, 32}};
    for (std::size_t i = 0; i < kAllowed.size(); ++i) {
        if (kAllowed[i] == den) {
            return static_cast<float>(i) / static_cast<float>(kAllowed.size() - 1);
        }
    }
    return 0.4f; // индекс для 4/4 в сетке {1,2,4,8,16,32}
}

float quantToNorm(QuantizeMode q) noexcept {
    switch (q) {
        case QuantizeMode::None: return 0.0f;
        case QuantizeMode::Beat: return 0.5f;
        case QuantizeMode::Bar:
        default:
            return 1.0f;
    }
}

IParameterized* resolveParamTarget(Target target) noexcept {
    // Global transport surface:
    // Target{trackId = -1, slotId = -1} -> ITransportBridge(IParameterized)
    if (target.trackId == kRtTrackGlobal && target.slotId == kRtSlotTrackParams) {
        return gParamGlobalTransport;
    }

    if (target.trackId < 0 || static_cast<std::size_t>(target.trackId) >= gParamTracks.size()) {
        return nullptr;
    }

    ITrack* tr = gParamTracks[static_cast<std::size_t>(target.trackId)];
    if (!tr) {
        return nullptr;
    }

    // Track-local surface:
    // Target{trackId = N, slotId = -1} -> ITrack(IParameterized)
    if (target.slotId == kRtSlotTrackParams) {
        return tr;
    }

    // FX-local surface:
    // Target{trackId = N, slotId >= 0} -> IAudioModule(IParameterized)
    if (target.slotId >= 0) {
        return tr->getModule(static_cast<std::size_t>(target.slotId));
    }
    return nullptr;
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

PatternState makeDefaultPattern(PatternId id,
                                uint8_t trackCount,
                                float bpm,
                                uint8_t tsNum,
                                uint8_t tsDen,
                                QuantizeMode quant) {
    PatternState p{};
    p.id = id;
    p.transport.bpm = bpm;
    p.transport.tsNum = tsNum;
    p.transport.tsDen = tsDen;
    p.transport.quant = quant;
    p.transport.swing01 = 0.0f;
    p.lengthInSteps = 64;
    p.stepsPerBeat = 4;
    p.tracks.reserve(trackCount);
    for (uint8_t t = 0; t < trackCount; ++t) {
        PatternTrackSnapshot tr{};
        tr.trackId = t;
        tr.muted = false;
        tr.armed = false;
        tr.gain01 = 1.0f;
        tr.playbackInc = 1.0f;
        tr.bars = 4u;
        tr.clipRefId = 0u;
        p.tracks.push_back(std::move(tr));
    }
    return p;
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
    // RT extension для тайминга pattern switch по transport grid.
    std::unique_ptr<PatternSchedulerRtExtension> patternRtExt{};
    // RT extension встроенного метронома (клик по сетке 1/16).
    std::unique_ptr<MetronomeRtExtension> metronomeRtExt{};
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
    // Preloaded PCM-пул клипов по clipRefId (без IO на switch).
    ClipBufferPool clipPool{};
    // Кэш path -> clipRefId для повторных загрузок без повторного декодирования.
    std::unordered_map<std::string, uint32_t> clipPathToRef{};
    // Обратный индекс clipRefId -> оригинальный path.
    std::unordered_map<uint32_t, std::string> clipRefToPath{};
    // Генератор clipRefId для runtime-сессии.
    uint32_t nextClipRef{1};
    // Теневая control-копия transport state для snapshot capture.
    float tempoBpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{kDefaultTransportQuantize};
    float swing01{0.0f};
    bool metronomeEnabled{false};
    // Теневая control-копия track state для snapshot capture.
    std::vector<bool> trackMuted{};
    std::vector<bool> trackArmed{};
    std::vector<float> trackGain{};
    std::vector<float> trackPlaybackInc{};
    std::vector<uint32_t> trackBars{};
    std::vector<uint32_t> trackClipRef{};
    // Pattern engine: bank + scheduler + snapshots.
    std::unique_ptr<PatternEngine> patternEngine{};
    // Адаптер применения switch-плана к SamplerEngineLayer API.
    std::unique_ptr<SamplerEnginePatternApplyTarget> patternApplyTarget{};
    // Канонический порядок pattern id для relative switch (prev/next).
    std::vector<PatternId> patternOrder{};
    // Pending pattern, ожидающий применения.
    PatternId pendingPatternId{kInvalidPatternId};
    // Флаг, что pending pattern существует.
    bool patternArmed{false};
    // Guard-флаги жизненного цикла.
    bool initialized{false};
    bool running{false};
};

SamplerEngineLayer::SamplerEngineLayer()
    : impl_(new Impl()) {}

SamplerEngineLayer::~SamplerEngineLayer() {
    stop();
    gParamGlobalTransport = nullptr;
    gParamTracks.clear();
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
    impl_->tempoBpm = 120.0f;
    impl_->tsNum = 4;
    impl_->tsDen = 4;
    impl_->quant = kDefaultTransportQuantize;
    impl_->swing01 = 0.0f;
    impl_->metronomeEnabled = false;
    impl_->trackMuted.assign(impl_->trackCount, false);
    impl_->trackArmed.assign(impl_->trackCount, false);
    impl_->trackGain.assign(impl_->trackCount, 1.0f);
    impl_->trackPlaybackInc.assign(impl_->trackCount, 1.0f);
    impl_->trackBars.assign(impl_->trackCount, 4u);
    impl_->trackClipRef.assign(impl_->trackCount, 0u);

    // Создаем пользовательские треки и preview-голос.
    std::vector<std::unique_ptr<IClipTrack>> userTracks(impl_->trackCount);
    std::vector<IClipTrack*> userTrackPtrs(impl_->trackCount, nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        userTracks[t] = std::make_unique<ClipTrackImpl>(config.sampleRate);
        userTrackPtrs[t] = userTracks[t].get();
    }
    auto previewClip = std::make_unique<ClipTrackImpl>(config.sampleRate);
    IClipTrack* previewClipPtr = previewClip.get();

    // Публикуем треки для resolver'а параметров.
    impl_->clipCtl.assign(impl_->trackCount, nullptr);
    gParamTracks.assign(static_cast<std::size_t>(impl_->trackCount) + 1u, nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->clipCtl[t] = userTrackPtrs[t];
        gParamTracks[t] = userTrackPtrs[t];
    }
    gParamTracks[impl_->trackCount] = previewClipPtr;
    impl_->previewClip = previewClipPtr;
    gParamGlobalTransport = &impl_->transport;
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

    // Pattern-подсистема:
    // - scheduler живет в PatternEngine;
    // - RT extension только прокидывает transport time и публикует ready switch id;
    // - apply-план строится/применяется в control-thread.
    impl_->patternEngine = std::make_unique<PatternEngine>(config.sampleRate);
    impl_->patternRtExt = std::make_unique<PatternSchedulerRtExtension>(
        &impl_->patternEngine->scheduler(),
        &impl_->transport);
    impl_->engine.addRtExtension(impl_->patternRtExt.get());
    impl_->metronomeRtExt = std::make_unique<MetronomeRtExtension>(
        config.sampleRate,
        static_cast<uint32_t>(std::max(1, config.numOutput)));
    impl_->engine.addRtExtension(impl_->metronomeRtExt.get());
    impl_->patternApplyTarget = std::make_unique<SamplerEnginePatternApplyTarget>(*this);
    impl_->patternOrder.clear();
    for (PatternId id = 1; id <= 4; ++id) {
        PatternState p = makeDefaultPattern(id,
                                            impl_->trackCount,
                                            120.0f,
                                            4,
                                            4,
                                            kDefaultTransportQuantize);
        if (impl_->patternEngine->putPattern(p)) {
            impl_->patternOrder.push_back(id);
        }
    }
    if (!impl_->patternOrder.empty()) {
        (void)impl_->patternEngine->setActivePattern(impl_->patternOrder.front());
    }
    impl_->pendingPatternId = kInvalidPatternId;
    impl_->patternArmed = false;

    // Инициализируем стартовое состояние транспорта.
    bootstrapOut.transport.playing = false;
    bootstrapOut.transport.bpm = 120.0f;
    bootstrapOut.transport.tsNum = 4;
    bootstrapOut.transport.tsDen = 4;
    bootstrapOut.transport.quant = kDefaultTransportQuantize;
    bootstrapOut.transport.metronomeEnabled = false;
    bootstrapOut.transport.activeTrack = 0;
    bootstrapOut.pattern.activeId = impl_->patternEngine
                                        ? impl_->patternEngine->activePatternId()
                                        : kInvalidPatternId;
    bootstrapOut.pattern.pendingId = kInvalidPatternId;
    bootstrapOut.pattern.armed = false;
    bootstrapOut.pattern.bankSize = static_cast<uint16_t>(impl_->patternOrder.size());

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
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::PlaybackMode,
                                                     toParamValue(TrackPlaybackModeValue::Looper));
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LaunchPolicy,
                                                     toParamValue(TrackLaunchPolicyValue::IgnoreIfPlaying));
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::StopPolicy,
                                                     toParamValue(TrackStopPolicyValue::ManualStop));
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LoopEnabled, kRtValueOn);
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::MuteEnabled, kRtValueOff);
        impl_->immediateDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::ArmEnabled, kRtValueOff);
    }

    // Preview-голос: отдельный one-shot режим (не следует global transport).
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::Gain01, 0.25f);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::LoopEnabled, kRtValueOff);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::FollowTransportEnabled, kRtValueOff);
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::PlaybackMode,
                                                 toParamValue(TrackPlaybackModeValue::Note));
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::LaunchPolicy,
                                                 toParamValue(TrackLaunchPolicyValue::RetriggerOnNoteOn));
    impl_->immediateDispatcher.sendTrackParamSet(impl_->previewTrackId, TrackParamId::StopPolicy,
                                                 toParamValue(TrackStopPolicyValue::ManualStop));
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
        track.playbackMode = UiTrackPlaybackMode::Looper;
        track.loop = true;
        track.fxCount = 0;
        track.fxChainIds.clear();
        track.fxEnabled.clear();
        track.clipName.clear();
        track.clipPath.clear();
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
    impl_->transport.setParam(toParamIndex(TransportParamId::Playing), playing ? kRtValueOn : kRtValueOff);

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
    impl_->transport.setParam(toParamIndex(TransportParamId::TempoNorm), tempoToNorm(bpm));
    impl_->tempoBpm = std::clamp(bpm, 20.0f, 300.0f);
}

void SamplerEngineLayer::setQuantize(QuantizeMode q) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setQuantizeMode(q);
    impl_->transport.setParam(toParamIndex(TransportParamId::QuantizeNorm), quantToNorm(q));
    impl_->quant = q;
}

void SamplerEngineLayer::setTimeSignature(uint8_t num, uint8_t den) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setTimeSignature(num, den);
    impl_->transport.setParam(toParamIndex(TransportParamId::TimeSigNumNorm), tsNumToNorm(num));
    impl_->transport.setParam(toParamIndex(TransportParamId::TimeSigDenNorm), tsDenToNorm(den));
    impl_->tsNum = static_cast<uint8_t>(std::clamp<int>(num, 1, 32));
    switch (den) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
            impl_->tsDen = den;
            break;
        default:
            impl_->tsDen = 4;
            break;
    }
}

void SamplerEngineLayer::setSwing(float swing01) noexcept {
    if (!impl_) {
        return;
    }
    const float v = std::clamp(swing01, 0.0f, 1.0f);
    (void)impl_->controlDispatcher.sendParamSet(
        kRtTrackGlobal,
        kRtSlotTrackParams,
        toParamIndex(TransportParamId::Swing01),
        v);
    impl_->transport.setParam(toParamIndex(TransportParamId::Swing01), v);
    impl_->swing01 = v;
}

void SamplerEngineLayer::setMetronomeEnabled(bool enabled) noexcept {
    if (!impl_) {
        return;
    }
    impl_->metronomeEnabled = enabled;
    if (impl_->metronomeRtExt) {
        impl_->metronomeRtExt->setEnabled(enabled);
    }
}

bool SamplerEngineLayer::setTrackMuted(uint8_t track, bool muted) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::MuteEnabled,
        muted ? kRtValueOn : kRtValueOff);
    if (ok && t < impl_->trackMuted.size()) {
        impl_->trackMuted[t] = muted;
    }
    return ok;
}

bool SamplerEngineLayer::setTrackArmed(uint8_t track, bool armed) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::ArmEnabled,
        armed ? kRtValueOn : kRtValueOff);
    if (ok && t < impl_->trackArmed.size()) {
        impl_->trackArmed[t] = armed;
    }
    return ok;
}

bool SamplerEngineLayer::setTrackLooperMode(uint8_t track, bool looperEnabled) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }

    // Единая "кнопка LOOPER" для UX:
    // пользователь не крутит три policy отдельно, а включает понятный пресет.
    //
    // LOOPER ON:
    // - mode: Looper
    // - launch: IgnoreIfPlaying (не ретриггерим уже идущий луп)
    // - stop: ManualStop (только явный stop/mute)
    // - transport coupling: follow transport (удобно для глобального play/stop)
    // - slot loop: on
    //
    // LOOPER OFF (Sampler/Note):
    // - mode: Note
    // - launch: RetriggerOnNoteOn
    // - stop: ByNoteOff
    // - transport coupling: one-shot gate (follow transport off)
    // - slot loop: off
    const TrackPlaybackModeValue mode = looperEnabled ? TrackPlaybackModeValue::Looper : TrackPlaybackModeValue::Note;
    const TrackLaunchPolicyValue launch =
        looperEnabled ? TrackLaunchPolicyValue::IgnoreIfPlaying : TrackLaunchPolicyValue::RetriggerOnNoteOn;
    const TrackStopPolicyValue stop =
        looperEnabled ? TrackStopPolicyValue::ManualStop : TrackStopPolicyValue::ByNoteOff;

    bool ok = true;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::PlaybackMode, toParamValue(mode)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LaunchPolicy, toParamValue(launch)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::StopPolicy, toParamValue(stop)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::FollowTransportEnabled,
                                                    looperEnabled ? kRtValueOn : kRtValueOff) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LoopEnabled,
                                                    looperEnabled ? kRtValueOn : kRtValueOff) && ok;
    return ok;
}

bool SamplerEngineLayer::setTrackSpeed(uint8_t track, float speed) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::PlaybackInc,
        speed);
    if (ok && t < impl_->trackPlaybackInc.size()) {
        impl_->trackPlaybackInc[t] = speed;
    }
    return ok;
}

bool SamplerEngineLayer::setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendParamSet(
        static_cast<int16_t>(t),
        kRtSlotTrackParams,
        paramIndex,
        value);
    if (!ok) {
        return false;
    }
    if (t < impl_->trackGain.size() && paramIndex == toParamIndex(TrackParamId::Gain01)) {
        impl_->trackGain[t] = std::clamp(value, 0.0f, 1.0f);
    } else if (t < impl_->trackPlaybackInc.size() && paramIndex == toParamIndex(TrackParamId::PlaybackInc)) {
        impl_->trackPlaybackInc[t] = std::clamp(value, 0.25f, 4.0f);
    } else if (t < impl_->trackMuted.size() && paramIndex == toParamIndex(TrackParamId::MuteEnabled)) {
        impl_->trackMuted[t] = (value >= 0.5f);
    } else if (t < impl_->trackArmed.size() && paramIndex == toParamIndex(TrackParamId::ArmEnabled)) {
        impl_->trackArmed[t] = (value >= 0.5f);
    }
    return true;
}

bool SamplerEngineLayer::setTrackBars(uint8_t track, uint32_t bars) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const uint32_t safeBars = std::max<uint32_t>(1u, bars);
    const bool ok = impl_->clipCtl[t]->setSlotLengthInBars(0u, safeBars);
    if (ok && t < impl_->trackBars.size()) {
        impl_->trackBars[t] = safeBars;
    }
    return ok;
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

bool SamplerEngineLayer::setFxEnabled(uint8_t track, uint8_t fxSlot, bool enabled) noexcept {
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
        toParamIndex(FxCommonParamId::Enabled),
        enabled ? kRtValueOn : kRtValueOff);
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

    // 1) Резолвим clipRefId для path.
    uint32_t clipRefId = 0;
    const auto it = impl_->clipPathToRef.find(path);
    if (it != impl_->clipPathToRef.end()) {
        clipRefId = it->second;
    } else {
        clipRefId = impl_->nextClipRef++;
        std::string err{};
        if (!impl_->clipPool.loadFromFile(clipRefId, path, &err)) {
            return false;
        }
        impl_->clipPathToRef[path] = clipRefId;
        impl_->clipRefToPath[clipRefId] = path;
    }

    // 2) Назначаем preloaded буфер треку без повторного декодирования.
    if (!impl_->clipPool.bindClipToTrack(*impl_->clipCtl[t], 0, clipRefId)) {
        return false;
    }
    if (t < impl_->trackClipRef.size()) {
        impl_->trackClipRef[t] = clipRefId;
    }

    // 3) Поведение по умолчанию для пользовательской загрузки: loop ON.
    (void)impl_->clipCtl[t]->setSlotLooping(0, true);
    clipNameOut = clipNameFromPath(path);
    return true;
}

bool SamplerEngineLayer::preloadClipToPool(uint32_t clipRefId,
                                           const std::string& path,
                                           std::string& errorOut) noexcept {
    if (!impl_) {
        errorOut = "engine is null";
        return false;
    }
    if (clipRefId == 0u) {
        errorOut = "clipRefId=0 is reserved";
        return false;
    }
    if (path.empty()) {
        errorOut = "path is empty";
        return false;
    }
    if (!impl_->clipPool.loadFromFile(clipRefId, path, &errorOut)) {
        return false;
    }
    impl_->clipPathToRef[path] = clipRefId;
    impl_->clipRefToPath[clipRefId] = path;
    if (clipRefId >= impl_->nextClipRef) {
        impl_->nextClipRef = clipRefId + 1u;
    }
    return true;
}

bool SamplerEngineLayer::setTrackClipRef(uint8_t track, uint32_t clipRefId) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    if (clipRefId == 0u) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->clipPool.bindClipToTrack(*impl_->clipCtl[t], 0, clipRefId);
    if (ok && t < impl_->trackClipRef.size()) {
        impl_->trackClipRef[t] = clipRefId;
    }
    return ok;
}

bool SamplerEngineLayer::clearTrackSample(uint8_t track) noexcept {
    if (!impl_ || impl_->clipCtl.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    if (!impl_->clipCtl[t] || !impl_->clipCtl[t]->healthcheck()) {
        return false;
    }
    const bool ok = impl_->clipCtl[t]->clearSlot(0);
    if (ok && t < impl_->trackClipRef.size()) {
        impl_->trackClipRef[t] = 0u;
    }
    return ok;
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

bool SamplerEngineLayer::requestPatternSwitchRelative(int delta) noexcept {
    if (!impl_ || !impl_->patternEngine || impl_->patternOrder.empty()) {
        return false;
    }
    if (delta == 0) {
        return false;
    }

    PatternId current = impl_->patternEngine->activePatternId();
    auto it = std::find(impl_->patternOrder.begin(), impl_->patternOrder.end(), current);
    std::size_t idx = (it == impl_->patternOrder.end()) ? 0U : static_cast<std::size_t>(it - impl_->patternOrder.begin());
    if (delta > 0) {
        idx = (idx + 1U) % impl_->patternOrder.size();
    } else {
        idx = (idx == 0U) ? (impl_->patternOrder.size() - 1U) : (idx - 1U);
    }

    const PatternId target = impl_->patternOrder[idx];
    if (target == kInvalidPatternId || target == current) {
        return false;
    }
    return requestPatternSwitchTo(target);
}

bool SamplerEngineLayer::requestPatternSwitchTo(PatternId target) noexcept {
    if (!impl_ || !impl_->patternEngine || impl_->patternOrder.empty()) {
        return false;
    }
    if (target == kInvalidPatternId) {
        return false;
    }
    if (std::find(impl_->patternOrder.begin(), impl_->patternOrder.end(), target) == impl_->patternOrder.end()) {
        return false;
    }
    if (target == impl_->patternEngine->activePatternId()) {
        return false;
    }

    // Перед выходом из текущего паттерна фиксируем его runtime-state в snapshot manager.
    (void)captureActivePatternSnapshot_();

    QuantizeMode q = impl_->transport.rt().quant;
    // AVANTGARDE_SWITCH_QUANTIZE_TUNE:
    // Для живого UX не даем pattern switch висеть до конца BAR.
    // Если глобально выбран BAR, для switch временно понижаем до дефолта (сейчас BEAT).
    if (q == QuantizeMode::Bar) {
        q = kDefaultPatternSwitchQuantize;
    }
    impl_->patternEngine->requestSwitch(target, q);
    impl_->pendingPatternId = target;
    impl_->patternArmed = true;
    return true;
}

bool SamplerEngineLayer::captureActivePatternSnapshot_() noexcept {
    if (!impl_ || !impl_->patternEngine) {
        return false;
    }
    const PatternId active = impl_->patternEngine->activePatternId();
    if (active == kInvalidPatternId) {
        return false;
    }

    PatternState state = makeDefaultPattern(active,
                                            impl_->trackCount,
                                            impl_->tempoBpm,
                                            impl_->tsNum,
                                            impl_->tsDen,
                                            impl_->quant);
    state.transport.swing01 = impl_->swing01;
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        PatternTrackSnapshot& tr = state.tracks[t];
        tr.muted = (t < impl_->trackMuted.size()) ? impl_->trackMuted[t] : false;
        tr.armed = (t < impl_->trackArmed.size()) ? impl_->trackArmed[t] : false;
        tr.gain01 = (t < impl_->trackGain.size()) ? impl_->trackGain[t] : 1.0f;
        tr.playbackInc = (t < impl_->trackPlaybackInc.size()) ? impl_->trackPlaybackInc[t] : 1.0f;
        tr.bars = (t < impl_->trackBars.size()) ? impl_->trackBars[t] : 4u;
        tr.clipRefId = (t < impl_->trackClipRef.size()) ? impl_->trackClipRef[t] : 0u;
    }
    return impl_->patternEngine->putPattern(state);
}

bool SamplerEngineLayer::processPendingPatternSwitches() noexcept {
    if (!impl_ || !impl_->patternRtExt || !impl_->patternEngine || !impl_->patternApplyTarget) {
        return false;
    }

    PatternId ready = kInvalidPatternId;
    if (!impl_->patternRtExt->consumeReadySwitch(ready)) {
        return false;
    }

    CompiledSwitchPlan plan{};
    if (!impl_->patternEngine->buildSwitchPlanTo(ready, plan)) {
        return false;
    }

    const PatternSwitchApplyReport report =
        PatternSwitchPlanApplier::apply(plan, *impl_->patternApplyTarget);
    if (!report.ok()) {
        return false;
    }

    if (impl_->pendingPatternId == ready) {
        impl_->pendingPatternId = kInvalidPatternId;
    }
    impl_->patternArmed = (impl_->pendingPatternId != kInvalidPatternId);
    return true;
}

UiPatternState SamplerEngineLayer::patternUiState() const noexcept {
    UiPatternState out{};
    if (!impl_ || !impl_->patternEngine) {
        return out;
    }
    out.activeId = impl_->patternEngine->activePatternId();
    out.pendingId = impl_->pendingPatternId;
    out.armed = impl_->patternArmed;
    out.bankSize = static_cast<uint16_t>(impl_->patternOrder.size());
    return out;
}

bool SamplerEngineLayer::syncUiCache(UiTransportState& transportInOut,
                                     std::vector<UiTrackStateView>& tracksInOut) const noexcept {
    if (!impl_) {
        return false;
    }

    const TransportRtSnapshot rt = impl_->transport.rt();
    // ВАЖНО:
    // Для UI используем control-side write-копию Playing,
    // чтобы после play/stop не было "отката" до старого rt snapshot
    // между блоками аудио (это и вызывало необходимость двойного нажатия).
    transportInOut.playing =
        (impl_->transport.getParam(toParamIndex(TransportParamId::Playing)) >= 0.5f);
    transportInOut.bpm = impl_->tempoBpm;
    transportInOut.tsNum = impl_->tsNum;
    transportInOut.tsDen = impl_->tsDen;
    transportInOut.quant = impl_->quant;
    transportInOut.metronomeEnabled = impl_->metronomeEnabled;
    transportInOut.sampleTime = rt.sampleTime;

    if (tracksInOut.size() < impl_->trackCount) {
        tracksInOut.resize(impl_->trackCount);
    }
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        UiTrackStateView& ui = tracksInOut[t];
        ui.id = t;
        if (t < impl_->trackMuted.size()) {
            ui.muted = impl_->trackMuted[t];
        }
        if (t < impl_->trackArmed.size()) {
            ui.armed = impl_->trackArmed[t];
        }
        if (t < impl_->trackGain.size()) {
            ui.gain01 = impl_->trackGain[t];
        }
        if (t < impl_->trackPlaybackInc.size()) {
            ui.stretchRatio = impl_->trackPlaybackInc[t];
        }
        if (t < impl_->trackBars.size()) {
            ui.bars = impl_->trackBars[t];
        }

        const uint32_t clipRef = (t < impl_->trackClipRef.size()) ? impl_->trackClipRef[t] : 0u;
        if (clipRef == 0u) {
            ui.clipPath.clear();
            ui.clipName.clear();
        } else {
            const auto itPath = impl_->clipRefToPath.find(clipRef);
            if (itPath != impl_->clipRefToPath.end()) {
                ui.clipPath = itPath->second;
                ui.clipName = clipNameFromPath(itPath->second);
            } else {
                ui.clipPath.clear();
                ui.clipName.clear();
            }
        }

        if (ui.clipName.empty()) {
            ui.state = UiTrackState::Empty;
        } else if (transportInOut.playing) {
            ui.state = UiTrackState::Playing;
        } else {
            ui.state = UiTrackState::Stopped;
        }
    }
    return true;
}

} // namespace avantgarde
