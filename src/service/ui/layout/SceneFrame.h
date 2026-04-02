#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace avantgarde {

// Прямоугольная рамка.
// ВАЖНО: здесь нет символов/глифов.
// Это геометрический примитив, а выбор "как рисовать" — задача renderer-а.
struct SceneFrameRect {
    int16_t x{0};
    int16_t y{0};
    uint16_t width{0};
    uint16_t height{0};
};

// Горизонтальная линия.
struct SceneFrameHLine {
    int16_t x{0};
    int16_t y{0};
    uint16_t length{0};
    std::string glyph{"─"};
};

// Текстовый примитив.
struct SceneFrameText {
    int16_t x{0};
    int16_t y{0};
    std::string text{};
    // Если >0, renderer сам выполняет utf8-aware clip/pad до этой ширины.
    // Это держит логику форматирования в renderer-слое, а не в виджетах.
    uint16_t width{0};
};

// "Крутилка" как отдельный примитив будущего графического рендера.
// В fallback-ASCII рендерится упрощенно.
struct SceneFrameKnob {
    int16_t x{0};
    int16_t y{0};
    std::string label{};
    float value01{0.0f};
    bool selected{false};
};

// Слот анимации будущего графического рендера.
struct SceneFrameAnimSlot {
    int16_t x{0};
    int16_t y{0};
    uint16_t width{0};
    uint16_t height{0};
    std::string label{};
};

// Универсальный кадр сцены, который строят виджеты и читает renderer.
// Это основной транспорт между scene-слоем и backend-рендерами.
struct SceneFrame {
    uint16_t width{0};
    uint16_t height{0};

    std::vector<SceneFrameRect> rects{};
    std::vector<SceneFrameHLine> hlines{};
    std::vector<SceneFrameText> texts{};
    std::vector<SceneFrameKnob> knobs{};
    std::vector<SceneFrameAnimSlot> animSlots{};

    void clear() noexcept {
        rects.clear();
        hlines.clear();
        texts.clear();
        knobs.clear();
        animSlots.clear();
    }
};

} // namespace avantgarde
