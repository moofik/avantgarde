#include "service/ui/widgets/SampleEditWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "service/ui/UiBindResolver.h"
#include "service/ui/UiCapabilityService.h"
#include "service/ui/UiTargetResolver.h"
#include "service/ui/layout/UiNodeComponentComposer.h"

namespace avantgarde {
namespace {

uint16_t readU16Le(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32Le(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0] |
                                 (p[1] << 8) |
                                 (p[2] << 16) |
                                 (p[3] << 24));
}

bool readExact(std::ifstream& in, void* dst, std::size_t n) {
    in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    return in.good();
}

bool intentFromTarget(std::string_view targetCanonical,
                      uint8_t selectedTrack,
                      float value,
                      UiIntent& out) {
    if (targetCanonical == "param.track.selected.playback_profile") {
        out.type = UiIntentType::SetTrackPlaybackProfile;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.speed") {
        out.type = UiIntentType::SetTrackSpeed;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.gain") {
        out.type = UiIntentType::SetTrackGain;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.start") {
        out.type = UiIntentType::SetTrackTrimStart;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.end") {
        out.type = UiIntentType::SetTrackTrimEnd;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    if (targetCanonical == "param.track.selected.tempo_sync") {
        out.type = UiIntentType::SetTrackTempoSync;
        out.track = selectedTrack;
        out.value = value;
        return true;
    }
    return false;
}

} // namespace

SampleEditWidget::SampleEditWidget() noexcept = default;

SampleEditWidget::SampleEditWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth),
      speedStep_(options.speedStep > 0.0f ? options.speedStep : 0.05f),
      gainStep_(options.gainStep > 0.0f ? options.gainStep : 0.05f),
      trimStep_(options.trimStep > 0.0f ? options.trimStep : 0.01f) {
    if (options.layoutTemplate.has_value()) {
        layoutTemplate_ = options.layoutTemplate;
        buildLayoutModel_(*options.layoutTemplate);
    }
}

const char* SampleEditWidget::id() const noexcept {
    return "sample_edit";
}

uint8_t SampleEditWidget::clampTrack_(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0U) {
        return 0U;
    }
    return (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
}

uint8_t SampleEditWidget::profileToIndex_(UiTrackPlaybackProfile profile) noexcept {
    return static_cast<uint8_t>(profile);
}

UiTrackPlaybackProfile SampleEditWidget::indexToProfile_(uint8_t index) noexcept {
    switch (index) {
        case 0: return UiTrackPlaybackProfile::Pattern;
        case 1: return UiTrackPlaybackProfile::PatternOnce;
        case 2: return UiTrackPlaybackProfile::Loop;
        case 3:
        default:
            return UiTrackPlaybackProfile::OneShot;
    }
}

const char* SampleEditWidget::profileName_(UiTrackPlaybackProfile profile) noexcept {
    switch (profile) {
        case UiTrackPlaybackProfile::Pattern: return "PATTERN";
        case UiTrackPlaybackProfile::PatternOnce: return "PATTERN ONCE";
        case UiTrackPlaybackProfile::Loop: return "LOOP";
        case UiTrackPlaybackProfile::OneShot:
        default:
            return "ONESHOT";
    }
}

float SampleEditWidget::speedTo01_(float speed) noexcept {
    constexpr float kMin = 0.25f;
    constexpr float kMax = 4.0f;
    const float clamped = std::clamp(speed, kMin, kMax);
    return (clamped - kMin) / (kMax - kMin);
}

float SampleEditWidget::trimTo01_(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

std::vector<float> SampleEditWidget::buildWavePeaks_(const std::vector<float>& mono,
                                                     std::size_t pointCount) {
    // Количество "колонок" итоговой waveform.
    // Держим нижнюю границу 64, чтобы маленькие файлы/окна не давали слишком грубую картинку.
    const std::size_t points = std::max<std::size_t>(64U, pointCount);
    std::vector<float> peaks(points, 0.0f);
    if (mono.empty()) {
        // Пустой сигнал = пустая огибающая.
        return peaks;
    }

    // сколько всего сэмплов в декодированном моно-массиве (mono.size()),
    // то есть длина входного сигнала для анализа waveform.
    const std::size_t total = mono.size();
    // Глобальный максимум амплитуды во всех бакетах.
    // Нужен для нормализации финального массива в диапазон [0..1].
    float globalPeak = 0.0f;
    for (std::size_t i = 0; i < points; ++i) {
        // Делим весь сигнал на points равных участков (бакетов).
        // from/to — полуинтервал [from, to) конкретного бакета.
        // Идея: делим весь массив длиной total на points примерно равных частей.
        // Для бакета i обрабатывается диапазон [from, to).
        const std::size_t from = (i * total) / points;
        const std::size_t to = std::min<std::size_t>(total, ((i + 1U) * total) / points);
        float peak = 0.0f;
        // Из-за целочисленного деления часто бывает from == to
        // (особенно если points > total, например короткий сэмпл).
        // Явно показываем: считаем peak только если диапазон непустой.
        // Когда бывает from == to:
        // когда points > total, то есть столбиков хотим больше, чем есть исходных сэмплов.
        // пример: total = 50, points = 256
        // тогда много бакетов получаются “пустыми” после integer division, и у них from == to.
        // Практически такое бывает на очень коротких/битых/обрезанных файлах
        // или если в будущем сильно увеличить points при маленьких буферах.
        if (to > from) {
            // Берем peak envelope: максимум |sample| внутри бакета.
            // Используем модуль, потому что для визуализации важна энергия, а не знак полуволны.
            for (std::size_t j = from; j < to; ++j) {
                peak = std::max(peak, std::fabs(mono[j]));
            }
        }
        peaks[i] = peak;
        globalPeak = std::max(globalPeak, peak);
    }

    if (globalPeak > std::numeric_limits<float>::epsilon()) {
        // Нормализация по глобальному пику:
        // самый громкий бакет станет 1.0, остальные пропорционально масштабируются.
        // Это дает стабильную визуализацию для тихих/громких клипов в одном и том же UI.
        const float norm = 1.0f / globalPeak;
        for (float& v : peaks) {
            v = std::clamp(v * norm, 0.0f, 1.0f);
        }
    }
    return peaks;
}

bool SampleEditWidget::decodeWavMono_(const std::string& path,
                                      std::vector<float>& monoOut) {
    monoOut.clear();
    if (path.empty()) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    uint8_t riff[12]{};
    if (!readExact(in, riff, sizeof(riff))) {
        return false;
    }
    if (std::memcmp(riff + 0, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    struct Fmt {
        uint16_t audioFormat{0};
        uint16_t channels{0};
        uint32_t sampleRate{0};
        uint16_t bitsPerSample{0};
        uint16_t blockAlign{0};
    } fmt{};

    bool haveFmt = false;
    bool haveData = false;
    std::vector<uint8_t> data{};

    while (in.good() && !(haveFmt && haveData)) {
        uint8_t chunkHeader[8]{};
        if (!readExact(in, chunkHeader, sizeof(chunkHeader))) {
            break;
        }
        const uint32_t chunkSize = readU32Le(chunkHeader + 4);
        const bool hasPad = (chunkSize & 1U) != 0U;

        if (std::memcmp(chunkHeader, "fmt ", 4) == 0) {
            if (chunkSize < 16U) {
                return false;
            }
            std::vector<uint8_t> fmtBuf(chunkSize);
            if (!readExact(in, fmtBuf.data(), chunkSize)) {
                return false;
            }
            fmt.audioFormat = readU16Le(fmtBuf.data() + 0);
            fmt.channels = readU16Le(fmtBuf.data() + 2);
            fmt.sampleRate = readU32Le(fmtBuf.data() + 4);
            fmt.blockAlign = readU16Le(fmtBuf.data() + 12);
            fmt.bitsPerSample = readU16Le(fmtBuf.data() + 14);
            haveFmt = true;
        } else if (std::memcmp(chunkHeader, "data", 4) == 0) {
            data.resize(chunkSize);
            if (chunkSize > 0U && !readExact(in, data.data(), chunkSize)) {
                return false;
            }
            haveData = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
            if (!in.good()) {
                return false;
            }
        }

        if (hasPad) {
            in.seekg(1, std::ios::cur);
            if (!in.good()) {
                return false;
            }
        }
    }

    if (!haveFmt || !haveData) {
        return false;
    }
    if (!(fmt.audioFormat == 1U || fmt.audioFormat == 3U)) {
        return false;
    }
    if (!(fmt.channels == 1U || fmt.channels == 2U)) {
        return false;
    }
    if (!(fmt.bitsPerSample == 16U || fmt.bitsPerSample == 24U || fmt.bitsPerSample == 32U)) {
        return false;
    }
    if (fmt.blockAlign == 0U || fmt.sampleRate == 0U) {
        return false;
    }

    const uint32_t frames = static_cast<uint32_t>(data.size() / fmt.blockAlign);
    if (frames == 0U) {
        return false;
    }

    monoOut.assign(frames, 0.0f);
    const uint8_t* src = data.data();
    auto clamp1 = [](float x) noexcept {
        return std::clamp(x, -1.0f, 1.0f);
    };

    if (fmt.audioFormat == 3U && fmt.bitsPerSample == 32U) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float* frame = reinterpret_cast<const float*>(src + i * fmt.blockAlign);
            const float l = clamp1(frame[0]);
            const float r = (fmt.channels == 2U) ? clamp1(frame[1]) : l;
            monoOut[i] = 0.5f * (l + r);
        }
        return true;
    }

    if (fmt.audioFormat == 1U && fmt.bitsPerSample == 16U) {
        for (uint32_t i = 0; i < frames; ++i) {
            const uint8_t* frame = src + i * fmt.blockAlign;
            const int16_t s0 = static_cast<int16_t>(readU16Le(frame + 0));
            const float l = static_cast<float>(s0) / 32768.0f;
            float r = l;
            if (fmt.channels == 2U) {
                const int16_t s1 = static_cast<int16_t>(readU16Le(frame + 2));
                r = static_cast<float>(s1) / 32768.0f;
            }
            monoOut[i] = 0.5f * (l + r);
        }
        return true;
    }

    if (fmt.audioFormat == 1U && fmt.bitsPerSample == 24U) {
        auto readS24 = [](const uint8_t* p) noexcept {
            int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
            if ((v & 0x00800000) != 0) {
                v |= 0xFF000000;
            }
            return v;
        };
        for (uint32_t i = 0; i < frames; ++i) {
            const uint8_t* frame = src + i * fmt.blockAlign;
            const float l = static_cast<float>(readS24(frame + 0)) / 8388608.0f;
            float r = l;
            if (fmt.channels == 2U) {
                r = static_cast<float>(readS24(frame + 3)) / 8388608.0f;
            }
            monoOut[i] = 0.5f * (l + r);
        }
        return true;
    }

    return false;
}

std::vector<float> SampleEditWidget::getWavePeaksForPath_(const std::string& path) const {
    if (path.empty()) {
        return {};
    }
    if (const auto it = waveformCache_.find(path); it != waveformCache_.end()) {
        return it->second;
    }

    std::vector<float> mono{};
    std::vector<float> peaks{};
    if (decodeWavMono_(path, mono)) {
        peaks = buildWavePeaks_(mono, 256U);
    }
    if (waveformCache_.size() > 16U) {
        waveformCache_.clear();
    }
    waveformCache_[path] = peaks;
    return peaks;
}

bool SampleEditWidget::buildPreparedLayout(UiPreparedLayout& out,
                                           const UiState& rtState,
                                           const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    UiPreparedParams params = buildPreparedLayoutParams_(rtState, navState);
    uint16_t frameHeightHint = 24U;
    if (auto h = params.findInteger("frame.heightHint"); h.has_value()) {
        frameHeightHint = static_cast<uint16_t>(std::clamp<int32_t>(*h, 8, 4096));
    }

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("sample_edit")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(std::max<uint16_t>(frameWidth_, 36U))
        .frameHeightHint(frameHeightHint);

    UiNodeComponentComposer::compose(UiScene::SampleEdit, *layoutTemplate_, rtState, navState, params, builder);
    out = std::move(builder).build();
    return true;
}

UiPreparedParams SampleEditWidget::buildPreparedLayoutParams_(const UiState& rtState,
                                                              const UiNavState& navState) const {
    UiPreparedParams params{};
    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t selectedTrack = clampTrack_(navState.selectedTrack, totalTracks);
    const UiTrackStateView* tr = (totalTracks == 0U) ? nullptr : &rtState.tracks[selectedTrack];

    char title[128]{};
    std::snprintf(title, sizeof(title), " %s T%u ", layout_.title.c_str(), static_cast<unsigned>(selectedTrack + 1U));
    params.text["status.scene.title"] = title;
    params.text["header_title"] = title;

    std::string trackLine = " track: - ";
    if (tr) {
        trackLine = " clip:" + (tr->clipName.empty() ? std::string("-") : tr->clipName);
    }
    params.text["status.track"] = trackLine;
    params.text["track_line"] = trackLine;

    std::string modeLine = " mode: - ";
    if (tr) {
        modeLine = std::string(" mode:") + profileName_(tr->playbackProfile);
    }
    params.text["status.mode"] = modeLine;
    params.text["mode_line"] = modeLine;

    const float speed = tr ? tr->stretchRatio : 1.0f;
    const float gain = tr ? std::clamp(tr->gain01, 0.0f, 1.0f) : 1.0f;
    const float start = tr ? std::clamp(tr->trimStart01, 0.0f, 0.99f) : 0.0f;
    const float end = tr ? std::clamp(tr->trimEnd01, 0.01f, 1.0f) : 1.0f;
    const uint8_t profileIdx = tr ? profileToIndex_(tr->playbackProfile) : 2U;

    params.number["track.selected.playback_profile"] = static_cast<float>(profileIdx) / 3.0f;
    params.integer["track.selected.playback_profile.selectedIndex"] = profileIdx;
    params.text["track.selected.playback_profile.label"] = "MODE";

    params.number["track.selected.speed"] = speedTo01_(speed);
    params.text["track.selected.speed.label"] = "SPD";
    params.number["track.selected.gain"] = gain;
    params.text["track.selected.gain.label"] = "GAIN";
    params.number["track.selected.start"] = trimTo01_(start);
    params.text["track.selected.start.label"] = "START";
    params.number["track.selected.end"] = trimTo01_(end);
    params.text["track.selected.end.label"] = "END";
    const float tempoSync01 = (tr && tr->tempoSync) ? 1.0f : 0.0f;
    const int32_t tempoSyncIndex = (tempoSync01 >= 0.5f) ? 1 : 0;
    // Дублируем значения для двух форм canonical bind:
    // `tempo_sync` (legacy) и `tempo.sync` (после normalizer `_` -> `.`).
    // Это убирает визуальные рассинхроны switch при mixed layout/state ключах.
    params.number["track.selected.tempo_sync"] = tempoSync01;
    params.number["track.selected.tempo.sync"] = tempoSync01;
    params.integer["track.selected.tempo_sync.selectedIndex"] = tempoSyncIndex;
    params.integer["track.selected.tempo.sync.selectedIndex"] = tempoSyncIndex;
    params.text["track.selected.tempo_sync.label"] = "SYNC";
    params.text["track.selected.tempo.sync.label"] = "SYNC";

    UiAction::Id selectedActionId = UiAction::Id::None;
    {
        const UiActionCatalog actions = queryAvailableActions(rtState, navState);
        if (!actions.actions.empty()) {
            const uint16_t idx = std::min<uint16_t>(actions.currentIndex, static_cast<uint16_t>(actions.actions.size() - 1U));
            selectedActionId = actions.actions[idx].def.id;
        }
    }
    params.flag["track.selected.playback_profile.selected"] = (selectedActionId == UiAction::Id::SceneTrackPlaybackProfile);
    params.flag["track.selected.speed.selected"] = (selectedActionId == UiAction::Id::SceneTrackSpeed);
    params.flag["track.selected.gain.selected"] = (selectedActionId == UiAction::Id::SceneTrackGain);
    params.flag["track.selected.start.selected"] = (selectedActionId == UiAction::Id::SceneTrackTrimStart);
    params.flag["track.selected.end.selected"] = (selectedActionId == UiAction::Id::SceneTrackTrimEnd);
    const bool tempoSyncSelected = (selectedActionId == UiAction::Id::SceneTrackTempoSync);
    params.flag["track.selected.tempo_sync.selected"] = tempoSyncSelected;
    params.flag["track.selected.tempo.sync.selected"] = tempoSyncSelected;

    const std::vector<float> wavePeaks = tr ? getWavePeaksForPath_(tr->clipPath) : std::vector<float>{};
    params.waves["track.selected.waveform"] = wavePeaks;
    params.waves["sample_wave"] = wavePeaks;
    params.number["track.selected.waveform.trim_start"] = trimTo01_(start);
    params.number["track.selected.waveform.trim_end"] = trimTo01_(end);
    const bool previewOwnsPlayhead =
        rtState.transport.previewPlaying &&
        (navState.previewTrack == selectedTrack);
    const float sampleViewPlayhead01 = previewOwnsPlayhead
        ? std::clamp(rtState.transport.previewPlayhead01, 0.0f, 1.0f)
        : (tr ? std::clamp(tr->playhead01, 0.0f, 1.0f) : 0.0f);
    params.number["track.selected.waveform.playhead"] = sampleViewPlayhead01;
    params.number["sample_wave.trim_start"] = trimTo01_(start);
    params.number["sample_wave.trim_end"] = trimTo01_(end);
    params.number["sample_wave.playhead"] = sampleViewPlayhead01;
    params.number["fx.anim.current"] = gain;
    params.text["fx.anim.current.label"] = "";
    params.text["fx.anim.current.animKey"] = "track.anim";

    params.text["status.action"] = buildActionStatusLine_(rtState, navState);
    params.text["action_status"] = params.text["status.action"];
    params.text["status.keys"] = layout_.keysHint;
    params.text["keys_hint"] = layout_.keysHint;

    params.integer["frame.heightHint"] = 24;
    return params;
}

WidgetOutput SampleEditWidget::onGesture(UiGesture action,
                                         const UiState& rtState,
                                         UiNavState& navState) {
    WidgetOutput out{};
    out.handled = false;

    if (rtState.tracks.empty()) {
        return out;
    }
    const uint8_t t = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const UiTrackStateView& tr = rtState.tracks[t];

    if (action == UiGesture::PreviewPlay) {
        out.handled = true;
        if (rtState.transport.previewPlaying) {
            UiIntent stop{};
            stop.type = UiIntentType::PreviewStop;
            out.intents.push_back(std::move(stop));
            return out;
        }
        if (tr.clipPath.empty()) {
            UiIntent hud{};
            hud.type = UiIntentType::HudNotify;
            hud.hudLevel = UiHudIntentLevel::Action;
            hud.hudText = "LOAD SAMPLE FIRST";
            out.intents.push_back(std::move(hud));
            return out;
        }
        UiIntent preview{};
        preview.type = UiIntentType::PreviewRequest;
        preview.track = t;
        preview.path = tr.clipPath;
        preview.previewSpeed = std::clamp(tr.stretchRatio, 0.25f, 4.0f);
        preview.previewStart01 = std::clamp(tr.trimStart01, 0.0f, 1.0f);
        preview.previewEnd01 = std::clamp(tr.trimEnd01, 0.0f, 1.0f);
        preview.previewGain = std::clamp(tr.gain01, 0.0f, 1.0f);
        out.intents.push_back(std::move(preview));
        return out;
    }

    if (action == UiGesture::OpenSampleContextMenu) {
        out.handled = true;
        UiIntent open{};
        open.type = UiIntentType::OpenScene;
        open.scene = UiScene::SampleContextMenu;
        open.resetCursor = true;
        open.resetScroll = true;
        open.resetSceneActionIndex = true;
        out.intents.push_back(std::move(open));
        return out;
    }

    return out;
}

UiActionCatalog SampleEditWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};
    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t selectedTrack = clampTrack_(navState.selectedTrack, totalTracks);
    const bool hasTrack = totalTracks > 0U;

    const UiCapabilityState profileCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.playback_profile", layout_.targetTrackPlaybackProfile, rtState, navState);
    const UiCapabilityState speedCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.speed", layout_.targetTrackSpeed, rtState, navState);
    const UiCapabilityState gainCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.gain", layout_.targetTrackGain, rtState, navState);
    const UiCapabilityState startCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.start", layout_.targetTrackTrimStart, rtState, navState);
    const UiCapabilityState endCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.end", layout_.targetTrackTrimEnd, rtState, navState);
    const UiCapabilityState tempoSyncCap = UiCapabilityService::resolve(
        UiScene::SampleEdit, "track.selected.tempo_sync", layout_.targetTrackTempoSync, rtState, navState);

    auto push = [&out](UiAction action) {
        out.actions.push_back(std::move(action));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackPlaybackProfile;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "Track Mode";
        a.def.minValue = 0.0f;
        a.def.maxValue = 3.0f;
        a.def.step = 1.0f;
        a.state.enabled = hasTrack && profileCap.targetActive;
        a.state.value = hasTrack ? static_cast<float>(profileToIndex_(rtState.tracks[selectedTrack].playbackProfile)) : 2.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackSpeed;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Track Speed";
        a.def.minValue = 0.25f;
        a.def.maxValue = 4.0f;
        a.def.step = speedStep_;
        a.state.enabled = hasTrack && speedCap.targetActive;
        a.state.value = hasTrack ? rtState.tracks[selectedTrack].stretchRatio : 1.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackGain;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Track Gain";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = gainStep_;
        a.state.enabled = hasTrack && gainCap.targetActive;
        a.state.value = hasTrack ? std::clamp(rtState.tracks[selectedTrack].gain01, 0.0f, 1.0f) : 1.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackTrimStart;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Trim Start";
        a.def.minValue = 0.0f;
        a.def.maxValue = 0.99f;
        a.def.step = trimStep_;
        a.state.enabled = hasTrack && startCap.targetActive;
        a.state.value = hasTrack ? std::clamp(rtState.tracks[selectedTrack].trimStart01, 0.0f, 0.99f) : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackTempoSync;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "Tempo Sync";
        a.state.enabled = hasTrack && tempoSyncCap.targetActive;
        a.state.value = hasTrack && rtState.tracks[selectedTrack].tempoSync ? 1.0f : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneTrackTrimEnd;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateContinuous;
        a.def.valueKind = UiAction::ValueKind::Float;
        a.def.label = "Trim End";
        a.def.minValue = 0.01f;
        a.def.maxValue = 1.0f;
        a.def.step = trimStep_;
        a.state.enabled = hasTrack && endCap.targetActive;
        a.state.value = hasTrack ? std::clamp(rtState.tracks[selectedTrack].trimEnd01, 0.01f, 1.0f) : 1.0f;
        push(std::move(a));
    }

    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }
    return out;
}

WidgetOutput SampleEditWidget::onAction(UiAction& action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;
    if (!action.state.enabled || rtState.tracks.empty()) {
        return out;
    }

    const uint8_t t = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const UiTrackStateView& tr = rtState.tracks[t];

    auto pushIntent = [&out](UiIntent intent) {
        out.intents.push_back(std::move(intent));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneTrackPlaybackProfile: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            int next = static_cast<int>(profileToIndex_(tr.playbackProfile));
            if (action.op == UiAction::Op::AdjustPrev) {
                next = (next + 3) % 4;
            } else if (action.op == UiAction::Op::AdjustNext) {
                next = (next + 1) % 4;
            } else {
                next = (next + 1) % 4;
            }
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackPlaybackProfile, t, static_cast<float>(next), it)) {
                it.type = UiIntentType::SetTrackPlaybackProfile;
                it.track = t;
                it.value = static_cast<float>(next);
            }
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneTrackSpeed: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float step = action.def.step > 0.0f ? action.def.step : speedStep_;
            const float next = std::clamp(tr.stretchRatio + dir * step, 0.25f, 4.0f);
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackSpeed, t, next, it)) {
                it.type = UiIntentType::SetTrackSpeed;
                it.track = t;
                it.value = next;
            }
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneTrackGain: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float step = action.def.step > 0.0f ? action.def.step : gainStep_;
            const float next = std::clamp(tr.gain01 + dir * step, 0.0f, 1.0f);
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackGain, t, next, it)) {
                it.type = UiIntentType::SetTrackGain;
                it.track = t;
                it.value = next;
            }
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneTrackTrimStart: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float step = action.def.step > 0.0f ? action.def.step : trimStep_;
            float next = std::clamp(tr.trimStart01 + dir * step, 0.0f, 0.99f);
            if (next >= tr.trimEnd01 - 0.01f) {
                next = std::max(0.0f, tr.trimEnd01 - 0.01f);
            }
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackTrimStart, t, next, it)) {
                it.type = UiIntentType::SetTrackTrimStart;
                it.track = t;
                it.value = next;
            }
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneTrackTrimEnd: {
            if (action.op != UiAction::Op::AdjustPrev && action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const float dir = (action.op == UiAction::Op::AdjustNext) ? 1.0f : -1.0f;
            const float step = action.def.step > 0.0f ? action.def.step : trimStep_;
            float next = std::clamp(tr.trimEnd01 + dir * step, 0.01f, 1.0f);
            if (next <= tr.trimStart01 + 0.01f) {
                next = std::min(1.0f, tr.trimStart01 + 0.01f);
            }
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackTrimEnd, t, next, it)) {
                it.type = UiIntentType::SetTrackTrimEnd;
                it.track = t;
                it.value = next;
            }
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneTrackTempoSync: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext &&
                action.op != UiAction::Op::Press) {
                break;
            }
            const bool current = tr.tempoSync;
            bool next = current;
            if (action.op == UiAction::Op::AdjustPrev) {
                next = false;
            } else if (action.op == UiAction::Op::AdjustNext) {
                next = true;
            } else {
                next = !current;
            }
            UiIntent it{};
            if (!intentFromTarget(layout_.targetTrackTempoSync, t, next ? 1.0f : 0.0f, it)) {
                it.type = UiIntentType::SetTrackTempoSync;
                it.track = t;
                it.value = next ? 1.0f : 0.0f;
            }
            pushIntent(std::move(it));
        } break;

        default:
            out.handled = false;
            break;
    }

    return out;
}

std::string SampleEditWidget::buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const {
    const UiActionCatalog catalog = queryAvailableActions(rtState, navState);
    if (catalog.actions.empty()) {
        return " action:- ";
    }
    const std::size_t idx = std::min<std::size_t>(catalog.currentIndex, catalog.actions.size() - 1U);
    const UiAction& a = catalog.actions[idx];
    char buf[192]{};
    switch (a.def.id) {
        case UiAction::Id::SceneTrackPlaybackProfile:
            std::snprintf(buf, sizeof(buf), " action:%s (cycle/apply) ", a.def.label.c_str());
            break;
        case UiAction::Id::SceneTrackSpeed:
        case UiAction::Id::SceneTrackGain:
        case UiAction::Id::SceneTrackTrimStart:
        case UiAction::Id::SceneTrackTrimEnd:
            std::snprintf(buf, sizeof(buf), " action:%s = %.2f ", a.def.label.c_str(), a.state.value);
            break;
        case UiAction::Id::SceneTrackTempoSync:
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), a.state.value >= 0.5f ? "ON" : "OFF");
            break;
        default:
            std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
            break;
    }
    return std::string(buf);
}

void SampleEditWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "sample_edit") {
        return;
    }

    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (layout_.title.empty() &&
            node.type == UiLayoutNodeType::StatusBar &&
            !node.text.empty()) {
            layout_.title = node.text;
        }
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout_.keysHint = node.text;
        }
        if (node.type == UiLayoutNodeType::Knob ||
            node.type == UiLayoutNodeType::Switch) {
            const UiBindResolution bind = UiBindResolver::resolve(UiScene::SampleEdit, node.type, node.bind);
            if (!bind.ok) {
                return;
            }
            const UiTargetResolution target = UiTargetResolver::resolve(
                UiScene::SampleEdit,
                node.type,
                node.target,
                bind.canonical);
            if (!target.ok) {
                return;
            }
            if (bind.canonical == "track.selected.playback_profile") {
                layout_.targetTrackPlaybackProfile = target.canonical;
            } else if (bind.canonical == "track.selected.speed") {
                layout_.targetTrackSpeed = target.canonical;
            } else if (bind.canonical == "track.selected.gain") {
                layout_.targetTrackGain = target.canonical;
            } else if (bind.canonical == "track.selected.start") {
                layout_.targetTrackTrimStart = target.canonical;
            } else if (bind.canonical == "track.selected.end") {
                layout_.targetTrackTrimEnd = target.canonical;
            } else if (bind.canonical == "track.selected.tempo_sync" ||
                       bind.canonical == "track.selected.tempo.sync") {
                layout_.targetTrackTempoSync = target.canonical;
            }
        }
    });
    layout_.enabled = true;
}

} // namespace avantgarde
