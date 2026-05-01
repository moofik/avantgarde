#include "app/SamplerIoLayer.h"

#if defined(__APPLE__)
#include "platform/macos/MacPrimitiveWindowInput.h"
#include "platform/macos/MacPrimitiveWindowRenderer.h"
#endif
#include "platform/raspi/RpiPrimitiveWindowInput.h"
#include "platform/raspi/RpiPrimitiveWindowRenderer.h"
#include "platform/raspi/RpiUiConfig.h"

namespace avantgarde {

bool parseSamplerUiMode(std::string_view raw, SamplerUiMode& out) noexcept {
    if (raw == "gb-window" || raw == "window") {
        out = SamplerUiMode::GbWindow;
        return true;
    }
    if (raw == "rpi-wrapper" || raw == "rpi" || raw == "raspi") {
        out = SamplerUiMode::RpiWrapper;
        return true;
    }
    return false;
}

SamplerIoLayer::SamplerIoLayer() = default;

SamplerIoLayer::~SamplerIoLayer() = default;

bool SamplerIoLayer::init(const SamplerIoConfig& config, std::string& errorOut) {
    const UiTheme effectiveTheme = config.themeProvided ? config.theme : UiTheme::Gothic;
    rpiRenderer_.reset();
    rpiInput_.reset();
#if defined(__APPLE__)
    windowRenderer_ = nullptr;
    windowInput_.reset();
#endif

#if defined(__APPLE__)
    if (config.mode == SamplerUiMode::GbWindow) {
        renderer_ = std::make_unique<MacPrimitiveWindowRenderer>(effectiveTheme);
        windowRenderer_ = dynamic_cast<MacPrimitiveWindowRenderer*>(renderer_.get());
        windowInput_ = std::make_unique<macos::MacPrimitiveWindowInput>();
        return true;
    }
#endif
    if (config.mode == SamplerUiMode::RpiWrapper) {
        rpiRenderer_ = std::make_unique<raspi::RpiPrimitiveWindowRenderer>();
        rpiInput_ = std::make_unique<raspi::RpiPrimitiveWindowInput>();
        raspi::RpiUiConfig rpiCfg{};
        rpiCfg.width = 640;
        rpiCfg.height = 480;
        rpiCfg.rotateDeg = config.rpiRotateDeg;
        rpiCfg.headless = false;
        rpiCfg.inputDevice = config.rpiInputDevice;
        if (!rpiRenderer_->init(rpiCfg, errorOut)) {
            rpiRenderer_.reset();
            rpiInput_.reset();
            return false;
        }
        if (!rpiInput_->init(rpiCfg, errorOut)) {
            rpiRenderer_.reset();
            rpiInput_.reset();
            return false;
        }
        return true;
    }

    (void)effectiveTheme;
    errorOut = "gb-window mode is supported only on macOS; use --ui=rpi-wrapper for Raspberry wrapper";
    return false;
}

bool SamplerIoLayer::readWindowEvents() {
    if (rpiInput_) {
        const bool hadPlatformEvents = rpiInput_->pollPlatformEvents();
        PrimitiveInputEvent ev{};
        bool hadInputEvents = false;
        while (rpiInput_->readNextInputEvent(ev)) {
            inputQueue_.push(ev);
            hadInputEvents = true;
        }
        return hadPlatformEvents || hadInputEvents;
    }
#if defined(__APPLE__)
    if (!windowRenderer_ || !windowInput_) {
        return false;
    }
    const bool hadWindowEvents = windowRenderer_->pumpEvents();
    PrimitiveInputEvent ev{};
    bool hadInputEvents = false;
    // Собираем все события из окна в общую очередь.
    while (windowInput_->readNextInputEvent(ev)) {
        inputQueue_.push(ev);
        hadInputEvents = true;
    }
    return hadWindowEvents || hadInputEvents;
#else
    return false;
#endif
}

bool SamplerIoLayer::readNextInputEvent(PrimitiveInputEvent& out) {
    return inputQueue_.tryPop(out);
}

void SamplerIoLayer::render(const UiState& state,
                            const UiPreparedLayout* preparedLayout) {
    if (rpiRenderer_) {
        if (preparedLayout) {
            rpiRenderer_->renderPreparedLayout(*preparedLayout);
        } else {
            rpiRenderer_->render(state);
        }
        return;
    }
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

void SamplerIoLayer::InputEventQueue::push(const PrimitiveInputEvent& ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Ограничиваем глубину очереди: при переполнении отбрасываем самый старый.
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
}

bool SamplerIoLayer::InputEventQueue::tryPop(PrimitiveInputEvent& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out = PrimitiveInputEvent{};
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

} // namespace avantgarde
