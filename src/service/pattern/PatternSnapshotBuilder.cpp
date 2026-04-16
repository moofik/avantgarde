#include "service/pattern/PatternSnapshotBuilder.h"

#include <algorithm>
#include <limits>

#include "contracts/ids.h"

namespace avantgarde {

PatternTrackSnapshot PatternSnapshotBuilder::makeDefaultTrackSnapshot_(uint8_t trackId) {
    PatternTrackSnapshot tr{};
    tr.trackId = trackId;
    tr.muted = false;
    tr.armed = false;
    tr.gain01 = 1.0f;
    tr.playbackInc = 1.0f;
    tr.bars = 4u;
    tr.clipRefId = 0u;
    tr.trackParams.push_back(ParamKV{toParamIndex(TrackParamId::LoopEnabled), 1.0f});
    tr.trackParams.push_back(ParamKV{toParamIndex(TrackParamId::PlaybackMode),
                                     toParamValue(TrackPlaybackModeValue::Looper)});
    tr.trackParams.push_back(ParamKV{toParamIndex(TrackParamId::StartNorm), 0.0f});
    tr.trackParams.push_back(ParamKV{toParamIndex(TrackParamId::EndNorm), 1.0f});
    tr.trackParams.push_back(ParamKV{toParamIndex(TrackParamId::TempoSyncEnabled), 1.0f});
    return tr;
}

bool PatternSnapshotBuilder::readSnapshotRecord_(const ISnapshotable& source,
                                                 SnapshotRecord& out) noexcept {
    out = SnapshotRecord{};
    if (!source.getSnapshot(out)) {
        return false;
    }
    return true;
}

void PatternSnapshotBuilder::applySnapshotRecord_(const SnapshotRecord& record,
                                                  PatternState& state,
                                                  uint8_t trackId) noexcept {
    switch (record.domain) {
        case SnapshotDomain::Transport:
            state.transport.bpm = record.transport.bpm;
            state.transport.tsNum = record.transport.tsNum;
            state.transport.tsDen = record.transport.tsDen;
            state.transport.quant = record.transport.quant;
            state.transport.swing01 = record.transport.swing01;
            break;
        case SnapshotDomain::Track:
            if (trackId < state.tracks.size()) {
                copyTrackSnapshotToPattern_(record.track, state.tracks[trackId]);
            }
            break;
        case SnapshotDomain::Unknown:
        default:
            break;
    }
}

void PatternSnapshotBuilder::copyTrackSnapshotToPattern_(const TrackSnapshot& src,
                                                         PatternTrackSnapshot& dst) {
    dst.muted = src.muted;
    dst.armed = src.armed;
    dst.gain01 = src.gain01;
    dst.playbackInc = src.playbackInc;
    dst.bars = src.bars;
    dst.clipRefId = src.clipRefId;
    dst.trackParams.clear();
    dst.trackParams.push_back(ParamKV{
        toParamIndex(TrackParamId::LoopEnabled),
        src.loopEnabled ? 1.0f : 0.0f});
    dst.trackParams.push_back(ParamKV{
        toParamIndex(TrackParamId::PlaybackMode),
        toParamValue(src.playbackMode)});
    dst.trackParams.push_back(ParamKV{
        toParamIndex(TrackParamId::StartNorm),
        src.trimStart01});
    dst.trackParams.push_back(ParamKV{
        toParamIndex(TrackParamId::EndNorm),
        src.trimEnd01});
    dst.trackParams.push_back(ParamKV{
        toParamIndex(TrackParamId::TempoSyncEnabled),
        src.tempoSync ? 1.0f : 0.0f});
}

void PatternSnapshotBuilder::applyLayoutDefaults_(PatternState& state,
                                                  const LayoutDefaults& layout) {
    state.ppq = kSequencerPpq;
    state.lengthBars = std::max<uint32_t>(1u, layout.lengthBars);
    state.lengthInSteps = std::max<uint32_t>(1u, layout.lengthInSteps);
    state.stepsPerBeat = std::max<uint16_t>(1u, layout.stepsPerBeat);

    // Канонически считаем lengthTicks от bars при фиксированном PPQ.
    const uint64_t ticks64 =
        static_cast<uint64_t>(state.ppq) * 4ull * static_cast<uint64_t>(state.lengthBars);
    const uint64_t safeTicks = std::max<uint64_t>(1ull, ticks64);
    const uint64_t capped = std::min<uint64_t>(safeTicks, std::numeric_limits<SequencerTick>::max());
    state.lengthTicks = static_cast<SequencerTick>(capped);
}

PatternState PatternSnapshotBuilder::makeDefaultPattern(PatternId id,
                                                        uint8_t trackCount,
                                                        const PatternTransportSnapshot& transport) const {
    return makeDefaultPattern(id, trackCount, transport, LayoutDefaults{});
}

PatternState PatternSnapshotBuilder::makeDefaultPattern(PatternId id,
                                                        uint8_t trackCount,
                                                        const PatternTransportSnapshot& transport,
                                                        const LayoutDefaults& layout) const {
    PatternState out{};
    out.id = id;
    out.transport = transport;
    applyLayoutDefaults_(out, layout);
    out.tracks.reserve(trackCount);
    for (uint8_t t = 0; t < trackCount; ++t) {
        out.tracks.push_back(makeDefaultTrackSnapshot_(t));
    }
    return out;
}

bool PatternSnapshotBuilder::buildFromSnapshotables(PatternId id,
                                                    std::span<ISnapshotable* const> sources,
                                                    PatternState& out) const noexcept {
    return buildFromSnapshotables(id, sources, out, LayoutDefaults{});
}

bool PatternSnapshotBuilder::buildFromSnapshotables(PatternId id,
                                                    std::span<ISnapshotable* const> sources,
                                                    PatternState& out,
                                                    const LayoutDefaults& layout) const noexcept {
    std::vector<SnapshotRecord> records{};
    records.reserve(sources.size());
    int32_t maxTrackId = -1;
    bool hasTransport = false;
    for (ISnapshotable* src : sources) {
        if (!src) {
            continue;
        }
        SnapshotRecord record{};
        if (!readSnapshotRecord_(*src, record)) {
            continue;
        }
        if (record.domain == SnapshotDomain::Track) {
            if (record.entityId < 0 || record.entityId > 255) {
                // Строгий режим без fallback:
                // трековый snapshot обязан нести валидный entityId [0..255].
                return false;
            }
            maxTrackId = std::max(maxTrackId, record.entityId);
        } else if (record.domain == SnapshotDomain::Transport) {
            if (record.entityId != kSnapshotEntityTransport) {
                // Строгий режим без fallback:
                // transport snapshot обязан иметь канонический entityId.
                return false;
            }
            hasTransport = true;
        }
        records.push_back(record);
    }

    if (!hasTransport) {
        return false;
    }

    const uint8_t trackCount = (maxTrackId >= 0)
        ? static_cast<uint8_t>(std::min<int32_t>(maxTrackId + 1, 255))
        : static_cast<uint8_t>(0);
    out = makeDefaultPattern(id, trackCount, PatternTransportSnapshot{}, layout);

    bool transportApplied = false;
    for (const SnapshotRecord& record : records) {
        if (record.domain == SnapshotDomain::Transport) {
            if (!transportApplied) {
                applySnapshotRecord_(record, out, 0);
                transportApplied = true;
            }
            continue;
        }
        if (record.domain == SnapshotDomain::Track) {
            const uint8_t trackId = static_cast<uint8_t>(record.entityId);
            if (trackId < trackCount) {
                applySnapshotRecord_(record, out, trackId);
            }
            continue;
        }
    }
    return true;
}

} // namespace avantgarde
