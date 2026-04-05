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
    AnimSlot,
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
    UiLayoutNodeType type{UiLayoutNodeType::Unknown};
    std::string id{};
    std::string text{};
    std::string label{};
    // Роль/источник шрифта для рендереров, которые поддерживают типографику.
    // Поддерживаемые значения:
    // - "default" / "body" / "gothic"
    // - PostScript имя шрифта
    // - путь к font-файлу (.ttf/.otf/.ttc/.otc), например "assets/fonts/my.ttf".
    std::string font{};
    // Размер шрифта ноды в pt. 0 = размер по умолчанию рендерера/роли.
    float fontSize{0.0f};
    // Визуальный эффект ноды (опционально), например "glitch".
    std::string effect{};
    // Триггер визуального эффекта:
    // - ""/"always"/"time"  -> тайм-режим (случайные вспышки по интервалу)
    // - "change"            -> включается при изменении значения (например knob)
    std::string effectTrigger{};
    // Таймаут "отпускания" для trigger=change (мс):
    // эффект остается активным после последнего изменения значения.
    // 0 = default рендерера/FX.
    uint32_t effectTriggerOutMs{0};
    // Период эффекта в миллисекундах (если 0, рендерер использует свой default).
    uint16_t effectIntervalMs{0};
    // Интенсивность эффекта [0..1] (если 0, рендерер использует мягкий default).
    float effectAmount{0.0f};
    // Скорость микродвижения внутри эффекта (1.0 = default, >1 быстрее, <1 медленнее).
    float effectSpeed{1.0f};
    std::string bind{};
    // Масштаб крутилки (только для нод type="knob"):
    // 1.0 = дефолтный размер, <1 уменьшает, >1 увеличивает.
    // Рендерер дополнительно ограничивает размер рамками layout-ячейки.
    float knobSize{1.0f};
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
        case UiLayoutNodeType::AnimSlot: return "anim_slot";
        case UiLayoutNodeType::Spacer: return "spacer";
        case UiLayoutNodeType::Unknown:
        default:
            return "unknown";
    }
}

// Мини-парсер строкового имени узла (для загрузчиков TOML/JSON и т.д.).
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
    if (raw == "anim_slot") return UiLayoutNodeType::AnimSlot;
    if (raw == "spacer") return UiLayoutNodeType::Spacer;
    return UiLayoutNodeType::Unknown;
}

} // namespace avantgarde
