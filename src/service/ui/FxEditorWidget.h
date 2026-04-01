#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/FxRegistry.h"
#include "contracts/IUiWidget.h"

namespace avantgarde {

// Экран редактирования параметров конкретного FX-слота.
class FxEditorWidget final : public IUiWidget {
public:
    explicit FxEditorWidget(uint16_t frameWidth = 60, float paramStep = 0.05f) noexcept;

    const char* id() const noexcept override;
    void render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) override;
    WidgetOutput onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) override;
    UiActionCatalog queryAvailableActions(const UiState& rtState, const UiNavState& navState) const override;
    WidgetOutput onAction(UiAction& action, const UiState& rtState, UiNavState& navState) override;

private:
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

    // Рамка и шаг редактирования параметров.
    uint16_t frameWidth_{60};
    float paramStep_{0.05f};
    // Локальный UI-кэш параметров по слотам.
    // Это UI-only кэш, чтобы редактор мог показывать текущее значение после кручения.
    std::unordered_map<uint32_t, SlotCache> slotCache_{};
};

} // namespace avantgarde
