#include "service/ui/widgets/TrackContextMenuWidget.h"

namespace avantgarde {

TrackContextMenuWidget::TrackContextMenuWidget(
    uint16_t frameWidth,
    std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : ContextMenuWidgetBase(
          "track_menu",
          UiScene::TrackContext,
          frameWidth,
          "TRACK MENU",
          " keys [F5/F6 select] [F1 apply] [n seq] [shift+n pattern] [esc back] ",
          std::move(layoutTemplate)) {}

std::vector<ContextMenuWidgetBase::MenuItem> TrackContextMenuWidget::buildMenuItems_(const UiState& rtState,
                                                                                      const UiNavState&) const {
    const bool hasTracks = !rtState.tracks.empty();
    return {
        {
            UiAction::Id::SceneTrackMenuClear,
            "CLEAR",
            " action:CLEAR (remove sample from track) ",
            hasTracks,
        },
        {
            UiAction::Id::SceneTrackMenuFxList,
            "LOAD FX",
            " action:LOAD FX ",
            hasTracks,
        },
        {
            UiAction::Id::SceneTrackMenuSampleEdit,
            "SAMPLE EDIT",
            " action:SAMPLE EDIT ",
            hasTracks,
        },
        {
            UiAction::Id::SceneTrackMenuSequencer,
            "SEQUENCER VIEW",
            " action:OPEN SEQUENCER ",
            true,
        },
        {
            UiAction::Id::SceneTrackMenuPatternEdit,
            "PATTERN EDIT",
            " action:OPEN PATTERN EDIT ",
            true,
        },
    };
}

WidgetOutput TrackContextMenuWidget::applyMenuItem_(UiAction::Id actionId,
                                                    const UiState& rtState,
                                                    UiNavState& navState) const {
    WidgetOutput out{};
    out.handled = true;

    switch (actionId) {
        case UiAction::Id::SceneTrackMenuClear: {
            if (rtState.tracks.empty()) {
                break;
            }
            const uint8_t t = (navState.selectedTrack >= rtState.tracks.size())
                                  ? static_cast<uint8_t>(rtState.tracks.size() - 1U)
                                  : navState.selectedTrack;
            UiIntent clear{};
            clear.type = UiIntentType::ClearTrackSample;
            clear.track = t;
            out.intents.push_back(std::move(clear));
            UiIntent back{};
            back.type = UiIntentType::Back;
            back.scene = UiScene::Tracks;
            back.resetSceneActionIndex = true;
            out.intents.push_back(std::move(back));
        } break;
        case UiAction::Id::SceneTrackMenuFxList: {
            if (rtState.tracks.empty()) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::FxList;
            it.resetSceneActionIndex = true;
            it.resetSelectedFx = true;
            it.closeFxAddPopup = true;
            out.intents.push_back(std::move(it));
        } break;
        case UiAction::Id::SceneTrackMenuSampleEdit: {
            if (rtState.tracks.empty()) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::SampleEdit;
            it.resetSceneActionIndex = true;
            out.intents.push_back(std::move(it));
        } break;
        case UiAction::Id::SceneTrackMenuSequencer: {
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::Sequencer;
            it.resetSceneActionIndex = true;
            it.resetCursor = true;
            it.resetScroll = true;
            out.intents.push_back(std::move(it));
        } break;
        case UiAction::Id::SceneTrackMenuPatternEdit: {
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::PatternEdit;
            it.resetSceneActionIndex = true;
            it.resetCursor = true;
            it.resetScroll = true;
            out.intents.push_back(std::move(it));
        } break;
        default:
            out.handled = false;
            break;
    }
    return out;
}

} // namespace avantgarde

