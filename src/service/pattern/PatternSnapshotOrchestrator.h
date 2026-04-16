#pragma once

#include <cstdint>
#include <span>

#include "contracts/IPattern.h"
#include "contracts/ISnapshotable.h"
#include "service/pattern/PatternEngine.h"
#include "service/pattern/PatternSnapshotBuilder.h"

namespace avantgarde {

/**
 * @brief Оркестратор захвата snapshot-ов паттернов в PatternEngine.
 *
 * Роль:
 * - читает active pattern id из `PatternEngine`;
 * - просит `PatternSnapshotBuilder` собрать `PatternState` из единого списка snapshot-источников;
 * - публикует обновленный state обратно через `PatternEngine::putPattern`.
 *
 * Таким образом слой приложения не держит у себя доменную логику сборки
 * `PatternState` и не зависит от структуры track snapshot.
 */
class PatternSnapshotOrchestrator final {
public:
    explicit PatternSnapshotOrchestrator(PatternEngine& engine) noexcept
        : engine_(&engine) {}

    /**
     * @brief Сохранить live-state активного паттерна обратно в bank/snapshot manager.
     * @param sources Единый список snapshot-источников (transport + tracks).
     * @return true если active pattern существует и успешно обновлен.
     */
    bool captureActivePattern(std::span<ISnapshotable* const> sources) noexcept;

    /**
     * @brief Добавить дефолтный паттерн в engine.
     * @param id PatternId.
     * @param transport Стартовый transport snapshot для паттерна.
     * @param trackCount Число треков.
     * @return true если putPattern успешен.
     */
    bool putDefaultPattern(PatternId id,
                           const PatternTransportSnapshot& transport,
                           uint8_t trackCount);

    PatternSnapshotBuilder& builder() noexcept { return builder_; }
    const PatternSnapshotBuilder& builder() const noexcept { return builder_; }

private:
    PatternEngine* engine_{nullptr};
    PatternSnapshotBuilder builder_{};
};

} // namespace avantgarde
