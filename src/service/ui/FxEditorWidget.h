#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/FxRegistry.h"
#include "contracts/IUiWidget.h"
#include "contracts/UiLayout.h"

namespace avantgarde {

// Экран редактирования параметров конкретного FX-слота.
class FxEditorWidget final : public IUiWidget {
public:
    explicit FxEditorWidget(uint16_t frameWidth = 60,
                            float paramStep = 0.05f,
                            std::optional<UiLayoutTemplate> layoutTemplate = std::nullopt) noexcept;

    const char* id() const noexcept override;
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    bool buildPreparedLayout(UiPreparedLayout& out,
                             const UiState& rtState,
                             const UiNavState& navState) const override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
    struct LayoutKnob {
        // ID ноды крутилки в layout-шаблоне.
        std::string nodeId{};
        // Подпись крутилки из шаблона (или fallback-имя параметра).
        std::string label{};
        // Индекс параметра в descriptor.params.
        // -1 означает "использовать текущий выбранный параметр".
        int32_t paramIndex{-1};
        // Канонический bind после резолва.
        std::string bindCanonical{};
    };

    struct LayoutAnimSlot {
        // true, если anim-slot описан в шаблоне.
        bool enabled{false};
        // ID ноды анимационного слота в layout-шаблоне.
        std::string nodeId{"fx_anim"};
        // Канонический bind источника анимации.
        std::string bindCanonical{"fx.anim.current"};
        // Желаемый размер слота из шаблона.
        uint16_t width{0};
        uint16_t height{0};
    };

    struct LayoutModel {
        // true, если шаблон успешно загружен и применен.
        bool enabled{false};
        // ID функционального контейнера редактора в layout.
        std::string viewNodeId{"fx_view"};
        // ID текстовой строки метаданных FX внутри view-контейнера.
        std::string metaNodeId{"fx_meta"};
        // Текст заголовка statusbar из шаблона.
        std::string title{"FX EDITOR"};
        // Подсказка клавиш внизу кадра.
        std::string keysHint{" keys [j/k focus] [/? adj] [o apply] [esc] "};
        // Список крутилок, которые нужно показывать в редакторе.
        std::vector<LayoutKnob> knobs{};
        // Конфиг анимационного слота.
        LayoutAnimSlot anim{};
    };

    struct SlotCache {
        // Канонический ID FX, для которого инициализирован кэш.
        std::string fxId{};
        // Кэш текущих значений параметров в порядке descriptor.params.
        std::vector<float> values{};
    };

    // Безопасное ограничение индекса трека.
    static uint8_t clampTrack_(uint8_t track, std::size_t totalTracks) noexcept;
    // Безопасное ограничение индекса FX-слота.
    static uint16_t clampFx_(uint16_t fx, std::size_t fxCount) noexcept;
    // Безопасное ограничение индекса параметра.
    static uint16_t clampParamIndex_(uint16_t index, std::size_t paramCount) noexcept;
    // Ключ для кэша параметров (track + fxSlot).
    static uint32_t slotKey_(uint8_t track, uint16_t fxSlot) noexcept;
    // Резолв descriptor FX для выбранного track/slot.
    static const FxDescriptor* resolveDescriptor_(const UiState& rtState,
                                                  uint8_t track,
                                                  uint16_t fxSlot) noexcept;
    // Доступ к кэшу параметров слота с ленивой инициализацией.
    SlotCache& cacheFor_(uint8_t track, uint16_t fxSlot, const FxDescriptor& descriptor);
    // Доступ к кэшу в const-контексте (без создания новых записей).
    const SlotCache* cacheForConst_(uint8_t track, uint16_t fxSlot, const FxDescriptor& descriptor) const;
    // Подготовка статуса active action pointer.
    std::string buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const;
    // Построить runtime-модель из TOML-шаблона.
    void buildLayoutModel_(const UiLayoutTemplate& tpl);
    // Индекс параметра для конкретной крутилки layout-модели.
    uint16_t resolveKnobParam_(const LayoutKnob& knob,
                               uint16_t selectedParam,
                               std::size_t paramCount) const noexcept;
    // Рекурсивный DFS обход лейаута.
    static void collectNodes_(const UiLayoutNode& root,
                              std::vector<const UiLayoutNode*>& out) noexcept;

    // Рамка и шаг редактирования параметров.
    uint16_t frameWidth_{60};
    float paramStep_{0.05f};
    // Локальный UI-кэш параметров по слотам.
    // Это UI-only кэш, чтобы редактор мог показывать текущее значение после кручения.
    std::unordered_map<uint32_t, SlotCache> slotCache_{};
    // Runtime-представление TOML-шаблона.
    LayoutModel layout_{};
    // Исходный TOML template для layout-engine.
    std::optional<UiLayoutTemplate> layoutTemplate_{};
};

} // namespace avantgarde
