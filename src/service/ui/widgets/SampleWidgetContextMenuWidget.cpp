#include "service/ui/widgets/SampleWidgetContextMenuWidget.h"

#include <algorithm>

namespace avantgarde {

SampleWidgetContextMenuWidget::SampleWidgetContextMenuWidget(
    uint16_t frameWidth,
    std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : ContextMenuWidgetBase(
          "sample_menu",
          UiScene::SampleContextMenu,
          frameWidth,
          "SAMPLE MENU",
          " keys [F5/F6 select] [F1 apply] [space tap preview] [space hold menu] [esc back] ",
          std::move(layoutTemplate)) {}

std::vector<ContextMenuWidgetBase::MenuItem> SampleWidgetContextMenuWidget::buildMenuItems_(
    const UiState& rtState,
    const UiNavState& navState) const {
    const bool hasTracks = !rtState.tracks.empty();
    const uint8_t selectedTrack = (hasTracks && navState.selectedTrack < rtState.tracks.size())
                                      ? navState.selectedTrack
                                      : static_cast<uint8_t>(0U);
    const bool hasClip = hasTracks && !rtState.tracks[selectedTrack].clipPath.empty();

    return {
        {
            UiAction::Id::SceneSampleMenuPreview,
            "PREVIEW SAMPLE",
            " action:PREVIEW SAMPLE (start/end/speed from selected track) ",
            hasClip,
        },
        {
            UiAction::Id::SceneSampleMenuLoadSample,
            "LOAD SAMPLE",
            " action:LOAD SAMPLE (open manager) ",
            hasTracks,
        },
        {
            UiAction::Id::SceneSampleMenuDetectBpm,
            "DETECT BPM",
            " action:DETECT BPM (from selected track sample) ",
            hasClip,
        },
    };
}

WidgetOutput SampleWidgetContextMenuWidget::applyMenuItem_(UiAction::Id actionId,
                                                           const UiState& rtState,
                                                           UiNavState& navState) const {
    WidgetOutput out{};
    out.handled = true;

    const bool hasTracks = !rtState.tracks.empty();
    if (!hasTracks) {
        return out;
    }
    const uint8_t t = (navState.selectedTrack >= rtState.tracks.size())
                          ? static_cast<uint8_t>(rtState.tracks.size() - 1U)
                          : navState.selectedTrack;
    const UiTrackStateView& tr = rtState.tracks[t];

    switch (actionId) {
        case UiAction::Id::SceneSampleMenuPreview: {
            if (rtState.transport.previewPlaying) {
                UiIntent stop{};
                stop.type = UiIntentType::PreviewStop;
                out.intents.push_back(std::move(stop));
                break;
            }
            if (tr.clipPath.empty()) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::PreviewRequest;
            it.track = t;
            it.path = tr.clipPath;
            it.previewSpeed = std::clamp(tr.stretchRatio, 0.25f, 4.0f);
            it.previewStart01 = std::clamp(tr.trimStart01, 0.0f, 1.0f);
            it.previewEnd01 = std::clamp(tr.trimEnd01, 0.0f, 1.0f);
            it.previewGain = std::clamp(tr.gain01, 0.0f, 1.0f);
            out.intents.push_back(std::move(it));
        } break;
        case UiAction::Id::SceneSampleMenuLoadSample: {
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::Manager;
            it.resetCursor = true;
            it.resetScroll = true;
            it.resetSceneActionIndex = true;
            out.intents.push_back(std::move(it));
        } break;
        case UiAction::Id::SceneSampleMenuDetectBpm: {
            if (tr.clipPath.empty()) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::DetectProjectBpmFromTrack;
            it.track = t;
            out.intents.push_back(std::move(it));
        } break;
        default:
            out.handled = false;
            break;
    }
    return out;
}

} // namespace avantgarde
