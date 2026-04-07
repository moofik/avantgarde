#pragma once

#include <unordered_map>

#include "contracts/IPattern.h"

namespace avantgarde {

/**
 * @brief In-memory хранилище паттернов.
 *
 * Контекст:
 * - Используется в control/service слое.
 * - Хранит полные снимки PatternState по стабильному PatternId.
 * - Не имеет RT-ограничений, допускает обычные STL-контейнеры.
 *
 * Ограничения:
 * - Потокобезопасность внешне не гарантируется: синхронизация при
 *   конкурентном доступе должна обеспечиваться вызывающей стороной.
 */
class PatternBank final : public IPatternBank {
public:
    /**
     * @brief Количество сохраненных паттернов в банке.
     * @return Число записей (PatternId -> PatternState).
     */
    std::size_t size() const noexcept override;
    /**
     * @brief Проверить существование паттерна по id.
     * @param id Идентификатор паттерна.
     * @return true если паттерн есть в банке; иначе false.
     */
    bool contains(PatternId id) const noexcept override;
    /**
     * @brief Получить копию состояния паттерна.
     * @param id Идентификатор паттерна.
     * @param out Выходной буфер, куда копируется PatternState при успехе.
     * @return true если паттерн найден и скопирован; иначе false.
     */
    bool get(PatternId id, PatternState& out) const override;
    /**
     * @brief Сохранить или обновить паттерн.
     * @param state Полный снимок PatternState.
     * @return true если запись выполнена; false если id невалиден.
     * @note Повторный put с тем же id заменяет существующее состояние.
     */
    bool put(const PatternState& state) override;
    /**
     * @brief Удалить паттерн по id.
     * @param id Идентификатор паттерна.
     * @return true если запись была и удалена; иначе false.
     */
    bool erase(PatternId id) override;

private:
    /**
     * @brief Основное хранилище снимков паттернов.
     *
     * Ключ:
     * - PatternId (устойчивый id в рамках проекта/банка).
     *
     * Значение:
     * - Полный PatternState (transport snapshot, track snapshots, step events).
     */
    std::unordered_map<PatternId, PatternState> states_{};
};

} // namespace avantgarde
