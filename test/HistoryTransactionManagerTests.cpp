#include <catch2/catch_all.hpp>

#include <vector>

#include "app/HistoryTransactionManager.h"

using namespace avantgarde;

namespace {

UiIntent bpmIntent(float bpm) {
    UiIntent it{};
    it.type = UiIntentType::SetTransportBpm;
    it.value = bpm;
    return it;
}

} // namespace

TEST_CASE("HistoryTransactionManager: atomic change supports undo/redo") {
    HistoryTransactionManager history(4);

    HistoryTransactionManager::Change c{};
    c.undoIntent = bpmIntent(100.0f);
    c.redoIntent = bpmIntent(124.0f);
    REQUIRE(history.pushAtomic(c));

    std::vector<float> applied{};
    auto applyFn = [&applied](const UiIntent& it) {
        applied.push_back(it.value);
        return true;
    };

    REQUIRE(history.undo(applyFn));
    REQUIRE(applied == std::vector<float>{100.0f});

    REQUIRE(history.redo(applyFn));
    REQUIRE(applied == std::vector<float>{100.0f, 124.0f});
}

TEST_CASE("HistoryTransactionManager: transaction stores batch and keeps order") {
    HistoryTransactionManager history(4);
    REQUIRE(history.begin());

    HistoryTransactionManager::Change c1{};
    c1.undoIntent = bpmIntent(90.0f);
    c1.redoIntent = bpmIntent(110.0f);
    HistoryTransactionManager::Change c2{};
    c2.undoIntent = bpmIntent(80.0f);
    c2.redoIntent = bpmIntent(120.0f);
    REQUIRE(history.record(c1));
    REQUIRE(history.record(c2));
    REQUIRE(history.commit());

    std::vector<float> applied{};
    auto applyFn = [&applied](const UiIntent& it) {
        applied.push_back(it.value);
        return true;
    };

    REQUIRE(history.undo(applyFn));
    REQUIRE(applied == std::vector<float>{80.0f, 90.0f});

    REQUIRE(history.redo(applyFn));
    REQUIRE(applied == std::vector<float>{80.0f, 90.0f, 110.0f, 120.0f});
}

TEST_CASE("HistoryTransactionManager: new action clears redo and depth is limited") {
    HistoryTransactionManager history(2);

    HistoryTransactionManager::Change c1{};
    c1.undoIntent = bpmIntent(100.0f);
    c1.redoIntent = bpmIntent(101.0f);
    HistoryTransactionManager::Change c2{};
    c2.undoIntent = bpmIntent(101.0f);
    c2.redoIntent = bpmIntent(102.0f);
    HistoryTransactionManager::Change c3{};
    c3.undoIntent = bpmIntent(102.0f);
    c3.redoIntent = bpmIntent(103.0f);

    REQUIRE(history.pushAtomic(c1));
    REQUIRE(history.pushAtomic(c2));
    REQUIRE(history.pushAtomic(c3));
    REQUIRE(history.undoSize() == 2);

    std::vector<float> applied{};
    auto applyFn = [&applied](const UiIntent& it) {
        applied.push_back(it.value);
        return true;
    };

    REQUIRE(history.undo(applyFn));
    REQUIRE(history.redo(applyFn));
    REQUIRE(applied == std::vector<float>{102.0f, 103.0f});

    HistoryTransactionManager::Change c4{};
    c4.undoIntent = bpmIntent(103.0f);
    c4.redoIntent = bpmIntent(104.0f);
    REQUIRE(history.pushAtomic(c4));
    REQUIRE(history.redoSize() == 0);
    REQUIRE_FALSE(history.redo(applyFn));
}

