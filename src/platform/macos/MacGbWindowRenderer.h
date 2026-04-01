#pragma once

#include <string>

#include "contracts/IUi.h"
#include "contracts/IUiGestureInput.h"
#include "contracts/UiTheme.h"

namespace avantgarde {

class MacGbWindowRenderer final : public IUiRenderer {
public:
    // theme/textWidth задают палитру и ожидаемую текстовую ширину GB-кадра.
    // Рендерер открывает отдельное NSWindow и держит его до деструктора.
    explicit MacGbWindowRenderer(UiTheme theme, uint16_t textWidth);
    ~MacGbWindowRenderer() override;

    // Стандартный путь рендера: строит кадр через GbFrameComposer и рисует его в NSTextView.
    void render(const UiState& state) override;
    // Fast-path для готового монокадра (когда кадр уже собран scene/widget слоем).
    void renderCustomFrame(const std::string& monoFrame, bool showHeaderOverlay);

    // Нужно вызывать в main thread, чтобы AppKit обработал очередь событий окна.
    void pumpEvents() noexcept;
    // Забирает следующее действие клавиатуры из внутренней lock-protected очереди.
    bool readNextInputEvent(UiGestureEvent& out) noexcept;

private:
    struct Impl;
    // PImpl скрывает Objective-C типы (NSWindow/NSTextView) из C++ заголовка.
    Impl* impl_{nullptr};
    // Ширина текстового кадра в символах для GbFrameComposer.
    uint16_t textWidth_{0};
};

} // namespace avantgarde
