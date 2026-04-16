#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avantgarde {

// Тип узла декларативного UI-лейаута.
enum class UiLayoutNodeType : uint8_t {
    Unknown = 0,
    Column,
    Row,
    StatusBar,
    Text,
    TrackView,
    ManagerView,
    FxListView,
    FxEditorView,
    List,
    Separator,
    Knob,
    Switch,
    Icon,
    AnimSlot,
    Waveform,
    Spacer
};

// Размер в декларативном лейауте:
// - Auto: размер определяется контентом/родителем.
// - Px: фиксированный размер в условных пикселях/ячейках.
// - Percent: процент от доступного размера родителя.
struct UiLayoutSize {
    enum class Unit : uint8_t {
        Auto = 0,
        Px,
        Percent
    };

    Unit unit{Unit::Auto};
    float value{0.0f};
};

// Выравнивание элементов по основной оси контейнера (row/column).
enum class UiLayoutJustify : uint8_t {
    Start = 0,
    Center,
    End,
    SpaceBetween
};

// Выравнивание элементов по поперечной оси контейнера (row/column)
// и для отдельных узлов, где важно позиционирование внутри выделенного rect.
enum class UiLayoutAlign : uint8_t {
    Start = 0,
    Center,
    End
};

// Узел дерева декларативной разметки.
// Пока это чистый DTO-контракт: хранит структуру и метаданные без логики рендера.
struct UiLayoutNode {
    // Описание одного визуального эффекта для UI-ноды.
    struct EffectSpec {
        // Тип эффекта, например "glitch", "glow", "typing", "color_filter".
        std::string type{};
        // Цвет эффекта в hex-формате, например "#A86DB5" (опционально).
        std::string effectColor{};
        // Режим триггера: ""/"always"/"time"/"change".
        std::string effectTrigger{};
        // Режим перехода эффекта: ""/"crumble"/"instant"/"none".
        std::string effectTransition{};
        // Таймаут "release" для trigger=change (мс).
        uint32_t effectTriggerOutMs{0};
        // Интервал time-trigger в миллисекундах.
        uint16_t effectIntervalMs{0};
        // Интенсивность [0..1].
        float effectAmount{0.0f};
        // Скорость анимации эффекта.
        float effectSpeed{1.0f};
    };

    UiLayoutNodeType type{UiLayoutNodeType::Unknown};
    std::string id{};
    std::string text{};
    std::string label{};
    // Для type="icon": путь к изображению.
    // Допускаются:
    // - абсолютный путь;
    // - путь относительно cwd процесса;
    // - короткий вид "images/foo.png" -> "assets/images/foo.png".
    std::string assetPath{};
    // Цвет текста ноды в hex-формате (например "#D6D1E6").
    std::string textColor{};
    // Цвет границы/линии ноды в hex-формате.
    std::string borderColor{};
    // Цвет фоновой заливки ноды в hex-формате.
    std::string backgroundColor{};
    // Цвет линии playhead для компонента waveform (hex-формат).
    // Может задаваться на самой waveform-ноде или на родительском контейнере.
    std::string playheadColor{};
    // Дефолтный цвет текста для дочерних элементов (обычно для root-контейнера).
    std::string defaultTextColor{};
    // Роль/источник шрифта для рендереров, которые поддерживают типографику.
    // Поддерживаемые значения:
    // - "default" / "body" / "gothic"
    // - PostScript имя шрифта
    // - путь к font-файлу (.ttf/.otf/.ttc/.otc), например "assets/fonts/my.ttf".
    std::string font{};
    // Размер шрифта ноды в pt. 0 = размер по умолчанию рендерера/роли.
    float fontSize{0.0f};
    // Новый контракт: цепочка эффектов с параметрами на каждый эффект.
    std::vector<EffectSpec> effects{};
    // Переопределения для конкретного визуального состояния.
    // Внутри state-блока можно задать условие перехода в состояние и
    // локальные style-override поля. Если поле пустое/нулевое, берется
    // базовое значение ноды.
    struct StateSpec {
        // Условие, при котором нода попадает в данное состояние.
        // Пример: "track.selected.fx.enabled", "!target.active".
        // Для active пустое условие означает "использовать по умолчанию".
        std::string ifExpr{};
        // Множитель прозрачности состояния [0..1].
        float opacity{1.0f};
        // Эффекты именно для этого состояния.
        // Если пусто — рендерер использует node.effects.
        std::vector<EffectSpec> effects{};
        // Локальные style-override поля.
        std::string textColor{};
        std::string borderColor{};
        std::string backgroundColor{};
        std::string playheadColor{};
        std::string font{};
        float fontSize{0.0f};
        float knobSize{0.0f};

        StateSpec() = default;
        explicit StateSpec(float opacityDefault) : opacity(opacityDefault) {}
    };

    // bind — источник значения для отображения (read path).
    // Пример: "track.selected.speed", "status.transport", "fx.anim.current".
    std::string bind{};
    // target — цель изменения значения (write path).
    // По умолчанию пустой: тогда интент изменения берется из bind/контекста виджета.
    // Пример: "param.track.selected.speed", "param.fx.selected.0".
    std::string target{};
    // Условие видимости ноды.
    // Если условие false, компонент остается в prepared-layout, но не рендерится.
    // Пример: "track.selected.exists", "!fx.selected.exists", "target.active".
    std::string visibleIf{};
    // Базовая прозрачность ноды [0..1].
    float opacity{1.0f};
    // Декларация поведения/стилей по состояниям.
    // active / inactive / disabled заменяют legacy-поля:
    // enabled_if, active_if, active_opacity, inactive_opacity, disabled_opacity,
    // effects_active, effects_inactive, effects_disabled.
    StateSpec active{1.0f};
    StateSpec inactive{0.45f};
    StateSpec disabled{0.28f};
    // Масштаб крутилки (только для нод type="knob"):
    // 1.0 = дефолтный размер, <1 уменьшает, >1 увеличивает.
    // Рендерер дополнительно ограничивает размер рамками layout-ячейки.
    float knobSize{1.0f};
    // Настройки кадровой анимации для type="anim_slot".
    // mode:
    // - "loop"  -> кадры листаются по времени с частотой animFps.
    // - "scrub" -> кадр выбирается по intensity01 из bind (0..1).
    std::string animMode{"loop"};
    // Частота проигрывания кадров для loop-режима.
    float animFps{8.0f};
    // Список кадров анимации (пути до PNG/изображений).
    std::vector<std::string> animFrames{};
    // Рисовать рамку вокруг anim-slot.
    bool animShowFrame{true};
    // Толщина рамки anim-slot.
    float animFrameWidth{1.1f};
    // Радиус скругления рамки anim-slot.
    float animFrameRadius{4.0f};
    // Для нод типа switch: список дискретных значений (порядок важен).
    std::vector<std::string> options{};
    UiLayoutSize width{};
    UiLayoutSize height{};
    // Только для row: разрешить перенос дочерних элементов на новую строку.
    bool wrap{true};
    // Только для контейнеров row/column: выравнивание дочерних элементов по основной оси.
    UiLayoutJustify justify{UiLayoutJustify::Start};
    // Для row/column: выравнивание дочерних элементов по поперечной оси.
    // Для leaf-нод может использоваться рендерером как подсказка позиционирования.
    UiLayoutAlign align{UiLayoutAlign::Start};
    // Для text/statusbar: разрешить перенос строк внутри выделенного прямоугольника.
    bool textWrap{false};
    // Внешний отступ элемента (space вокруг ноды, влияет на раскладку соседей).
    uint16_t margin{0};
    // Внутренний отступ элемента (space внутри ноды, влияет на контент).
    uint16_t padding{0};
    uint16_t gap{0};
    std::vector<UiLayoutNode> children{};
};

// Корневой шаблон виджета (например "fx_editor").
struct UiLayoutTemplate {
    std::string widgetId{};
    UiLayoutNode root{};

    // Пройти по всем нодам layout-дерева в DFS-порядке (root -> children...).
    // Удобно для декларативной сборки компонентов без ручного collectNodes.
    template <typename Fn>
    void forEachNode(Fn&& fn) const {
        auto&& fnRef = fn;
        forEachNodeImpl_(root, fnRef);
    }

private:
    template <typename Fn>
    static void forEachNodeImpl_(const UiLayoutNode& node, Fn& fn) {
        fn(node);
        for (const UiLayoutNode& child : node.children) {
            forEachNodeImpl_(child, fn);
        }
    }
};

inline const char* toString(UiLayoutNodeType type) noexcept {
    switch (type) {
        case UiLayoutNodeType::Column: return "column";
        case UiLayoutNodeType::Row: return "row";
        case UiLayoutNodeType::StatusBar: return "statusbar";
        case UiLayoutNodeType::Text: return "text";
        case UiLayoutNodeType::TrackView: return "track_view";
        case UiLayoutNodeType::ManagerView: return "manager_view";
        case UiLayoutNodeType::FxListView: return "fx_list_view";
        case UiLayoutNodeType::FxEditorView: return "fx_editor_view";
        case UiLayoutNodeType::List: return "list";
        case UiLayoutNodeType::Separator: return "separator";
        case UiLayoutNodeType::Knob: return "knob";
        case UiLayoutNodeType::Switch: return "switch";
        case UiLayoutNodeType::Icon: return "icon";
        case UiLayoutNodeType::AnimSlot: return "anim_slot";
        case UiLayoutNodeType::Waveform: return "waveform";
        case UiLayoutNodeType::Spacer: return "spacer";
        case UiLayoutNodeType::Unknown:
        default:
            return "unknown";
    }
}

// Мини-парсер строкового имени узла (для JSON-загрузчика и runtime-утилит).
inline UiLayoutNodeType parseUiLayoutNodeType(std::string_view raw) noexcept {
    if (raw == "column") return UiLayoutNodeType::Column;
    if (raw == "row") return UiLayoutNodeType::Row;
    if (raw == "statusbar") return UiLayoutNodeType::StatusBar;
    if (raw == "text") return UiLayoutNodeType::Text;
    if (raw == "track_view") return UiLayoutNodeType::TrackView;
    if (raw == "manager_view") return UiLayoutNodeType::ManagerView;
    if (raw == "fx_list_view") return UiLayoutNodeType::FxListView;
    if (raw == "fx_editor_view") return UiLayoutNodeType::FxEditorView;
    if (raw == "list") return UiLayoutNodeType::List;
    if (raw == "separator") return UiLayoutNodeType::Separator;
    if (raw == "knob") return UiLayoutNodeType::Knob;
    if (raw == "switch") return UiLayoutNodeType::Switch;
    if (raw == "icon") return UiLayoutNodeType::Icon;
    if (raw == "anim_slot") return UiLayoutNodeType::AnimSlot;
    if (raw == "waveform") return UiLayoutNodeType::Waveform;
    if (raw == "spacer") return UiLayoutNodeType::Spacer;
    return UiLayoutNodeType::Unknown;
}

} // namespace avantgarde
