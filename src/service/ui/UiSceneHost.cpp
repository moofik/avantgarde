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

UiInputAction normalizeHardwareAction(UiInputAction action) noexcept {
    // F-ряд выступает как аппаратные "софт-кнопки".
    // Приводим их к общим action-командам, чтобы остальная логика была единообразной.
    switch (action) {
        case UiInputAction::F1: return UiInputAction::ActionApply;
        case UiInputAction::F2: return UiInputAction::ActionUndo;
        case UiInputAction::F3: return UiInputAction::ActionScopeToggle;
        case UiInputAction::F4: return UiInputAction::OpenManager;
        case UiInputAction::F5: return UiInputAction::ActionFocusPrev;
        case UiInputAction::F6: return UiInputAction::ActionFocusNext;
        case UiInputAction::F7: return UiInputAction::ActionAdjustPrev;
        case UiInputAction::F8: return UiInputAction::ActionAdjustNext;
        case UiInputAction::F9: return UiInputAction::ActionRedo;
        case UiInputAction::F10: return UiInputAction::ActionAlt;
        case UiInputAction::F11: return UiInputAction::TrackPagePrev;
        case UiInputAction::F12: return UiInputAction::TrackPageNext;
        default:
            return action;
    }
}

bool isPointerAction(UiInputAction action) noexcept {
    switch (action) {
        case UiInputAction::ActionFocusPrev:
        case UiInputAction::ActionFocusNext:
        case UiInputAction::ActionAdjustPrev:
        case UiInputAction::ActionAdjustNext:
        case UiInputAction::ActionApply:
        case UiInputAction::ActionUndo:
        case UiInputAction::ActionRedo:
        case UiInputAction::ActionQuick:
        case UiInputAction::ActionAlt:
        case UiInputAction::ActionPress:
        case UiInputAction::ActionRelease:
            return true;
        default:
            return false;
    }
}

UiAction::Op toActionOp(UiInputAction action) noexcept {
    switch (action) {
        case UiInputAction::ActionFocusPrev: return UiAction::Op::FocusPrev;
        case UiInputAction::ActionFocusNext: return UiAction::Op::FocusNext;
        case UiInputAction::ActionAdjustPrev: return UiAction::Op::AdjustPrev;
        case UiInputAction::ActionAdjustNext: return UiAction::Op::AdjustNext;
        case UiInputAction::ActionApply: return UiAction::Op::Apply;
        case UiInputAction::ActionUndo: return UiAction::Op::Undo;
        case UiInputAction::ActionRedo: return UiAction::Op::Redo;
        case UiInputAction::ActionPress: return UiAction::Op::Press;
        case UiInputAction::ActionRelease: return UiAction::Op::Release;
        case UiInputAction::ActionQuick: return UiAction::Op::Apply;
        case UiInputAction::ActionAlt: return UiAction::Op::Apply;
        default: return UiAction::Op::None;
    }
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
    action = normalizeHardwareAction(action);

    // В трековой сцене даем удобные алиасы под active-action-pointer:
    // j/k — смена активного action, Enter — apply.
    if (nav_.scene == UiScene::Tracks) {
        if (action == UiInputAction::ListUp) {
            action = UiInputAction::ActionFocusPrev;
        } else if (action == UiInputAction::ListDown) {
            action = UiInputAction::ActionFocusNext;
        } else if (action == UiInputAction::ListEnter) {
            action = UiInputAction::ActionApply;
        }
    }

    if (action == UiInputAction::ActionScopeToggle) {
        nav_.actionScope = (nav_.actionScope == UiAction::Scope::Scene)
                               ? UiAction::Scope::Global
                               : UiAction::Scope::Scene;
        return WidgetOutput{true, {}};
    }

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

    // Active Action Pointer path:
    // action + navState + uiState -> intent (вычисляется в виджете).
    if (isPointerAction(action)) {
        // Пока глобальные actions не реализованы как отдельный каталог:
        // даже в Global-режиме мягко фоллбэкаемся на scene-catalog,
        // чтобы управление не "замерзало" для пользователя.

        UiActionCatalog catalog = widget->queryAvailableActions(rtState, nav_);
        if (catalog.actions.empty()) {
            return WidgetOutput{true, {}};
        }

        nav_.sceneActionIndex = std::min<uint16_t>(
            nav_.sceneActionIndex,
            static_cast<uint16_t>(catalog.actions.size() - 1U));

        const UiAction::Op op = toActionOp(action);
        if (op == UiAction::Op::FocusPrev) {
            if (nav_.sceneActionIndex == 0U) {
                nav_.sceneActionIndex = static_cast<uint16_t>(catalog.actions.size() - 1U);
            } else {
                nav_.sceneActionIndex = static_cast<uint16_t>(nav_.sceneActionIndex - 1U);
            }
            return WidgetOutput{true, {}};
        }
        if (op == UiAction::Op::FocusNext) {
            nav_.sceneActionIndex = static_cast<uint16_t>((nav_.sceneActionIndex + 1U) % catalog.actions.size());
            return WidgetOutput{true, {}};
        }

        UiAction active = catalog.actions[nav_.sceneActionIndex];
        active.op = op;
        if (op == UiAction::Op::AdjustPrev) {
            active.delta = -std::max(0.0f, active.def.step);
        } else if (op == UiAction::Op::AdjustNext) {
            active.delta = std::max(0.0f, active.def.step);
        }
        return widget->onAction(active, rtState, nav_);
    }

    return widget->onInput(action, rtState, nav_);
}

} // namespace avantgarde
