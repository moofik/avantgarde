#include "app/SamplerIoLayer.h"

#include <chrono>

#include "control/TerminalUiInput.h"
#include "platform/lowres/LowResUiRenderer.h"
#include "platform/macos/MacGbWindowRenderer.h"
#include "platform/terminal/AnsiUiRenderer.h"
#include "platform/terminal/GothicGbUiRenderer.h"
#include "platform/terminal/TerminalCharDisplay.h"

namespace avantgarde {

bool parseSamplerUiMode(std::string_view raw, SamplerUiMode& out) noexcept {
    if (raw == "ansi") {
        out = SamplerUiMode::Ansi;
        return true;
    }
    if (raw == "lowres") {
        out = SamplerUiMode::LowRes;
        return true;
    }
    if (raw == "gb") {
        out = SamplerUiMode::Gb;
        return true;
    }
    if (raw == "gb-window") {
        out = SamplerUiMode::GbWindow;
        return true;
    }
    return false;
}

SamplerIoLayer::~SamplerIoLayer() {
    // Safety: гарантируем join фонового input thread.
    stopTerminalInput();
}

bool SamplerIoLayer::init(const SamplerIoConfig& config, std::string& errorOut) {
    // Для GB-режимов по умолчанию используем gothic-тему.
    const UiTheme effectiveGbTheme = config.themeProvided ? config.theme : UiTheme::Gothic;

    if (config.mode == SamplerUiMode::LowRes) {
        display_ = std::make_unique<TerminalCharDisplay>(64, 16);
        renderer_ = std::make_unique<LowResUiRenderer>(*display_);
        return true;
    }
    if (config.mode == SamplerUiMode::GbWindow) {
        // В window backend запоминаем типизированный указатель для input pump.
        renderer_ = std::make_unique<MacGbWindowRenderer>(effectiveGbTheme, config.gbTextWidth);
        windowRenderer_ = dynamic_cast<MacGbWindowRenderer*>(renderer_.get());
        return true;
    }
    if (config.mode == SamplerUiMode::Gb) {
        renderer_ = std::make_unique<GothicGbUiRenderer>(effectiveGbTheme, config.gbTextWidth);
        return true;
    }
    if (config.mode == SamplerUiMode::Ansi) {
        renderer_ = std::make_unique<AnsiUiRenderer>();
        return true;
    }

    errorOut = "unsupported io mode";
    return false;
}

void SamplerIoLayer::startTerminalInput(std::atomic<bool>& stopFlag) {
    // Перезапуск потока разрешен: сначала стопаем старый.
    stopTerminalInput();
    terminalInputThread_ = std::thread([this, &stopFlag]() {
        TerminalUiInput input;
        while (!stopFlag.load(std::memory_order_acquire)) {
            UiInputEvent ev{};
            if (input.poll(ev)) {
                inputQueue_.push(ev);
                continue;
            }
            // Небольшой sleep снижает CPU spin в idle.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void SamplerIoLayer::stopTerminalInput() noexcept {
    if (terminalInputThread_.joinable()) {
        terminalInputThread_.join();
    }
}

bool SamplerIoLayer::pumpWindowInput() {
    if (!windowRenderer_) {
        return false;
    }
    windowRenderer_->pumpEvents();
    UiInputEvent ev{};
    // Собираем все события из окна в общую очередь.
    while (windowRenderer_->pollInput(ev)) {
        inputQueue_.push(ev);
    }
    return true;
}

bool SamplerIoLayer::pollInput(UiInputEvent& out) {
    return inputQueue_.tryPop(out);
}

bool SamplerIoLayer::renderOnMainThread() const noexcept {
    return windowRenderer_ != nullptr;
}

void SamplerIoLayer::render(const UiState& state, const std::string& sceneFrame, bool showHeaderOverlay) {
    if (!renderer_) {
        return;
    }
    // Если scene frame сформирован в application слое, отдаем его напрямую
    // в backends, которые умеют custom frame.
    if (!sceneFrame.empty()) {
        if (auto* windowRenderer = dynamic_cast<MacGbWindowRenderer*>(renderer_.get())) {
            windowRenderer->renderCustomFrame(sceneFrame, showHeaderOverlay);
            return;
        }
        if (auto* gbRenderer = dynamic_cast<GothicGbUiRenderer*>(renderer_.get())) {
            gbRenderer->renderCustomFrame(sceneFrame);
            return;
        }
    }
    // Fallback: обычный render(state) для backends без custom frame.
    renderer_->render(state);
}

void SamplerIoLayer::InputEventQueue::push(const UiInputEvent& ev) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Ограничиваем глубину очереди: при переполнении отбрасываем самый старый.
    if (queue_.size() >= 1024U) {
        queue_.pop_front();
    }
    queue_.push_back(ev);
}

bool SamplerIoLayer::InputEventQueue::tryPop(UiInputEvent& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        out.action = UiInputAction::None;
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

} // namespace avantgarde
