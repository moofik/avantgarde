#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "contracts/IPattern.h"

namespace avantgarde {

/**
 * @brief Тип низкоуровневой операции применения снапшота при pattern switch.
 *
 * Это не UI-уровень и не "бизнес команда", а компактный apply-план, который
 * можно быстро пройти и скормить в существующий control/RT command path.
 */
enum class PatternApplyOpKind : uint8_t {
    TransportSetTempo = 0,
    TransportSetTimeSig = 1,
    TransportSetQuant = 2,
    TransportSetSwing = 3,
    TrackSetMuted = 4,
    TrackSetArmed = 5,
    TrackSetGain = 6,
    TrackSetPlaybackInc = 7,
    TrackSetBars = 8,
    TrackSetClipRef = 9,
    TrackParamSet = 10,
    FxParamSet = 11
};

/**
 * @brief Одна операция apply-плана.
 *
 * Поля универсальные: конкретная интерпретация зависит от kind.
 * Это уменьшает размер и упрощает батч-проход без виртуальных вызовов.
 */
struct PatternApplyOp {
    /// Вид операции (transport/track/fx).
    PatternApplyOpKind kind{PatternApplyOpKind::TrackParamSet};
    /// Целевой track id (если операция трековая), иначе 0.
    uint8_t trackId{0};
    /// Целевой FX slot для FxParamSet, иначе -1.
    int16_t slot{-1};
    /// Индекс параметра/ключа (для ParamSet семейств), иначе 0.
    uint16_t index{0};
    /// Основное float-значение операции.
    float value{0.0f};
    /// Дополнительное целочисленное значение (bars, clipRefId и т.п.).
    uint32_t valueU32{0};
};

/**
 * @brief Скомпилированный снимок паттерна (RT-friendly data view).
 *
 * ВАЖНО:
 * - Здесь только уже подготовленные данные состояния.
 * - Никаких IO-операций и создания DSP-модулей.
 *
 * Про клипы:
 * - Поле PatternTrackSnapshot::clipRefId хранится как ссылка на внешний
 *   clip-pool/registry проекта.
 * - Менеджер snapshot-ов не владеет и не хранит PCM-буферы.
 */
struct CompiledPatternSnapshot {
    /// Идентификатор паттерна.
    PatternId id{kInvalidPatternId};
    /// Монотонная версия снимка внутри менеджера.
    uint64_t revision{0};
    /// Транспортная часть состояния паттерна.
    PatternTransportSnapshot transport{};
    /// Нормализованные snapshots треков (отсортированы по trackId).
    std::vector<PatternTrackSnapshot> tracks{};
};

/**
 * @brief Готовый diff-план переключения from -> to.
 *
 * План содержит только изменившиеся операции; неизмененные параметры в него
 * не попадают. Это минимизирует работу в момент switch.
 */
struct CompiledSwitchPlan {
    /// Исходный паттерн. Может быть kInvalidPatternId для "full apply".
    PatternId from{kInvalidPatternId};
    /// Целевой паттерн.
    PatternId to{kInvalidPatternId};
    /// Ревизия целевого snapshot, на которой собран план.
    uint64_t toRevision{0};
    /// Линейный список apply-операций.
    std::vector<PatternApplyOp> ops{};
};

/**
 * @brief Менеджер precompiled snapshot-ов и diff-планов переключения.
 *
 * Зона ответственности:
 * - принимает PatternState из control/service слоя;
 * - нормализует и хранит compiled snapshot;
 * - строит быстрый diff from->to как список PatternApplyOp.
 *
 * Не делает:
 * - IO по файлам;
 * - создание/удаление DSP-модулей;
 * - загрузку/выгрузку clip buffer в память;
 * - RT-отправку команд.
 */
class PatternSnapshotManager final {
public:
    /**
     * @brief Добавить/обновить compiled snapshot по PatternState.
     * @param state Исходный паттерн.
     * @return true при успехе, false если state.id невалиден.
     *
     * Алгоритм:
     * 1) валидирует id;
     * 2) нормализует track/fx параметры (dedupe + stable order);
     * 3) присваивает новую revision;
     * 4) выполняет upsert в внутреннюю таблицу snapshots.
     *
     * Потоки:
     * - Вызывается из control/service слоя (вне RT).
     */
    bool upsert(const PatternState& state);
    /**
     * @brief Удалить snapshot по id.
     * @param id PatternId.
     * @return true если запись была и удалена, иначе false.
     *
     * @note Метод удаляет только compiled snapshot.
     *       Синхронизацию с PatternBank/clip-pool делает вызывающий слой.
     */
    bool erase(PatternId id) noexcept;
    /**
     * @brief Проверить наличие compiled snapshot.
     * @param id PatternId.
     * @return true если snapshot есть.
     *
     * Потоки:
     * - Read-only вызов из control/service.
     */
    bool contains(PatternId id) const noexcept;
    /**
     * @brief Получить compiled snapshot (read-only pointer).
     * @param id PatternId.
     * @param out Указатель на внутренний snapshot при успехе.
     * @return true если snapshot найден, иначе false.
     * @note Указатель валиден пока менеджер не удалит/не перезапишет этот id.
     *
     * Потоки:
     * - Read-only вызов из control/service.
     */
    bool get(PatternId id, const CompiledPatternSnapshot*& out) const noexcept;
    /**
     * @brief Построить diff-план переключения from->to.
     * @param from Исходный pattern id, либо kInvalidPatternId для full apply.
     * @param to Целевой pattern id.
     * @param out Выходной план операций.
     * @return true если целевой snapshot найден и план построен.
     *
     * Поведение:
     * - Если from отсутствует/invalid -> строится full-apply план для to.
     * - Если from есть -> строится diff только по измененным полям.
     * - Для исчезнувших в target треков добавляется reset-план.
     *
     * Назначение:
     * - Результирующий план можно быстро пройти в момент switch и перевести
     *   в существующий командный путь без дополнительного анализа состояния.
     */
    bool buildSwitchPlan(PatternId from, PatternId to, CompiledSwitchPlan& out) const;

private:
    static PatternTrackSnapshot normalizeTrack_(const PatternTrackSnapshot& in);
    static std::vector<PatternTrackSnapshot> normalizeTracks_(const std::vector<PatternTrackSnapshot>& in);

    static void appendTransportDiff_(const PatternTransportSnapshot* src,
                                     const PatternTransportSnapshot& dst,
                                     std::vector<PatternApplyOp>& out);
    static void appendTrackDiff_(const PatternTrackSnapshot* src,
                                 const PatternTrackSnapshot& dst,
                                 std::vector<PatternApplyOp>& out);
    static void appendTrackResetDiff_(const PatternTrackSnapshot& src,
                                      std::vector<PatternApplyOp>& out);

private:
    /// Таблица compiled snapshot-ов по pattern id.
    std::unordered_map<PatternId, CompiledPatternSnapshot> snapshots_{};
    /// Внутренний генератор ревизий snapshot-ов (монотонно растет).
    uint64_t revisionCounter_{0};
};

} // namespace avantgarde
