#include "service/ui/TracksWidget.h"

#include <sstream>

#include "service/ui/GbFrameComposer.h"

namespace avantgarde {

TracksWidget::TracksWidget() noexcept = default;

TracksWidget::TracksWidget(const Options& options) noexcept
    : frameWidth_(options.frameWidth),
      headerTitle_(options.headerTitle) {}

const char* TracksWidget::id() const noexcept {
    return "tracks";
}

void TracksWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    // Всегда рендерим полный кадр заново, чтобы не копить артефакты.
    out.clear();
    // Композер собирает готовый монохромный GB-кадр.
    const std::string frame = GbFrameComposer::buildMonochromeFrame(
        rtState, frameWidth_, headerTitle_, static_cast<std::size_t>(navState.trackPage));

    // Конвертируем цельный текст кадра в line-buffer виджета.
    std::istringstream in(frame);
    std::string line;
    while (std::getline(in, line)) {
        out.lines.push_back(line);
    }
}

WidgetOutput TracksWidget::onInput(UiInputAction, const UiState&, UiNavState&) {
    // У этого экрана пока нет собственной scene-local логики ввода.
    // Все действия треков обрабатываются на application/control-слое.
    return {};
}

} // namespace avantgarde
