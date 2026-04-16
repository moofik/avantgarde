#pragma once

#include <cstdint>

#include "ids.h"

namespace avantgarde {

enum class QuantizeMode : uint8_t;

// Унифицированный снимок состояния трека (control-модель, не DSP state).
struct TrackSnapshot {
    bool muted{false};
    bool armed{false};
    float gain01{1.0f};
    float playbackInc{1.0f};
    uint32_t bars{4u};
    uint32_t clipRefId{0u};
    TrackPlaybackModeValue playbackMode{TrackPlaybackModeValue::Looper};
    bool loopEnabled{true};
    bool tempoSync{true};
    float trimStart01{0.0f};
    float trimEnd01{1.0f};
};

// Унифицированный снимок состояния транспорта (control+rt-модель).
struct TransportSnapshot {
    bool playing{false};
    float bpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{};
    float swing01{0.0f};
    uint64_t sampleTime{0};
};

enum class SnapshotDomain : uint8_t {
    Unknown = 0,
    Track = 1,
    Transport = 2
};

// Канонические entityId для unified snapshot pipeline.
// Track:   [0..N-1]
// Transport: kSnapshotEntityTransport
// Unknown/unset: kSnapshotEntityUnset
constexpr int32_t kSnapshotEntityUnset = -2;
constexpr int32_t kSnapshotEntityTransport = -1;

struct SnapshotRecord {
    SnapshotDomain domain{SnapshotDomain::Unknown};
    int32_t entityId{kSnapshotEntityUnset};
    TrackSnapshot track{};
    TransportSnapshot transport{};
};

/**
 * @brief Унифицированный контракт для любых сущностей, умеющих отдавать snapshot.
 *
 * Зачем введен:
 * - чтобы сборка pattern snapshot не зависела от конкретных доменных impl-классов;
 * - чтобы и треки, и транспорт, и будущие сущности участвовали в snapshot-flow
 *   через единый API.
 *
 * Контракт:
 * - `getSnapshot` заполняет `SnapshotRecord` и возвращает true при успехе;
 * - `domain` внутри `SnapshotRecord` определяет, какая ветка payload валидна.
 * - `entityId` обязателен:
 *   - для `Track` это trackId [0..N-1];
 *   - для `Transport` это `kSnapshotEntityTransport`.
 */
struct ISnapshotable {
    virtual ~ISnapshotable() = default;
    virtual bool getSnapshot(SnapshotRecord& out) const noexcept = 0;
};

} // namespace avantgarde
