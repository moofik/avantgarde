#include "service/ui/UiSceneHost.h"

namespace avantgarde {
namespace {

constexpr std::size_t kTracksPerPage = 2;

uint8_t selectPrevTrack(uint8_t current, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t last = static_cast<uint8_t>(totalTracks - 1U);
    if (current > last) {
        return last;
    }
    return (current == 0U) ? last : static_cast<uint8_t>(current - 1U);
}

uint8_t selectNextTrack(uint8_t current, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t last = static_cast<uint8_t>(totalTracks - 1U);
    if (current > last) {
        return 0;
    }
    return (current == last) ? 0U : static_cast<uint8_t>(current + 1U);
}

uint16_t pageForTrack(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint8_t safeTrack = (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
    return static_cast<uint16_t>(safeTrack / kTracksPerPage);
}

uint16_t wrapPrevPage(uint16_t currentPage, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint16_t totalPages = static_cast<uint16_t>((totalTracks + kTracksPerPage - 1U) / kTracksPerPage);
    if (totalPages <= 1) {
        return 0;
    }
    if (currentPage >= totalPages) {
        return static_cast<uint16_t>(totalPages - 1U);
    }
    return (currentPage == 0U) ? static_cast<uint16_t>(totalPages - 1U) : static_cast<uint16_t>(currentPage - 1U);
}

uint16_t wrapNextPage(uint16_t currentPage, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    const uint16_t totalPages = static_cast<uint16_t>((totalTracks + kTracksPerPage - 1U) / kTracksPerPage);
    if (totalPages <= 1) {
        return 0;
    }
    if (currentPage >= totalPages) {
        return 0;
    }
    return (currentPage + 1U >= totalPages) ? 0U : static_cast<uint16_t>(currentPage + 1U);
}

} // namespace

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
    if (action == UiInputAction::SelectPrevTrack) {
        nav_.selectedTrack = selectPrevTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::SelectNextTrack) {
        nav_.selectedTrack = selectNextTrack(nav_.selectedTrack, rtState.tracks.size());
        nav_.trackPage = pageForTrack(nav_.selectedTrack, rtState.tracks.size());
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::TrackPagePrev) {
        nav_.trackPage = wrapPrevPage(nav_.trackPage, rtState.tracks.size());
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::TrackPageNext) {
        nav_.trackPage = wrapNextPage(nav_.trackPage, rtState.tracks.size());
        return WidgetOutput{true, {}};
    }
    if (action == UiInputAction::OpenManager) {
        if (widgets_[sceneIndex_(UiScene::Manager)]) {
            nav_.scene = UiScene::Manager;
            UiIntent openIntent{};
            openIntent.type = UiIntentType::OpenScene;
            return WidgetOutput{true, {openIntent}};
        }
        return {};
    }
    if (action == UiInputAction::BackScene) {
        if (nav_.scene != UiScene::Tracks) {
            nav_.scene = UiScene::Tracks;
            UiIntent backIntent{};
            backIntent.type = UiIntentType::Back;
            return WidgetOutput{true, {backIntent}};
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
