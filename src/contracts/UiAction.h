#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace avantgarde {

// Унифицированная сущность "экшен для UI".
// ВАЖНО:
// - наружу между слоями передается только UiAction.
// - отдельные идентификаторы/операции инкапсулированы внутри этой структуры.
struct UiAction {
    // Область действия экшена:
    // - Global: доступен независимо от текущей сцены.
    // - Scene: доступен только в конкретном виджете/сцене.
    enum class Scope : uint8_t {
        Global = 0,
        Scene
    };

    // Режим выполнения экшена:
    // - ImmediateStep: дискретное действие "сразу" (mute/arm/quant mode).
    // - ImmediateContinuous: непрерывная ручка в live-режиме (gain/speed/pitch).
    // - ApplyRequired: изменение сначала подготавливается, затем подтверждается Apply.
    // - Momentary: активен только пока кнопка удерживается.
    enum class Execution : uint8_t {
        ImmediateStep = 0,
        ImmediateContinuous,
        ApplyRequired,
        Momentary
    };

    // Тип редактируемого значения для UI-слоя.
    enum class ValueKind : uint8_t {
        None = 0,
        Bool,
        Integer,
        Float,
        Enum
    };

    // Унифицированные операции pointer-навигации/редактирования.
    enum class Op : uint8_t {
        None = 0,
        FocusPrev,
        FocusNext,
        AdjustPrev,
        AdjustNext,
        Apply,
        Undo,
        Redo,
        Press,
        Release
    };

    // Стабильные идентификаторы экшенов.
    // Можно расширять без ломки уже существующих ID.
    enum class Id : uint16_t {
        None = 0,

        // Global actions
        GlobalPlayStop = 100,
        GlobalUndo,
        GlobalBack,
        GlobalPagePrev,
        GlobalPageNext,
        GlobalMasterVolume,

        // Scene actions (tracks-first)
        SceneTrackSelect = 1000,
        SceneTrackMute,
        SceneTrackArm,
        SceneTrackSpeed,
        SceneTrackGain,
        SceneQuantize,
        SceneTempoBpm,
        SceneOpenManager
    };

    // Статическое описание экшена (что это за действие и как его исполнять).
    struct Def {
        Id id{Id::None};
        Scope scope{Scope::Scene};
        Execution execution{Execution::ImmediateStep};
        ValueKind valueKind{ValueKind::None};

        // Человеко-читаемая подпись для status/footer.
        std::string label;

        // Диапазон/шаг используются для numeric экшенов (если valueKind = Float/Integer).
        float minValue{0.0f};
        float maxValue{1.0f};
        float step{1.0f};
    };

    // Динамическое UI-состояние экшена (не заменяет engine/IParameterized source-of-truth).
    struct State {
        bool visible{true};
        bool enabled{true};
        bool selected{false};
        bool pressed{false};
        bool gestureActive{false};

        // Текущее отображаемое значение (для immediate режимов).
        float value{0.0f};
        // Отложенное значение (для ApplyRequired сценариев).
        float pendingValue{0.0f};
    };

    Def def{};
    State state{};

    // Запрошенная операция для текущего шага обработки.
    Op op{Op::None};
    // Универсальная дельта для Adjust-сценариев (если нужна виджету).
    float delta{0.0f};
};

// Набор экшенов активного контекста + индекс активного pointer.
struct UiActionCatalog {
    std::vector<UiAction> actions;
    uint16_t currentIndex{0};
};

} // namespace avantgarde
