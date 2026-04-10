#include "app/SamplerIoLayer.h"

#if defined(__APPLE__)
#include "platform/macos/MacPrimitiveWindowInput.h"
#include "platform/macos/MacPrimitiveWindowRenderer.h"
#endif

namespace avantgarde {

bool parseSamplerUiMode(std::string_view raw, SamplerUiMode& out) noexcept {
    if (raw == "gb-window" || raw == "window") {
        out = SamplerUiMode::GbWindow;
        return true;
    }
    return false;
}

SamplerIoLayer::SamplerIoLayer() = default;

SamplerIoLayer::~SamplerIoLayer() = default;

bool SamplerIoLayer::init(const SamplerIoConfig& config, std::string& errorOut) {
    const UiTheme effectiveTheme = config.themeProvided ? config.theme : UiTheme::Gothic;

#if defined(__APPLE__)
    if (config.mode == SamplerUiMode::GbWindow) {
        renderer_ = std::make_unique<MacPrimitiveWindowRenderer>(effectiveTheme);
        windowRenderer_ = dynamic_cast<MacPrimitiveWindowRenderer*>(renderer_.get());
        windowInput_ = std::make_unique<macos::MacPrimitiveWindowInput>();
        return true;
    }
#endif
    windowRenderer_ = nullptr;
    windowInput_.reset();
    (void)effectiveTheme;
    errorOut = "gb-window mode is supported only on macOS";
    return false;
}

bool SamplerIoLayer::readWindowEvents() {
#if defined(__APPLE__)
    if (!windowRenderer_ || !windowInput_) {
        return false;
    }
    windowRenderer_->pumpEvents();
    UiGestureEvent ev{};
    // Собираем все события из окна в общую очередь.
    while (windowInput_->readNextInputEvent(ev)) {
        inputQueue_.push(ev);
    }
    return true;
#else
    return false;
#endif
}

bool SamplerIoLayer::readNextInputEvent(UiGestureEvent& out) {
    return inputQueue_.tryPop(out);
}

void SamplerIoLayer::render(const UiState& state,
                            const UiPreparedLayout* preparedLayout) {
    if (!renderer_) {
        return;
    }
#if defined(__APPLE__)
    if (preparedLayout) {
        if (auto* windowRenderer = dynamic_cast<MacPrimitiveWindowRenderer*>(renderer_.get())) {
            windowRenderer->renderPreparedLayout(*preparedLayout);
            return;
        }
    }
#endif
    renderer_->render(state);
}

void SamplerIoLayer::InputEventQueue::push(const UiGestureEvent& ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Ограничиваем глубину очереди: при переполнении отбрасываем самый старый.
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
}

bool SamplerIoLayer::InputEventQueue::tryPop(UiGestureEvent& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out.action = UiGesture::None;
        out.value = 0;
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

} // namespace avantgarde
