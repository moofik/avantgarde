#include "service/UiStateStore.h"

namespace avantgarde {

UiStateStore::UiStateStore() {
    // Явно инициализируем tracks как пустой контейнер.
    // Это делает начальное состояние предсказуемым для snapshot/read-пути.
    state_.tracks.clear();
}

UiState UiStateStore::snapshot() const {
    // Короткая критическая секция: копируем целиком и сразу выходим из lock.
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void UiStateStore::setState(const UiState& state) {
    // Полная атомарная (относительно mutex) замена UI-снимка.
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
}

void UiStateStore::setTransport(const UiTransportState& transport) {
    // Точечный update для снижения boilerplate у вызывающих слоев.
    std::lock_guard<std::mutex> lock(mutex_);
    state_.transport = transport;
}

void UiStateStore::setTrack(std::size_t index, const UiTrackStateView& track) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Безопасная граница: store не расширяет tracks самовольно,
    // чтобы не скрывать ошибки жизненного цикла инициализации UiState.
    if (index >= state_.tracks.size()) {
        return;
    }
    state_.tracks[index] = track;
}

void UiStateStore::setTelemetry(const UiTelemetryState& telemetry) {
    // Telemetry обновляется часто, поэтому метод держит только минимальную логику.
    std::lock_guard<std::mutex> lock(mutex_);
    state_.telemetry = telemetry;
}

void UiStateStore::setPattern(const UiPatternState& pattern) {
    // Pattern-метаданные хранятся в том же store, чтобы рендер читал единый snapshot.
    std::lock_guard<std::mutex> lock(mutex_);
    state_.pattern = pattern;
}

} // namespace avantgarde
