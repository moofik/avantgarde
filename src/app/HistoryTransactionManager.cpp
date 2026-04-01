#include "app/HistoryTransactionManager.h"

#include <algorithm>
#include <utility>

namespace avantgarde {

HistoryTransactionManager::HistoryTransactionManager(std::size_t depth) noexcept
    : depth_(std::max<std::size_t>(1U, depth)) {}

void HistoryTransactionManager::clear() noexcept {
    txOpen_ = false;
    txBuffer_.clear();
    undoStack_.clear();
    redoStack_.clear();
}

void HistoryTransactionManager::clearRedo() noexcept {
    redoStack_.clear();
}

bool HistoryTransactionManager::begin() {
    if (txOpen_) {
        return false;
    }
    txOpen_ = true;
    txBuffer_.clear();
    return true;
}

bool HistoryTransactionManager::record(Change change) {
    if (!txOpen_) {
        return false;
    }
    txBuffer_.push_back(std::move(change));
    return true;
}

bool HistoryTransactionManager::commit() {
    if (!txOpen_) {
        return false;
    }
    txOpen_ = false;
    if (txBuffer_.empty()) {
        return false;
    }
    if (undoStack_.size() >= depth_) {
        undoStack_.pop_front();
    }
    undoStack_.push_back(Entry{std::move(txBuffer_)});
    txBuffer_.clear();
    // Любой новый commit отрезает redo-ветку.
    redoStack_.clear();
    return true;
}

void HistoryTransactionManager::cancel() noexcept {
    txOpen_ = false;
    txBuffer_.clear();
}

bool HistoryTransactionManager::pushAtomic(Change change) {
    if (!begin()) {
        return false;
    }
    (void)record(std::move(change));
    return commit();
}

bool HistoryTransactionManager::undo(const std::function<bool(const UiIntent&)>& applyFn) {
    if (txOpen_ || undoStack_.empty() || !applyFn) {
        return false;
    }
    Entry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    bool changed = false;
    for (auto it = entry.changes.rbegin(); it != entry.changes.rend(); ++it) {
        changed = applyFn(it->undoIntent) || changed;
    }

    if (redoStack_.size() >= depth_) {
        redoStack_.pop_front();
    }
    redoStack_.push_back(std::move(entry));
    return changed;
}

bool HistoryTransactionManager::redo(const std::function<bool(const UiIntent&)>& applyFn) {
    if (txOpen_ || redoStack_.empty() || !applyFn) {
        return false;
    }
    Entry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    bool changed = false;
    for (const Change& change : entry.changes) {
        changed = applyFn(change.redoIntent) || changed;
    }

    if (undoStack_.size() >= depth_) {
        undoStack_.pop_front();
    }
    undoStack_.push_back(std::move(entry));
    return changed;
}

bool HistoryTransactionManager::inTransaction() const noexcept {
    return txOpen_;
}

std::size_t HistoryTransactionManager::undoSize() const noexcept {
    return undoStack_.size();
}

std::size_t HistoryTransactionManager::redoSize() const noexcept {
    return redoStack_.size();
}

} // namespace avantgarde

