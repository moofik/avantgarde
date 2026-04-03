#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avantgarde {

// Тип UI-компонента в prepared-layout пайплайне.
enum class UiComponentType : uint8_t {
    StatusBar = 0,
    Text,
    TrackView,
    ManagerView,
    FxListView,
    FxEditorView,
    Knob,
    Switch,
    AnimSlot,
    List,
    Separator,
};

// Базовый контракт любого UI-компонента.
// Компонент хранит только данные кадра и не содержит логики отрисовки.
class IUiComponent {
public:
    virtual ~IUiComponent() = default;
    virtual UiComponentType type() const noexcept = 0;
    virtual std::string_view id() const noexcept = 0;
};

// Статусная строка в верхней/нижней части layout.
class UiStatusBarComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::string text{};

    UiComponentType type() const noexcept override { return UiComponentType::StatusBar; }
    std::string_view id() const noexcept override { return idValue; }
};

// Произвольный текстовый компонент.
class UiTextComponent final : public IUiComponent {
public:
    enum class Align : uint8_t {
        Left = 0,
        Center,
        Right,
    };

    std::string idValue{};
    std::string text{};
    Align align{Align::Left};

    UiComponentType type() const noexcept override { return UiComponentType::Text; }
    std::string_view id() const noexcept override { return idValue; }
};

// Компонент "крутилка" (логическая модель, без геометрии).
class UiKnobComponent final : public IUiComponent {
public:
    enum class Scale : uint8_t {
        Linear = 0,
        Log,
    };

    std::string idValue{};
    std::string label{};
    float value01{0.0f};
    bool selected{false};
    Scale scale{Scale::Linear};

    UiComponentType type() const noexcept override { return UiComponentType::Knob; }
    std::string_view id() const noexcept override { return idValue; }
};

// Компонент "переключатель" для дискретных режимов.
class UiSwitchComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::string label{};
    std::vector<std::string> options{};
    uint16_t selectedIndex{0};
    bool selected{false};

    UiComponentType type() const noexcept override { return UiComponentType::Switch; }
    std::string_view id() const noexcept override { return idValue; }
};

// Слот анимации (например для FX-визуализации).
class UiAnimSlotComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::string label{};
    std::string animKey{};
    float intensity01{0.0f};

    UiComponentType type() const noexcept override { return UiComponentType::AnimSlot; }
    std::string_view id() const noexcept override { return idValue; }
};

// Список строк (например файловый менеджер или FX-chain list).
class UiListComponent final : public IUiComponent {
public:
    enum class Marker : uint8_t {
        None = 0,
        Arrow,
        Dot,
    };

    std::string idValue{};
    std::vector<std::string> rows{};
    int32_t selectedRow{-1};
    Marker marker{Marker::Arrow};

    UiComponentType type() const noexcept override { return UiComponentType::List; }
    std::string_view id() const noexcept override { return idValue; }
};

// Разделитель (горизонтальная линия).
class UiSeparatorComponent final : public IUiComponent {
public:
    enum class Style : uint8_t {
        Light = 0,
        Heavy,
    };

    std::string idValue{};
    Style style{Style::Light};

    UiComponentType type() const noexcept override { return UiComponentType::Separator; }
    std::string_view id() const noexcept override { return idValue; }
};

// Универсальный слот функционального компонента.
// В слот можно положить любые дочерние UiComponent-элементы.
struct UiComponentSlot {
    std::string id{};
    std::vector<std::unique_ptr<IUiComponent>> components{};
};

// Функциональный компонент экрана трека.
// Компонент не содержит отрисовочной логики и не хардкодит типы контента:
// любые вложенные элементы живут в слотах.
class UiTrackViewComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::vector<UiComponentSlot> slots{};

    UiComponentType type() const noexcept override { return UiComponentType::TrackView; }
    std::string_view id() const noexcept override { return idValue; }
};

// Функциональный компонент файлового менеджера.
class UiManagerViewComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::vector<UiComponentSlot> slots{};

    UiComponentType type() const noexcept override { return UiComponentType::ManagerView; }
    std::string_view id() const noexcept override { return idValue; }
};

// Функциональный компонент экрана списка FX.
class UiFxListViewComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::vector<UiComponentSlot> slots{};

    UiComponentType type() const noexcept override { return UiComponentType::FxListView; }
    std::string_view id() const noexcept override { return idValue; }
};

// Функциональный компонент экрана редактора FX.
// Базовый кейс для fallback-render: контейнер со слотами для любых компонентов.
class UiFxEditorViewComponent final : public IUiComponent {
public:
    std::string idValue{};
    std::vector<UiComponentSlot> slots{};

    UiComponentType type() const noexcept override { return UiComponentType::FxEditorView; }
    std::string_view id() const noexcept override { return idValue; }
};

// Builder: UiStatusBarComponent.
class UiStatusBarBuilder final {
public:
    explicit UiStatusBarBuilder(std::string id) { component_.idValue = std::move(id); }

    UiStatusBarBuilder& text(std::string value) {
        component_.text = std::move(value);
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiStatusBarComponent>(std::move(component_));
    }

private:
    UiStatusBarComponent component_{};
};

// Builder: UiTextComponent.
class UiTextBuilder final {
public:
    using Align = UiTextComponent::Align;

    explicit UiTextBuilder(std::string id) { component_.idValue = std::move(id); }

    UiTextBuilder& text(std::string value) {
        component_.text = std::move(value);
        return *this;
    }

    UiTextBuilder& align(Align value) {
        component_.align = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiTextComponent>(std::move(component_));
    }

private:
    UiTextComponent component_{};
};

// Builder: UiKnobComponent.
class UiKnobBuilder final {
public:
    using Scale = UiKnobComponent::Scale;

    explicit UiKnobBuilder(std::string id) { component_.idValue = std::move(id); }

    UiKnobBuilder& label(std::string value) {
        component_.label = std::move(value);
        return *this;
    }

    UiKnobBuilder& value01(float value) {
        component_.value01 = value;
        return *this;
    }

    UiKnobBuilder& selected(bool value) {
        component_.selected = value;
        return *this;
    }

    UiKnobBuilder& scale(Scale value) {
        component_.scale = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiKnobComponent>(std::move(component_));
    }

private:
    UiKnobComponent component_{};
};

// Builder: UiSwitchComponent.
class UiSwitchBuilder final {
public:
    explicit UiSwitchBuilder(std::string id) { component_.idValue = std::move(id); }

    UiSwitchBuilder& label(std::string value) {
        component_.label = std::move(value);
        return *this;
    }

    UiSwitchBuilder& options(std::vector<std::string> value) {
        component_.options = std::move(value);
        return *this;
    }

    UiSwitchBuilder& selectedIndex(uint16_t value) {
        component_.selectedIndex = value;
        return *this;
    }

    UiSwitchBuilder& selected(bool value) {
        component_.selected = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiSwitchComponent>(std::move(component_));
    }

private:
    UiSwitchComponent component_{};
};

// Builder: UiAnimSlotComponent.
class UiAnimSlotBuilder final {
public:
    explicit UiAnimSlotBuilder(std::string id) { component_.idValue = std::move(id); }

    UiAnimSlotBuilder& label(std::string value) {
        component_.label = std::move(value);
        return *this;
    }

    UiAnimSlotBuilder& animKey(std::string value) {
        component_.animKey = std::move(value);
        return *this;
    }

    UiAnimSlotBuilder& intensity01(float value) {
        component_.intensity01 = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiAnimSlotComponent>(std::move(component_));
    }

private:
    UiAnimSlotComponent component_{};
};

// Builder: UiListComponent.
class UiListBuilder final {
public:
    using Marker = UiListComponent::Marker;

    explicit UiListBuilder(std::string id) { component_.idValue = std::move(id); }

    UiListBuilder& addRow(std::string row) {
        component_.rows.push_back(std::move(row));
        return *this;
    }

    UiListBuilder& rows(std::vector<std::string> values) {
        component_.rows = std::move(values);
        return *this;
    }

    UiListBuilder& selectedRow(int32_t value) {
        component_.selectedRow = value;
        return *this;
    }

    UiListBuilder& marker(Marker value) {
        component_.marker = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiListComponent>(std::move(component_));
    }

private:
    UiListComponent component_{};
};

// Builder: UiSeparatorComponent.
class UiSeparatorBuilder final {
public:
    using Style = UiSeparatorComponent::Style;

    explicit UiSeparatorBuilder(std::string id) { component_.idValue = std::move(id); }

    UiSeparatorBuilder& style(Style value) {
        component_.style = value;
        return *this;
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiSeparatorComponent>(std::move(component_));
    }

private:
    UiSeparatorComponent component_{};
};

// Builder: UiTrackViewComponent.
class UiTrackViewBuilder final {
public:
    explicit UiTrackViewBuilder(std::string id) { component_.idValue = std::move(id); }

    UiTrackViewBuilder& addToSlot(std::string slotId, std::unique_ptr<IUiComponent> child) {
        if (!child) {
            return *this;
        }
        UiComponentSlot& slot = ensureSlot_(slotId);
        slot.components.push_back(std::move(child));
        return *this;
    }

    template <typename TBuilder>
    UiTrackViewBuilder& addToSlot(std::string slotId, TBuilder builder) {
        return addToSlot(std::move(slotId), std::move(builder).build());
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiTrackViewComponent>(std::move(component_));
    }

private:
    UiComponentSlot& ensureSlot_(const std::string& slotId) {
        for (UiComponentSlot& slot : component_.slots) {
            if (slot.id == slotId) {
                return slot;
            }
        }
        component_.slots.push_back(UiComponentSlot{});
        component_.slots.back().id = slotId;
        return component_.slots.back();
    }

    UiTrackViewComponent component_{};
};

// Builder: UiManagerViewComponent.
class UiManagerViewBuilder final {
public:
    explicit UiManagerViewBuilder(std::string id) { component_.idValue = std::move(id); }

    UiManagerViewBuilder& addToSlot(std::string slotId, std::unique_ptr<IUiComponent> child) {
        if (!child) {
            return *this;
        }
        UiComponentSlot& slot = ensureSlot_(slotId);
        slot.components.push_back(std::move(child));
        return *this;
    }

    template <typename TBuilder>
    UiManagerViewBuilder& addToSlot(std::string slotId, TBuilder builder) {
        return addToSlot(std::move(slotId), std::move(builder).build());
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiManagerViewComponent>(std::move(component_));
    }

private:
    UiComponentSlot& ensureSlot_(const std::string& slotId) {
        for (UiComponentSlot& slot : component_.slots) {
            if (slot.id == slotId) {
                return slot;
            }
        }
        component_.slots.push_back(UiComponentSlot{});
        component_.slots.back().id = slotId;
        return component_.slots.back();
    }

    UiManagerViewComponent component_{};
};

// Builder: UiFxListViewComponent.
class UiFxListViewBuilder final {
public:
    explicit UiFxListViewBuilder(std::string id) { component_.idValue = std::move(id); }

    UiFxListViewBuilder& addToSlot(std::string slotId, std::unique_ptr<IUiComponent> child) {
        if (!child) {
            return *this;
        }
        UiComponentSlot& slot = ensureSlot_(slotId);
        slot.components.push_back(std::move(child));
        return *this;
    }

    template <typename TBuilder>
    UiFxListViewBuilder& addToSlot(std::string slotId, TBuilder builder) {
        return addToSlot(std::move(slotId), std::move(builder).build());
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiFxListViewComponent>(std::move(component_));
    }

private:
    UiComponentSlot& ensureSlot_(const std::string& slotId) {
        for (UiComponentSlot& slot : component_.slots) {
            if (slot.id == slotId) {
                return slot;
            }
        }
        component_.slots.push_back(UiComponentSlot{});
        component_.slots.back().id = slotId;
        return component_.slots.back();
    }

    UiFxListViewComponent component_{};
};

// Builder: UiFxEditorViewComponent.
class UiFxEditorViewBuilder final {
public:
    explicit UiFxEditorViewBuilder(std::string id) { component_.idValue = std::move(id); }

    UiFxEditorViewBuilder& addToSlot(std::string slotId, std::unique_ptr<IUiComponent> child) {
        if (!child) {
            return *this;
        }
        UiComponentSlot& slot = ensureSlot_(slotId);
        slot.components.push_back(std::move(child));
        return *this;
    }

    template <typename TBuilder>
    UiFxEditorViewBuilder& addToSlot(std::string slotId, TBuilder builder) {
        return addToSlot(std::move(slotId), std::move(builder).build());
    }

    std::unique_ptr<IUiComponent> build() && {
        return std::make_unique<UiFxEditorViewComponent>(std::move(component_));
    }

private:
    UiComponentSlot& ensureSlot_(const std::string& slotId) {
        for (UiComponentSlot& slot : component_.slots) {
            if (slot.id == slotId) {
                return slot;
            }
        }
        component_.slots.push_back(UiComponentSlot{});
        component_.slots.back().id = slotId;
        return component_.slots.back();
    }

    UiFxEditorViewComponent component_{};
};

} // namespace avantgarde
