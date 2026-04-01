#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace avantgarde {

// Данные одной "крутилки" для рендера в текстовый canvas.
struct UiKnobModel {
    // Подпись под крутилкой.
    std::string label{};
    // Нормализованное значение [0..1].
    float value01{0.0f};
    // Позиция левого верхнего угла блока крутилки.
    uint16_t x{0};
    uint16_t y{0};
    // Визуальное выделение активной крутилки.
    bool selected{false};
};

// Мини-композер текстовых "крутилок":
// - рисует 2D-блоки по координатам x/y;
// - возвращает готовые строки для встраивания в frame виджета.
class UiKnobComposer final {
public:
    UiKnobComposer(uint16_t width, uint16_t height);

    // Добавить крутилку на canvas.
    void drawKnob(const UiKnobModel& model);
    // Срез готовых строк canvas.
    const std::vector<std::string>& lines() const noexcept;

private:
    static std::string formatValue_(float value01);
    static std::string fitLabel_(const std::string& label, std::size_t width);
    void put_(uint16_t x, uint16_t y, const std::string& text);

    uint16_t width_{0};
    uint16_t height_{0};
    std::vector<std::string> lines_{};
};

} // namespace avantgarde
