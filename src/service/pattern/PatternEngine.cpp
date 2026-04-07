#include "service/pattern/PatternEngine.h"

namespace avantgarde {

PatternEngine::PatternEngine(double sampleRate) noexcept
    : scheduler_(sampleRate) {}

IPatternBank& PatternEngine::bank() noexcept {
    return bank_;
}

const IPatternBank& PatternEngine::bank() const noexcept {
    return bank_;
}

PatternSnapshotManager& PatternEngine::snapshots() noexcept {
    return snapshotManager_;
}

const PatternSnapshotManager& PatternEngine::snapshots() const noexcept {
    return snapshotManager_;
}

bool PatternEngine::putPattern(const PatternState& state) {
    if (!bank_.put(state)) {
        return false;
    }
    if (!snapshotManager_.upsert(state)) {
        return false;
    }
    return true;
}

bool PatternEngine::erasePattern(PatternId id) {
    const bool erasedBank = bank_.erase(id);
    const bool erasedSnap = snapshotManager_.erase(id);
    if (activePattern_ == id) {
        activePattern_ = kInvalidPatternId;
    }
    return erasedBank || erasedSnap;
}

IPatternScheduler& PatternEngine::scheduler() noexcept {
    return scheduler_;
}

const IPatternScheduler& PatternEngine::scheduler() const noexcept {
    return scheduler_;
}

bool PatternEngine::setActivePattern(PatternId id) noexcept {
    if (id == kInvalidPatternId) {
        // Разрешаем явный сброс активного паттерна.
        activePattern_ = kInvalidPatternId;
        return true;
    }
    if (!bank_.contains(id)) {
        // Нельзя активировать паттерн, которого нет в банке.
        return false;
    }
    activePattern_ = id;
    return true;
}

PatternId PatternEngine::activePatternId() const noexcept {
    return activePattern_;
}

void PatternEngine::requestSwitch(PatternId target, QuantizeMode quantize) noexcept {
    scheduler_.requestSwitch(PatternSwitchRequest{
        .target = target,
        .quantize = quantize
    });
}

void PatternEngine::onTransportRt(const TransportRtSnapshot& transport) noexcept {
    scheduler_.onTransport(transport);
}

bool PatternEngine::popReadySwitch(PatternId& outPatternId) noexcept {
    CompiledSwitchPlan plan{};
    if (!popReadySwitchPlan(plan)) {
        return false;
    }
    outPatternId = plan.to;
    return true;
}

bool PatternEngine::popReadySwitchPlan(CompiledSwitchPlan& outPlan) noexcept {
    PatternId ready = kInvalidPatternId;
    if (!scheduler_.popReadySwitch(ready)) {
        return false;
    }
    if (!bank_.contains(ready)) {
        // Scheduler мог вернуть id, удаленный из банка после requestSwitch().
        return false;
    }

    CompiledSwitchPlan plan{};
    if (!snapshotManager_.buildSwitchPlan(activePattern_, ready, plan)) {
        // Если нет compiled snapshot для target — switch не публикуем.
        return false;
    }

    // Switch считается принятым только после успешной сборки apply-плана.
    activePattern_ = ready;
    outPlan = std::move(plan);
    return true;
}

bool PatternEngine::buildSwitchPlanTo(PatternId target, CompiledSwitchPlan& outPlan) noexcept {
    if (!bank_.contains(target)) {
        return false;
    }
    CompiledSwitchPlan plan{};
    if (!snapshotManager_.buildSwitchPlan(activePattern_, target, plan)) {
        return false;
    }
    activePattern_ = target;
    outPlan = std::move(plan);
    return true;
}

} // namespace avantgarde
