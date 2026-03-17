#include "service/ui/UiSceneHost.h"

namespace avantgarde {

bool UiSceneHost::registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget) {
    if (!widget) {
        return false;
    }
    // Registry keeps single owner for each scene widget.
    widgets_[sceneIndex_(scene)] = std::move(widget);
    return true;
}

void UiSceneHost::setScene(UiScene scene) noexcept {
    nav_.scene = scene;
}

UiScene UiSceneHost::scene() const noexcept {
    return nav_.scene;
}

UiNavState& UiSceneHost::nav() noexcept {
    return nav_;
}

const UiNavState& UiSceneHost::nav() const noexcept {
    return nav_;
}

bool UiSceneHost::renderActive(UiTextBuffer& out, const UiState& rtState) const {
    // Always start with a clean frame buffer for deterministic rendering.
    out.clear();
    const auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        return false;
    }
    widget->render(out, rtState, nav_);
    return true;
}

WidgetOutput UiSceneHost::handleInput(UiInputAction action, const UiState& rtState) {
    // Global shortcuts are handled by host to keep behavior consistent across scenes.
    if (action == UiInputAction::SelectTrack0) {
        nav_.selectedTrack = 0;
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::SelectTrack1) {
        nav_.selectedTrack = 1;
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::OpenManager) {
        if (widgets_[sceneIndex_(UiScene::Manager)]) {
            nav_.scene = UiScene::Manager;
            return WidgetOutput{true, {UiIntent{UiIntentType::OpenScene}}};
        }
        return {};
    }
    if (action == UiInputAction::BackScene) {
        if (nav_.scene != UiScene::Tracks) {
            nav_.scene = UiScene::Tracks;
            return WidgetOutput{true, {UiIntent{UiIntentType::Back}}};
        }
        return {};
    }

    auto& widget = widgets_[sceneIndex_(nav_.scene)];
    if (!widget) {
        return {};
    }
    return widget->onInput(action, rtState, nav_);
}

} // namespace avantgarde
