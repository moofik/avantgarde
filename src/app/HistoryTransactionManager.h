#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <vector>

#include "contracts/UiIntent.h"

namespace avantgarde {

// Менеджер истории и транзакций для undo/redo на уровне application.
// Ключевая идея:
// - в историю пишется не "один клик", а пакет изменений (транзакция);
// - один undo/redo шаг применяет целый пакет изменений за раз.
class HistoryTransactionManager {
public:
    // Элементарное изменение для истории:
    // - undoIntent возвращает параметр в состояние "до изменения";
    // - redoIntent повторяет исходное изменение.
    struct Change {
        UiIntent undoIntent{};
        UiIntent redoIntent{};
    };

    // Один шаг истории (может содержать множество изменений).
    struct Entry {
        std::vector<Change> changes{};
    };

    explicit HistoryTransactionManager(std::size_t depth = 4) noexcept;

    // Полный сброс транзакции и истории.
    void clear() noexcept;
    // Сброс только redo-ветки (используется при новом изменении).
    void clearRedo() noexcept;

    // Открывает транзакцию для накопления batch-изменений.
    bool begin();
    // Добавляет change в открытую транзакцию.
    bool record(Change change);
    // Фиксирует транзакцию в undo-стек (если есть изменения).
    bool commit();
    // Отменяет текущую транзакцию без записи в историю.
    void cancel() noexcept;

    // Удобный путь для одиночного изменения (без ручного begin/commit).
    bool pushAtomic(Change change);

    // Применяет последний undo/redo шаг через переданную функцию-применитель.
    // applyFn должна уметь применить любой UiIntent к текущей модели.
    bool undo(const std::function<bool(const UiIntent&)>& applyFn);
    bool redo(const std::function<bool(const UiIntent&)>& applyFn);

    bool inTransaction() const noexcept;
    std::size_t undoSize() const noexcept;
    std::size_t redoSize() const noexcept;

private:
    // Нормализованная глубина истории (минимум 1).
    std::size_t depth_{4};
    // Флаг открытой транзакции.
    bool txOpen_{false};
    // Буфер изменений текущей транзакции.
    std::vector<Change> txBuffer_{};
    // История "назад" и "вперед".
    std::deque<Entry> undoStack_{};
    std::deque<Entry> redoStack_{};
};

} // namespace avantgarde

