#include "app/SamplerEngineLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/IAudioEngine.h"
#include "contracts/IParameterized.h"
#include "contracts/IPlatform.h"
#include "contracts/ISnapshotable.h"
#include "contracts/ISamplePreviewEngine.h"
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
#include "service/pattern/PatternSnapshotOrchestrator.h"
#include "service/track/TrackFeatureResolver.h"
#include "service/audio/BpmDetectorService.h"

// Concrete runtime impls are compiled into this TU intentionally.
#include "runtime/AudioEngine.cpp"
#include "runtime/ClipTrack.cpp"
#include "runtime/ParamBridgeDualBuffer.cpp"
#include "runtime/RtCommandQueueSPSC.cpp"

namespace avantgarde {

// User-data для callback-а аудиохоста.
// Важно: тип имеет внешнюю linkage (не anonymous namespace), чтобы не ловить
// -Wsubobject-linkage в тестах, где этот TU подключается напрямую.
struct EngineRenderUser {
    IAudioEngine* engine{nullptr};
    ISamplePreviewEngine* preview{nullptr};
};

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

UiTrackPlaybackProfile uiProfileFromModeLoop(TrackPlaybackModeValue mode, bool loop) noexcept {
    if (mode == TrackPlaybackModeValue::Note) {
        return loop ? UiTrackPlaybackProfile::Pattern : UiTrackPlaybackProfile::PatternOnce;
    }
    return loop ? UiTrackPlaybackProfile::Loop : UiTrackPlaybackProfile::OneShot;
}

TrackPlaybackProfileValue profileFromLegacyLooperToggle(bool looperEnabled) noexcept {
    // Сохраняем legacy-семантику старого toggle:
    // ON  -> LOOP
    // OFF -> PATTERN_ONCE
    return looperEnabled ? TrackPlaybackProfileValue::Loop : TrackPlaybackProfileValue::PatternOnce;
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

PatternTransportSnapshot readPatternTransportSnapshot(const TransportBridgeDualBuffer& transport) noexcept {
    SnapshotRecord snapshot{};
    if (transport.getSnapshot(snapshot) && snapshot.domain == SnapshotDomain::Transport) {
        PatternTransportSnapshot out{};
        out.bpm = snapshot.transport.bpm;
        out.tsNum = snapshot.transport.tsNum;
        out.tsDen = snapshot.transport.tsDen;
        out.quant = snapshot.transport.quant;
        out.swing01 = snapshot.transport.swing01;
        return out;
    }
    PatternTransportSnapshot out{};
    return out;
}

TrackSnapshot readTrackSnapshot(const ITrack* track) noexcept {
    if (!track) {
        return {};
    }
    SnapshotRecord snapshot{};
    if (!track->getSnapshot(snapshot) || snapshot.domain != SnapshotDomain::Track) {
        return {};
    }
    return snapshot.track;
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

uint32_t inferBarsFromClipDuration(const SharedClipBuffer& clip,
                                   float bpm,
                                   uint8_t tsNum,
                                   uint8_t tsDen) noexcept {
    if (!clip.valid()) {
        return 4u;
    }
    const float safeBpm = std::clamp(bpm, 20.0f, 300.0f);
    const uint8_t safeNum = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(tsNum), 1, 32));
    uint8_t safeDen = tsDen;
    if (!(safeDen == 1 || safeDen == 2 || safeDen == 4 || safeDen == 8 || safeDen == 16 || safeDen == 32)) {
        safeDen = 4;
    }

    const double seconds = static_cast<double>(clip.frames) / static_cast<double>(clip.sampleRate);
    const double beatsPerBar = static_cast<double>(safeNum) * 4.0 / static_cast<double>(safeDen);
    const double secondsPerBar = beatsPerBar * (60.0 / static_cast<double>(safeBpm));
    if (!(secondsPerBar > 1e-6) || !std::isfinite(seconds)) {
        return 4u;
    }

    const double rawBars = seconds / secondsPerBar;
    if (!std::isfinite(rawBars)) {
        return 4u;
    }
    const uint32_t rounded = static_cast<uint32_t>(std::llround(std::max(1.0, rawBars)));
    return std::clamp<uint32_t>(rounded, 1u, 512u);
}

void renderThunk(AudioProcessContext& ctx, void* user) noexcept {
    // Колбэк аудиохоста:
    // Логика маршрутизации:
    // - если preview активен, блок целиком обслуживает preview-движок;
    // - иначе блок обслуживает основной engine-граф.
    auto* ru = static_cast<EngineRenderUser*>(user);
    if (!ru || !ru->engine) {
        return;
    }
    if (ru->preview && ru->preview->isActive()) {
        ru->preview->process(ctx);
        return;
    }
    ru->engine->processBlock(ctx);
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
    // Preview-движок: отдельный слой, не Track и не Transport.
    std::unique_ptr<ISamplePreviewEngine> preview{};
    // User-data для stream callback.
    EngineRenderUser renderUser{};
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
    // Сырые указатели на track instances (core pool в терминах ITrack).
    std::vector<ITrack*> tracks{};
    // Единый список snapshot-источников (transport + tracks).
    std::vector<ISnapshotable*> snapshotables{};
    // Resolver optional capabilities (IClipTrack, будущие I...Track extensions).
    TrackFeatureResolver trackFeatures{};
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
    // Кэш детекта исходного BPM сэмпла (source BPM, без speed/pitch трека).
    std::unordered_map<std::string, float> clipPathToSourceBpm{};
    // Генератор clipRefId для runtime-сессии.
    uint32_t nextClipRef{1};
    bool metronomeEnabled{false};
    // Pattern engine: bank + schedя uler + snapshots.
    std::unique_ptr<PatternEngine> patternEngine{};
    // Оркестратор capture default/live pattern snapshot-ов.
    std::unique_ptr<PatternSnapshotOrchestrator> patternSnapshotOrchestrator{};
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

    ITrack* trackAt(uint8_t trackId) const noexcept {
        return trackFeatures.track(trackId);
    }
    IClipTrack* clipAt(uint8_t trackId) const noexcept {
        return trackFeatures.clipTrack(trackId);
    }
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
    impl_->preview = MakeSamplePreviewEngine();
    impl_->metronomeEnabled = false;

    // Создаем пользовательские треки.
    std::vector<std::unique_ptr<ITrack>> userTracks(impl_->trackCount);
    std::vector<ITrack*> userTrackPtrs(impl_->trackCount, nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        userTracks[t] = std::make_unique<ClipTrackImpl>(config.sampleRate, t);
        userTrackPtrs[t] = userTracks[t].get();
    }

    // Публикуем треки для resolver'а параметров.
    impl_->tracks.assign(impl_->trackCount, nullptr);
    impl_->snapshotables.clear();
    impl_->snapshotables.reserve(static_cast<std::size_t>(impl_->trackCount) + 1u);
    impl_->snapshotables.push_back(&impl_->transport);
    gParamTracks.assign(static_cast<std::size_t>(impl_->trackCount), nullptr);
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->tracks[t] = userTrackPtrs[t];
        impl_->snapshotables.push_back(userTrackPtrs[t]);
        gParamTracks[t] = userTrackPtrs[t];
    }
    impl_->trackFeatures.bind(&impl_->tracks);
    gParamGlobalTransport = &impl_->transport;
    impl_->pb.setResolver(&resolveParamTarget);

    // Регистрируем пользовательские треки в engine.
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        impl_->engine.registerTrack(std::move(userTracks[t]));
    }

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
    impl_->patternSnapshotOrchestrator = std::make_unique<PatternSnapshotOrchestrator>(*impl_->patternEngine);
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
    const PatternTransportSnapshot bootstrapPatternTransport{
        .bpm = 120.0f,
        .tsNum = 4,
        .tsDen = 4,
        .quant = kDefaultTransportQuantize,
        .swing01 = 0.0f};
    for (PatternId id = 1; id <= 4; ++id) {
        if (impl_->patternSnapshotOrchestrator &&
            impl_->patternSnapshotOrchestrator->putDefaultPattern(id, bootstrapPatternTransport, impl_->trackCount)) {
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
    bootstrapOut.transport.recordEnabled = false;
    bootstrapOut.transport.bpm = 120.0f;
    bootstrapOut.transport.tsNum = 4;
    bootstrapOut.transport.tsDen = 4;
    bootstrapOut.transport.quant = kDefaultTransportQuantize;
    bootstrapOut.transport.metronomeEnabled = false;
    bootstrapOut.transport.activeTrack = 0;
    bootstrapOut.transport.previewPlaying = false;
    bootstrapOut.transport.previewPlayhead01 = 0.0f;
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
        track.playbackProfile = UiTrackPlaybackProfile::Loop;
        track.loop = true;
        track.trimStart01 = 0.0f;
        track.trimEnd01 = 1.0f;
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
    impl_->renderUser.engine = &impl_->engine;
    impl_->renderUser.preview = impl_->preview.get();
    if (!impl_->stream->start(&renderThunk, &impl_->renderUser)) {
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
}

void SamplerEngineLayer::setQuantize(QuantizeMode q) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setQuantizeMode(q);
    impl_->transport.setParam(toParamIndex(TransportParamId::QuantizeNorm), quantToNorm(q));
}

void SamplerEngineLayer::setTimeSignature(uint8_t num, uint8_t den) noexcept {
    if (!impl_) {
        return;
    }
    (void)impl_->controlDispatcher.setTimeSignature(num, den);
    impl_->transport.setParam(toParamIndex(TransportParamId::TimeSigNumNorm), tsNumToNorm(num));
    impl_->transport.setParam(toParamIndex(TransportParamId::TimeSigDenNorm), tsDenToNorm(den));
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
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::MuteEnabled,
        muted ? kRtValueOn : kRtValueOff);
    if (ok) {
        // Control fast-path для мгновенного UI snapshot до ближайшего RT-блока.
        if (IClipTrack* clip = impl_->clipAt(t)) {
            clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::MuteEnabled), muted ? kRtValueOn : kRtValueOff);
        }
    }
    return ok;
}

bool SamplerEngineLayer::setTrackArmed(uint8_t track, bool armed) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::ArmEnabled,
        armed ? kRtValueOn : kRtValueOff);
    if (ok) {
        // Control fast-path для мгновенного UI snapshot до ближайшего RT-блока.
        if (IClipTrack* clip = impl_->clipAt(t)) {
            clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::ArmEnabled), armed ? kRtValueOn : kRtValueOff);
        }
    }
    return ok;
}

bool SamplerEngineLayer::setTrackLooperMode(uint8_t track, bool looperEnabled) noexcept {
    return setTrackPlaybackProfile(track, profileFromLegacyLooperToggle(looperEnabled));
}

bool SamplerEngineLayer::setTrackPlaybackProfile(uint8_t track,
                                                 TrackPlaybackProfileValue profile) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }

    TrackPlaybackModeValue mode = TrackPlaybackModeValue::Looper;
    TrackLaunchPolicyValue launch = TrackLaunchPolicyValue::IgnoreIfPlaying;
    TrackStopPolicyValue stop = TrackStopPolicyValue::ManualStop;
    bool followTransport = true;
    bool loopEnabled = true;

    switch (profile) {
        case TrackPlaybackProfileValue::Pattern:
            mode = TrackPlaybackModeValue::Note;
            launch = TrackLaunchPolicyValue::RetriggerOnNoteOn;
            stop = TrackStopPolicyValue::ByNoteOff;
            followTransport = false;
            loopEnabled = true;
            break;
        case TrackPlaybackProfileValue::PatternOnce:
            mode = TrackPlaybackModeValue::Note;
            launch = TrackLaunchPolicyValue::RetriggerOnNoteOn;
            stop = TrackStopPolicyValue::ByNoteOff;
            followTransport = false;
            loopEnabled = false;
            break;
        case TrackPlaybackProfileValue::Loop:
            mode = TrackPlaybackModeValue::Looper;
            launch = TrackLaunchPolicyValue::IgnoreIfPlaying;
            stop = TrackStopPolicyValue::ManualStop;
            followTransport = true;
            loopEnabled = true;
            break;
        case TrackPlaybackProfileValue::OneShot:
            mode = TrackPlaybackModeValue::Looper;
            launch = TrackLaunchPolicyValue::RetriggerOnNoteOn;
            stop = TrackStopPolicyValue::ManualStop;
            followTransport = false;
            loopEnabled = false;
            break;
        default:
            break;
    }

    bool ok = true;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::PlaybackMode, toParamValue(mode)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LaunchPolicy, toParamValue(launch)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::StopPolicy, toParamValue(stop)) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::FollowTransportEnabled,
                                                    followTransport ? kRtValueOn : kRtValueOff) && ok;
    ok = impl_->controlDispatcher.sendTrackParamSet(static_cast<int16_t>(t), TrackParamId::LoopEnabled,
                                                    loopEnabled ? kRtValueOn : kRtValueOff) && ok;
    if (!ok) {
        return false;
    }
    // Control fast-path: сразу обновляем параметризацию трека для snapshot/UI.
    if (IClipTrack* clip = impl_->clipAt(t)) {
        clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::PlaybackMode), toParamValue(mode));
        clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::LoopEnabled),
                                     loopEnabled ? kRtValueOn : kRtValueOff);
    }
    return true;
}

bool SamplerEngineLayer::setTrackSpeed(uint8_t track, float speed) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::PlaybackInc,
        speed);
    if (ok) {
        // Control fast-path: сразу обновляем playbackInc/sync в snapshot трека.
        if (IClipTrack* clip = impl_->clipAt(t)) {
            clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::PlaybackInc), speed);
        }
    }
    return ok;
}

bool SamplerEngineLayer::setTrackTempoSync(uint8_t track, bool enabled) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    const bool ok = impl_->controlDispatcher.sendTrackParamSet(
        static_cast<int16_t>(t),
        TrackParamId::TempoSyncEnabled,
        enabled ? kRtValueOn : kRtValueOff);
    if (ok) {
        // Control fast-path для мгновенного UI snapshot до ближайшего RT-блока.
        if (IClipTrack* clip = impl_->clipAt(t)) {
            clip->mirrorParamForSnapshot(toParamIndex(TrackParamId::TempoSyncEnabled), enabled ? kRtValueOn : kRtValueOff);
        }
    }
    return ok;
}

bool SamplerEngineLayer::triggerTrackNoteOn(uint8_t track, uint8_t note, float velocity01) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    return impl_->controlDispatcher.sendNoteOn(static_cast<int16_t>(t), note, velocity01);
}

bool SamplerEngineLayer::triggerTrackNoteOff(uint8_t track, uint8_t note) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    return impl_->controlDispatcher.sendNoteOff(static_cast<int16_t>(t), note);
}

bool SamplerEngineLayer::setTrackParam(uint8_t track, uint16_t paramIndex, float value) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
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
    // Control fast-path для мгновенного UI snapshot до ближайшего RT-блока.
    if (IClipTrack* clip = impl_->clipAt(t)) {
        clip->mirrorParamForSnapshot(paramIndex, value);
    }
    return true;
}

bool SamplerEngineLayer::setTrackBars(uint8_t track, uint32_t bars) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    IClipTrack* clip = impl_->clipAt(t);
    if (!clip || !clip->healthcheck()) {
        return false;
    }
    const uint32_t safeBars = std::max<uint32_t>(1u, bars);
    const bool ok = clip->setSlotLengthInBars(0u, safeBars);
    if (ok) {
        // setSlotLengthInBars в ClipTrack включает stretch mode.
        // Если у трека tempo-sync был выключен, немедленно возвращаем OFF.
        const bool tempoSync = clip->getParam(toParamIndex(TrackParamId::TempoSyncEnabled)) >= 0.5f;
        if (!tempoSync) {
            (void)impl_->controlDispatcher.sendTrackParamSet(
                static_cast<int16_t>(t),
                TrackParamId::TempoSyncEnabled,
                kRtValueOff);
        }
    }
    return ok;
}

bool SamplerEngineLayer::addFxToTrack(uint8_t track, std::unique_ptr<IAudioModule> module) noexcept {
    if (!impl_ || !module || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    // addModule вызывается строго вне RT.
    tr->addModule(std::move(module));
    return true;
}

bool SamplerEngineLayer::removeFxFromTrack(uint8_t track, uint8_t fxSlot) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    IClipTrack* clip = impl_->clipAt(t);
    if (!clip || !clip->healthcheck()) {
        return false;
    }
    // Защита от невалидного слота до удаления.
    if (!clip->getModule(static_cast<std::size_t>(fxSlot))) {
        return false;
    }
    // removeModuleAt вызывается только вне RT.
    return clip->removeModuleAt(static_cast<std::size_t>(fxSlot));
}

bool SamplerEngineLayer::setFxParam(uint8_t track,
                                    uint8_t fxSlot,
                                    uint16_t paramIndex,
                                    float normalizedValue) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    if (!tr->getModule(static_cast<std::size_t>(fxSlot))) {
        return false;
    }
    return impl_->controlDispatcher.sendParamSet(
        static_cast<int16_t>(t),
        static_cast<int16_t>(fxSlot),
        paramIndex,
        normalizedValue);
}

bool SamplerEngineLayer::setFxEnabled(uint8_t track, uint8_t fxSlot, bool enabled) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    ITrack* tr = impl_->trackAt(t);
    if (!tr || !tr->healthcheck()) {
        return false;
    }
    if (!tr->getModule(static_cast<std::size_t>(fxSlot))) {
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
    if (impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    IClipTrack* clip = impl_->clipAt(t);
    if (!clip || !clip->healthcheck()) {
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
    if (!impl_->clipPool.bindClipToTrack(*clip, 0, clipRefId)) {
        return false;
    }
    clip->setClipRefId(clipRefId);

    // 3) Поведение по умолчанию для пользовательской загрузки: loop ON.
    (void)clip->setSlotLooping(0, true);
    // 4) Автоподбор bars по длине загруженного клипа и текущему transport.
    // Это убирает фиксированное "4 bars" при пользовательской загрузке.
    //
    // ВАЖНО:
    // Чтобы tempo-sync был музыкально консистентным на клипах любой длины,
    // bars вычисляем по source BPM сэмпла (если удалось детектнуть), а не по BPM проекта.
    // Иначе на длинных клипах (например 32+ bars) rounding "под BPM проекта"
    // дает playbackInc близкий к 1.0, и sync визуально "не работает".
    SharedClipBuffer loaded{};
    if (impl_->clipPool.get(clipRefId, loaded)) {
        const PatternTransportSnapshot transportSnap = readPatternTransportSnapshot(impl_->transport);
        float barsTempoBpm = transportSnap.bpm;
        if (!path.empty()) {
            const auto itBpm = impl_->clipPathToSourceBpm.find(path);
            if (itBpm != impl_->clipPathToSourceBpm.end()) {
                barsTempoBpm = std::clamp(itBpm->second, 20.0f, 300.0f);
            } else {
                BpmDetectorService detector{};
                const BpmDetectionResult det = detector.detectFromFile(path, 1.0f);
                if (det.ok && std::isfinite(det.sourceBpm) && det.sourceBpm > 0.0f) {
                    barsTempoBpm = std::clamp(det.sourceBpm, 20.0f, 300.0f);
                    impl_->clipPathToSourceBpm[path] = barsTempoBpm;
                }
            }
        }
        const uint32_t inferredBars =
            inferBarsFromClipDuration(loaded, barsTempoBpm, transportSnap.tsNum, transportSnap.tsDen);
        (void)clip->setSlotLengthInBars(0u, inferredBars);
        const bool tempoSync = clip->getParam(toParamIndex(TrackParamId::TempoSyncEnabled)) >= 0.5f;
        if (!tempoSync) {
            // Аналогично setTrackBars: загрузка сэмпла не должна неявно
            // включать sync у трека, если пользователь его выключил.
            (void)impl_->controlDispatcher.sendTrackParamSet(
                static_cast<int16_t>(t),
                TrackParamId::TempoSyncEnabled,
                kRtValueOff);
        }
    }
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
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    if (clipRefId == 0u) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    IClipTrack* clip = impl_->clipAt(t);
    if (!clip || !clip->healthcheck()) {
        return false;
    }
    const bool ok = impl_->clipPool.bindClipToTrack(*clip, 0, clipRefId);
    if (ok) {
        clip->setClipRefId(clipRefId);
    }
    return ok;
}

bool SamplerEngineLayer::clearTrackSample(uint8_t track) noexcept {
    if (!impl_ || impl_->tracks.empty()) {
        return false;
    }
    const uint8_t t = clampTrack(track, impl_->trackCount);
    IClipTrack* clip = impl_->clipAt(t);
    if (!clip || !clip->healthcheck()) {
        return false;
    }
    const bool ok = clip->clearSlot(0);
    if (ok) {
        clip->setClipRefId(0u);
    }
    return ok;
}

void SamplerEngineLayer::previewRequest(const std::string& path,
                                        float speed,
                                        float start01,
                                        float end01,
                                        float gain01) noexcept {
    if (!impl_) {
        return;
    }
    if (!impl_->preview) {
        return;
    }
    if (path.empty()) {
        impl_->preview->stop();
        return;
    }

    uint32_t clipRefId = 0u;
    const auto it = impl_->clipPathToRef.find(path);
    if (it != impl_->clipPathToRef.end()) {
        clipRefId = it->second;
    } else {
        clipRefId = impl_->nextClipRef++;
        std::string err{};
        if (!impl_->clipPool.loadFromFile(clipRefId, path, &err)) {
            return;
        }
        impl_->clipPathToRef[path] = clipRefId;
        impl_->clipRefToPath[clipRefId] = path;
    }

    SharedClipBuffer sample{};
    if (!impl_->clipPool.get(clipRefId, sample) || !sample.valid()) {
        impl_->preview->stop();
        return;
    }

    const float safeStart01 = std::clamp(start01, 0.0f, 1.0f);
    const float safeEnd01 = std::clamp(std::max(end01, safeStart01 + 0.01f), 0.0f, 1.0f);
    const int32_t frames = static_cast<int32_t>(sample.frames);
    SampleRegion region{};
    region.startFrame = std::clamp<int32_t>(
        static_cast<int32_t>(std::floor(static_cast<double>(safeStart01) * frames)),
        0,
        std::max(0, frames - 1));
    region.endFrame = std::clamp<int32_t>(
        static_cast<int32_t>(std::ceil(static_cast<double>(safeEnd01) * frames)),
        region.startFrame + 1,
        std::max(1, frames));

    PreviewOptions options{};
    options.gain = std::clamp(gain01, 0.0f, 1.0f);
    options.speed = 1.0f;
    impl_->preview->play(sample,
                         region,
                         std::clamp(speed, 0.25f, 4.0f),
                         SamplePreviewLoopMode::Off,
                         options);
}

void SamplerEngineLayer::previewStop() noexcept {
    if (!impl_) {
        return;
    }
    if (!impl_->preview) {
        return;
    }
    impl_->preview->stop();
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
    if (!impl_ || !impl_->patternEngine || !impl_->patternSnapshotOrchestrator) {
        return false;
    }
    return impl_->patternSnapshotOrchestrator->captureActivePattern(
        std::span<ISnapshotable* const>(impl_->snapshotables.data(), impl_->snapshotables.size()));
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
    const PatternTransportSnapshot transportSnap = readPatternTransportSnapshot(impl_->transport);
    transportInOut.bpm = transportSnap.bpm;
    transportInOut.tsNum = transportSnap.tsNum;
    transportInOut.tsDen = transportSnap.tsDen;
    transportInOut.quant = transportSnap.quant;
    transportInOut.metronomeEnabled = impl_->metronomeEnabled;
    transportInOut.sampleTime = rt.sampleTime;
    transportInOut.previewPlaying = false;
    transportInOut.previewPlayhead01 = 0.0f;
    if (impl_->preview) {
        const SamplePreviewState pv = impl_->preview->state();
        transportInOut.previewPlaying = pv.playing;
        transportInOut.previewPlayhead01 = std::clamp(pv.playhead01, 0.0f, 1.0f);
    }

    if (tracksInOut.size() < impl_->trackCount) {
        tracksInOut.resize(impl_->trackCount);
    }
    for (uint8_t t = 0; t < impl_->trackCount; ++t) {
        UiTrackStateView& ui = tracksInOut[t];
        ui.id = t;
        ITrack* track = impl_->trackAt(t);
        const TrackSnapshot sh = readTrackSnapshot(track);
        ui.muted = sh.muted;
        ui.armed = sh.armed;
        ui.gain01 = sh.gain01;
        ui.stretchRatio = sh.playbackInc;
        ui.bars = sh.bars;
        ui.playbackMode = (sh.playbackMode == TrackPlaybackModeValue::Note)
                              ? UiTrackPlaybackMode::Note
                              : UiTrackPlaybackMode::Looper;
        ui.loop = sh.loopEnabled;
        ui.tempoSync = sh.tempoSync;
        ui.playbackProfile = uiProfileFromModeLoop(
            (ui.playbackMode == UiTrackPlaybackMode::Note)
                ? TrackPlaybackModeValue::Note
                : TrackPlaybackModeValue::Looper,
            ui.loop);
        ui.trimStart01 = std::clamp(sh.trimStart01, 0.0f, 0.99f);
        ui.trimEnd01 = std::clamp(sh.trimEnd01, 0.01f, 1.0f);
        if (ui.trimEnd01 <= ui.trimStart01 + 0.01f) {
            ui.trimEnd01 = std::min(1.0f, ui.trimStart01 + 0.01f);
        }
        if (track) {
            // Playhead читаем из RT-слоя трека (это чистая runtime-телеметрия),
            // остальные поля (mute/speed/mode/trim) приходят из track snapshot.
            ui.playhead01 = std::clamp(
                track->getParam(toParamIndex(TrackParamId::PlayheadNorm)),
                0.0f,
                1.0f);
        } else {
            ui.playhead01 = 0.0f;
        }

        const uint32_t clipRef = sh.clipRefId;
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
