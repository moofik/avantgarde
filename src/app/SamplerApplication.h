#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "app/SamplerEngineLayer.h"
#include "app/HistoryTransactionManager.h"
#include "app/SamplerIoLayer.h"
#include "app/SnapshotIntentOrchestrator.h"
#include "app/UiIntentApplier.h"
#include "contracts/IPlatform.h"
#include "contracts/UiIntent.h"
#include "service/UiStateComposer.h"
#include "service/UiStateStore.h"
#include "service/sequencer/AutomationLane.h"
#include "service/sequencer/EventLane.h"
#include "service/sequencer/SmoothedValue.h"
#include "service/ui/input/UiInputInterpreter.h"
#include "service/ui/hud/HudNotificationsLayer.h"
#include "service/ui/UiSceneHost.h"

namespace avantgarde {

struct SequencerDispatchItem;

// Конфигурация верхнего уровня для запуска приложения.
struct SamplerAppConfig {
    struct StartupClipLoad {
        // Индекс трека, в который нужно загрузить файл.
        uint8_t track{0};
        // Путь к аудиофайлу.
        std::string path{};
    };

    // Параметры аудио/движка.
    SamplerEngineConfig engine{};
    // Платформенный аудиохост (инжекция зависимости в engine-слой).
    std::shared_ptr<IAudioHost> audioHost{};
    // Параметры слоя ввода/вывода.
    SamplerIoConfig io{};
    // Стартовые загрузки клипов (произвольный список пар track/path).
    std::vector<StartupClipLoad> startupClipLoads{};
};

// Оркестратор приложения.
// Слой связывает IO и Engine через UI intents/state,
// чтобы они не зависели друг от друга напрямую.
class SamplerApplication {
public:
    SamplerApplication() = default;
    ~SamplerApplication();

    // Полный жизненный цикл приложения:
    // init -> start -> event loop -> stop.
    int run(const SamplerAppConfig& config);

private:
    // Ограничение UI-индекса трека в диапазон [0..N-1].
    uint8_t clampUiTrack_(uint8_t track) const noexcept;

    // Применить intent виджета через ActionApplier и корректно записать в историю.
    bool dispatchWidgetIntent_(const UiIntent& intent);
    // Применить один dispatch item секвенсора в движок.
    bool applySequencerItem_(const SequencerDispatchItem& item);
    // Привязка UiIntent -> lane (automation/event) в глобальном REC-режиме.
    void recordIntentToSequencer_(const UiIntent& intent, uint64_t sampleTime);
    // Обработка playback lane-ов в диапазоне transport sampleTime.
    bool processSequencerPlayback_();
    // Обработка sequencer-intent-ов (lane/edit/navigation), минуя UiIntentApplier.
    bool applySequencerIntent_(const UiIntent& intent);
    // Переключить глобальный режим записи и синхронизировать его в UI state.
    void setRecordEnabled_(bool enabled);
    // Обновить mirror параметров после успешного применения intent.
    void updateIntentMirrors_(const UiIntent& intent);
    // Текущий активный PatternId для sequencer map.
    PatternId activePatternId_() const noexcept;
    // Доступ к per-pattern sequencer runtime (создает запись при отсутствии).
    struct SequencerPatternData;
    SequencerPatternData& ensureSequencerPattern_(PatternId id);
    // Read-only доступ к per-pattern sequencer runtime.
    const SequencerPatternData* findSequencerPattern_(PatternId id) const noexcept;
    // Текущий активный per-pattern sequencer runtime.
    SequencerPatternData& currentSequencerPattern_();
    const SequencerPatternData& currentSequencerPattern_() const;
    // Пересчитать UiSequencerState из текущего per-pattern runtime + nav state.
    void syncSequencerStateToUi_(UiState& merged) const;
    // Канонический ключ для SequencerParamTarget (track/slot/module/param).
    static uint64_t makeSequencerTargetKey_(const SequencerParamTarget& target) noexcept;
    // Применить значение в target (track/fx param) и обновить param-mirror.
    bool applySequencerParamTarget_(const SequencerParamTarget& target, float value);
    // Запланировать reset affected automation-targets при wrap паттерна.
    void schedulePatternLoopReset_(const SequencerPatternData& seq, uint64_t nowSample);
    // Выполнить порцию pending loop-reset апдейтов (chunked + skip micro-delta).
    bool processPendingLoopResets_(uint64_t nowSample);
    // Служебный ключ mirror для FX-параметров.
    static uint64_t makeFxParamMirrorKey_(uint8_t track, uint8_t fxSlot, uint16_t paramIndex) noexcept;
    // Удалить mirror параметров для всех FX выбранного трека.
    void clearFxParamMirrorTrack_(uint8_t track);
    // Пересчитать отображаемое состояние одного трека из transport + mute/clip данных.
    void refreshTrackViewState_(uint8_t track) noexcept;
    // Пересчитать состояния всех треков.
    void refreshAllTrackViewStates_() noexcept;
    // Один UI render-pass: telemetry + scene render + backend draw.
    void renderUiOnce_();
    // Обработка одного UI-жеста (клавиша/энкодер/кнопка).
    bool handleGesture_(const UiGestureEvent& ev);
    // Обновить pattern-состояние в UiStateStore из engine-слоя.
    void syncPatternStateToUi_();

    // Аудио/RT слой.
    SamplerEngineLayer engine_{};
    // Ввод/рендер слой.
    SamplerIoLayer io_{};
    // Интерпретатор сырого ввода (Down/Up/Repeat -> UiGesture tap/hold/repeat).
    UiInputInterpreter inputInterpreter_{};

    // Потокобезопасное состояние для UI.
    UiStateStore uiStore_{};
    // Слияние runtime telemetry в UI state snapshot.
    UiStateComposer uiComposer_{};
    // Роутер сцен и виджетов.
    UiSceneHost sceneHost_{};
    // Глобальный слой HUD-уведомлений (очередь + lifecycle + hud.json конфиг).
    HudNotificationsLayer hudLayer_{};
    // Оркестратор snapshot-intent сценариев:
    // policy + batch-apply + side-intent dispatch через UiIntentApplier.
    SnapshotIntentOrchestrator snapshotOrchestrator_{};

    // Кэш track state на control-уровне.
    std::vector<UiTrackStateView> tracksCtl_{};
    // Кэш transport state на control-уровне.
    UiTransportState trCtl_{};
    // Фактический sampleRate текущего движка (из SamplerEngineConfig).
    // Используется в секвенсоре для точной конвертации sampleTime <-> tick.
    double sampleRateHz_{48000.0};

    // Слой применения intent'ов к engine/ui-state (без знаний о ввода/сценах).
    UiIntentApplier intentApplier_{};
    // История и транзакции undo/redo.
    HistoryTransactionManager history_{4};

    // Runtime данные секвенсора для одного паттерна.
    struct SequencerPatternData {
        // Поведение параметров на границе цикла паттерна:
        // ResetOnLoop - вернуть affected параметры к base snapshot в начале нового цикла.
        // Continue - не делать reset, оставить "как есть" до следующих точек.
        enum class LoopMode : uint8_t {
            ResetOnLoop = 0,
            Continue = 1
        };
        uint16_t ppq{kSequencerPpq};
        uint32_t lengthBars{64};
        SequencerTick lengthTicks{static_cast<SequencerTick>(kSequencerPpq * 4u * 64u)};
        SequencerQuantize quant{SequencerQuantize::Quarter};
        LoopMode loopMode{LoopMode::ResetOnLoop};
        // Base snapshot для reset-on-loop: key(SequencerParamTarget) -> base value.
        std::unordered_map<uint64_t, float> baseSnapshotValues{};
        AutomationLane automation{};
        EventLane events{};
    };
    // PatternId -> набор lane-ов и параметров секвенсора.
    std::unordered_map<PatternId, SequencerPatternData> sequencerByPattern_{};
    // Активный pattern context для sequencer map.
    PatternId sequencerPatternId_{1};

    // Глобальный REC-флаг.
    bool recordEnabled_{false};
    // Защита от самозаписи во время воспроизведения lane-ов.
    bool sequencerPlaybackDispatch_{false};
    // Защита от самозаписи при snapshot-recall (ручной/по lane).
    bool snapshotRecallDispatch_{false};
    // Курсор прочитанной позиции транспорта для lane playback.
    bool sequencerCursorInitialized_{false};
    uint64_t sequencerCursorSample_{0};

    // Mirror последнего установленного значения FX-параметра:
    // key = track/slot/param, value = normalized param value.
    std::unordered_map<uint64_t, float> fxParamMirror_{};
    // Универсальный mirror параметров секвенсора (track/fx target key -> value).
    std::unordered_map<uint64_t, float> sequencerParamMirror_{};

    struct PendingLoopReset {
        SequencerParamTarget target{};
        uint64_t targetKey{0U};
        SmoothedValue smoother{};
        float lastSentValue{0.0f};
    };
    // Набор плавных reset-переходов на границе лупа.
    std::vector<PendingLoopReset> pendingLoopResets_{};

    // Синхронизация доступа к sceneHost_ между control/render потоками.
    std::mutex sceneMutex_{};
    // Глобальный флаг остановки фоновых циклов.
    std::atomic<bool> stopUi_{false};
    // Флаг "кадр устарел": render-поток рисует только при dirty=true.
    std::atomic<bool> uiDirty_{true};
    // Поток обработки input/actions.
    std::thread controlThread_{};
};

} // namespace avantgarde
