#include "service/pattern/PatternSnapshotManager.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace avantgarde {
namespace {

bool floatEq(float a, float b) noexcept {
    // Для diff-плана используем компактное сравнение float с epsilon.
    // Этого достаточно для normalized param/state значений в паттернах.
    return std::abs(a - b) <= 1e-6f;
}

PatternApplyOp makeOp(PatternApplyOpKind kind,
                      uint8_t track,
                      int16_t slot,
                      uint16_t index,
                      float value,
                      uint32_t valueU32 = 0) {
    PatternApplyOp op{};
    op.kind = kind;
    op.trackId = track;
    op.slot = slot;
    op.index = index;
    op.value = value;
    op.valueU32 = valueU32;
    return op;
}

const PatternTrackSnapshot* findTrack(const std::vector<PatternTrackSnapshot>& tracks, uint8_t trackId) {
    auto it = std::lower_bound(tracks.begin(), tracks.end(), trackId,
                               [](const PatternTrackSnapshot& t, uint8_t id) {
                                   return t.trackId < id;
                               });
    if (it == tracks.end() || it->trackId != trackId) {
        return nullptr;
    }
    return &(*it);
}

std::vector<ParamKV> normalizeTrackParams(const std::vector<ParamKV>& in) {
    // Канонизация трековых параметров:
    // - объединяем дубли по index;
    // - last-wins для повторяющихся ключей;
    // - на выходе ключи упорядочены по index.
    std::map<uint16_t, float> merged{};
    for (const ParamKV& kv : in) {
        merged[kv.index] = kv.value; // last-wins by key
    }
    std::vector<ParamKV> out{};
    out.reserve(merged.size());
    for (const auto& [idx, val] : merged) {
        out.push_back(ParamKV{.index = idx, .value = val});
    }
    return out;
}

std::vector<PatternFxParam> normalizeFxParams(const std::vector<PatternFxParam>& in) {
    // Канонизация FX-параметров:
    // - ключ = (slot,index);
    // - дубли схлопываем по last-wins;
    // - выдаем в стабильном отсортированном порядке.
    std::map<std::pair<uint8_t, uint16_t>, float> merged{};
    for (const PatternFxParam& p : in) {
        merged[{p.slot, p.index}] = p.value; // last-wins by (slot,index)
    }
    std::vector<PatternFxParam> out{};
    out.reserve(merged.size());
    for (const auto& [k, val] : merged) {
        out.push_back(PatternFxParam{
            .slot = k.first,
            .index = k.second,
            .value = val
        });
    }
    return out;
}

} // namespace

bool PatternSnapshotManager::upsert(const PatternState& state) {
    // Невалидный id не кладем в snapshot-storage.
    if (state.id == kInvalidPatternId) {
        return false;
    }

    // Компилируем control-level state в каноничный snapshot.
    // Здесь нет IO и нет DSP-операций: только подготовка данных.
    CompiledPatternSnapshot compiled{};
    compiled.id = state.id;
    compiled.revision = ++revisionCounter_;
    compiled.transport = state.transport;
    compiled.tracks = normalizeTracks_(state.tracks);
    // Upsert-поведение: старый snapshot заменяется целиком.
    snapshots_[compiled.id] = std::move(compiled);
    return true;
}

bool PatternSnapshotManager::erase(PatternId id) noexcept {
    // Удаляет только compiled snapshot.
    // Внешний clip-pool и bank управляются отдельно.
    return snapshots_.erase(id) > 0;
}

bool PatternSnapshotManager::contains(PatternId id) const noexcept {
    return snapshots_.find(id) != snapshots_.end();
}

bool PatternSnapshotManager::get(PatternId id, const CompiledPatternSnapshot*& out) const noexcept {
    const auto it = snapshots_.find(id);
    if (it == snapshots_.end()) {
        out = nullptr;
        return false;
    }
    out = &it->second;
    return true;
}

bool PatternSnapshotManager::buildSwitchPlan(PatternId from, PatternId to, CompiledSwitchPlan& out) const {
    // Целевой snapshot обязателен.
    const CompiledPatternSnapshot* dst = nullptr;
    if (!get(to, dst) || !dst) {
        return false;
    }

    // Source может отсутствовать: это трактуем как full-apply.
    const CompiledPatternSnapshot* src = nullptr;
    (void)get(from, src);

    CompiledSwitchPlan plan{};
    plan.from = from;
    plan.to = to;
    plan.toRevision = dst->revision;
    plan.ops.clear();

    // 1) transport diff.
    appendTransportDiff_(src ? &src->transport : nullptr, dst->transport, plan.ops);

    // 2) track/fx diff для треков, присутствующих в target.
    for (const PatternTrackSnapshot& toTrack : dst->tracks) {
        const PatternTrackSnapshot* fromTrack = src ? findTrack(src->tracks, toTrack.trackId) : nullptr;
        appendTrackDiff_(fromTrack, toTrack, plan.ops);
    }

    // 3) reset для треков, которых нет в target, но были в source.
    if (src) {
        for (const PatternTrackSnapshot& fromTrack : src->tracks) {
            const PatternTrackSnapshot* toTrack = findTrack(dst->tracks, fromTrack.trackId);
            if (!toTrack) {
                appendTrackResetDiff_(fromTrack, plan.ops);
            }
        }
    }

    out = std::move(plan);
    return true;
}

PatternTrackSnapshot PatternSnapshotManager::normalizeTrack_(const PatternTrackSnapshot& in) {
    // Нормализация одного трека: фиксируем каноничный порядок и правила дублей.
    PatternTrackSnapshot out = in;
    out.trackParams = normalizeTrackParams(in.trackParams);
    out.fxParams = normalizeFxParams(in.fxParams);
    return out;
}

std::vector<PatternTrackSnapshot> PatternSnapshotManager::normalizeTracks_(const std::vector<PatternTrackSnapshot>& in) {
    // Храним tracks отсортированными по trackId для быстрого findTrack(binary search).
    std::vector<PatternTrackSnapshot> out{};
    out.reserve(in.size());
    for (const PatternTrackSnapshot& t : in) {
        out.push_back(normalizeTrack_(t));
    }
    std::sort(out.begin(), out.end(),
              [](const PatternTrackSnapshot& a, const PatternTrackSnapshot& b) {
                  return a.trackId < b.trackId;
              });
    return out;
}

void PatternSnapshotManager::appendTransportDiff_(const PatternTransportSnapshot* src,
                                                  const PatternTransportSnapshot& dst,
                                                  std::vector<PatternApplyOp>& out) {
    // Если source отсутствует, все поля target публикуем как full-apply.
    if (!src || !floatEq(src->bpm, dst.bpm)) {
        out.push_back(makeOp(PatternApplyOpKind::TransportSetTempo, 0, -1, 0, dst.bpm));
    }
    if (!src || src->tsNum != dst.tsNum || src->tsDen != dst.tsDen) {
        out.push_back(makeOp(PatternApplyOpKind::TransportSetTimeSig, 0, -1, dst.tsDen, static_cast<float>(dst.tsNum)));
    }
    if (!src || src->quant != dst.quant) {
        out.push_back(makeOp(PatternApplyOpKind::TransportSetQuant, 0, -1, 0, static_cast<float>(static_cast<uint8_t>(dst.quant))));
    }
    if (!src || !floatEq(src->swing01, dst.swing01)) {
        out.push_back(makeOp(PatternApplyOpKind::TransportSetSwing, 0, -1, 0, dst.swing01));
    }
}

void PatternSnapshotManager::appendTrackDiff_(const PatternTrackSnapshot* src,
                                              const PatternTrackSnapshot& dst,
                                              std::vector<PatternApplyOp>& out) {
    // Базовые поля трека.
    if (!src || src->muted != dst.muted) {
        out.push_back(makeOp(PatternApplyOpKind::TrackSetMuted, dst.trackId, -1, 0, dst.muted ? 1.0f : 0.0f));
    }
    if (!src || src->armed != dst.armed) {
        out.push_back(makeOp(PatternApplyOpKind::TrackSetArmed, dst.trackId, -1, 0, dst.armed ? 1.0f : 0.0f));
    }
    if (!src || !floatEq(src->gain01, dst.gain01)) {
        out.push_back(makeOp(PatternApplyOpKind::TrackSetGain, dst.trackId, -1, 0, dst.gain01));
    }
    if (!src || !floatEq(src->playbackInc, dst.playbackInc)) {
        out.push_back(makeOp(PatternApplyOpKind::TrackSetPlaybackInc, dst.trackId, -1, 0, dst.playbackInc));
    }
    if (!src || src->bars != dst.bars) {
        out.push_back(makeOp(PatternApplyOpKind::TrackSetBars, dst.trackId, -1, 0, 0.0f, dst.bars));
    }
    if (!src || src->clipRefId != dst.clipRefId) {
        // Здесь только смена ссылки clipRefId.
        // Реальный буфер должен быть уже preloaded во внешнем clip-pool.
        out.push_back(makeOp(PatternApplyOpKind::TrackSetClipRef, dst.trackId, -1, 0, 0.0f, dst.clipRefId));
    }

    // Diff по track params: изменившиеся ключи + reset отсутствующих в target.
    std::map<uint16_t, float> srcTrackParams{};
    if (src) {
        for (const ParamKV& kv : src->trackParams) {
            srcTrackParams[kv.index] = kv.value;
        }
    }
    std::map<uint16_t, float> dstTrackParams{};
    for (const ParamKV& kv : dst.trackParams) {
        dstTrackParams[kv.index] = kv.value;
    }

    for (const auto& [idx, dstVal] : dstTrackParams) {
        const auto it = srcTrackParams.find(idx);
        if (it == srcTrackParams.end() || !floatEq(it->second, dstVal)) {
            out.push_back(makeOp(PatternApplyOpKind::TrackParamSet, dst.trackId, -1, idx, dstVal));
        }
    }
    for (const auto& [idx, srcVal] : srcTrackParams) {
        if (dstTrackParams.find(idx) == dstTrackParams.end() && !floatEq(srcVal, 0.0f)) {
            out.push_back(makeOp(PatternApplyOpKind::TrackParamSet, dst.trackId, -1, idx, 0.0f));
        }
    }

    // Diff по FX params: изменившиеся (slot,index) + reset отсутствующих в target.
    using FxKey = std::pair<uint8_t, uint16_t>;
    std::map<FxKey, float> srcFxParams{};
    if (src) {
        for (const PatternFxParam& p : src->fxParams) {
            srcFxParams[{p.slot, p.index}] = p.value;
        }
    }
    std::map<FxKey, float> dstFxParams{};
    for (const PatternFxParam& p : dst.fxParams) {
        dstFxParams[{p.slot, p.index}] = p.value;
    }

    for (const auto& [key, dstVal] : dstFxParams) {
        const auto it = srcFxParams.find(key);
        if (it == srcFxParams.end() || !floatEq(it->second, dstVal)) {
            out.push_back(makeOp(PatternApplyOpKind::FxParamSet, dst.trackId,
                                 static_cast<int16_t>(key.first), key.second, dstVal));
        }
    }
    for (const auto& [key, srcVal] : srcFxParams) {
        if (dstFxParams.find(key) == dstFxParams.end() && !floatEq(srcVal, 0.0f)) {
            out.push_back(makeOp(PatternApplyOpKind::FxParamSet, dst.trackId,
                                 static_cast<int16_t>(key.first), key.second, 0.0f));
        }
    }
}

void PatternSnapshotManager::appendTrackResetDiff_(const PatternTrackSnapshot& src,
                                                   std::vector<PatternApplyOp>& out) {
    // Трек полностью исчез из target-паттерна:
    // формируем безопасный reset-набор.
    out.push_back(makeOp(PatternApplyOpKind::TrackSetMuted, src.trackId, -1, 0, 1.0f));
    out.push_back(makeOp(PatternApplyOpKind::TrackSetArmed, src.trackId, -1, 0, 0.0f));
    out.push_back(makeOp(PatternApplyOpKind::TrackSetClipRef, src.trackId, -1, 0, 0.0f, 0));
    for (const ParamKV& kv : src.trackParams) {
        if (!floatEq(kv.value, 0.0f)) {
            out.push_back(makeOp(PatternApplyOpKind::TrackParamSet, src.trackId, -1, kv.index, 0.0f));
        }
    }
    for (const PatternFxParam& p : src.fxParams) {
        if (!floatEq(p.value, 0.0f)) {
            out.push_back(makeOp(PatternApplyOpKind::FxParamSet, src.trackId,
                                 static_cast<int16_t>(p.slot), p.index, 0.0f));
        }
    }
}

} // namespace avantgarde
