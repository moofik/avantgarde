#include "service/ui/TrackContextMenuWidget.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "service/ui/layout/SceneFrameAsciiRenderer.h"
#include "service/ui/layout/UiNodeComponentComposer.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

TrackContextMenuWidget::TrackContextMenuWidget(
    uint16_t frameWidth,
    std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : frameWidth_(frameWidth) {
    if (layoutTemplate.has_value()) {
        layoutTemplate_ = layoutTemplate;
        buildLayoutModel_(*layoutTemplate);
    }
}

const char* TrackContextMenuWidget::id() const noexcept {
    return "track_menu";
}

uint16_t TrackContextMenuWidget::clampIndex_(uint16_t index) noexcept {
    return (index >= kItemCount) ? static_cast<uint16_t>(kItemCount - 1U) : index;
}

TrackContextMenuWidget::Item TrackContextMenuWidget::itemFromIndex_(uint16_t index) noexcept {
    switch (clampIndex_(index)) {
        case 0U: return Item::LoadSample;
        case 1U: return Item::Clear;
        case 2U: return Item::LoadFx;
        default: return Item::LoadSample;
    }
}

uint16_t TrackContextMenuWidget::wrapPrev_(uint16_t index) noexcept {
    const uint16_t safe = clampIndex_(index);
    return (safe == 0U) ? static_cast<uint16_t>(kItemCount - 1U) : static_cast<uint16_t>(safe - 1U);
}

uint16_t TrackContextMenuWidget::wrapNext_(uint16_t index) noexcept {
    const uint16_t safe = clampIndex_(index);
    return static_cast<uint16_t>((safe + 1U) % kItemCount);
}

const char* TrackContextMenuWidget::itemLabel_(Item item) noexcept {
    switch (item) {
        case Item::LoadSample: return "LOAD SAMPLE";
        case Item::Clear: return "CLEAR";
        case Item::LoadFx: return "LOAD FX";
        default: return "-";
    }
}

const char* TrackContextMenuWidget::itemStatus_(Item item) noexcept {
    switch (item) {
        case Item::LoadSample: return " action:LOAD SAMPLE (open manager) ";
        case Item::Clear: return " action:CLEAR (remove sample from track) ";
        case Item::LoadFx: return " action:LOAD FX";
        default: return " action:- ";
    }
}

std::string TrackContextMenuWidget::padRight_(const std::string& s, std::size_t width) {
    if (s.size() >= width) {
        return s.substr(0, width);
    }
    std::string out = s;
    out.append(width - s.size(), ' ');
    return out;
}

bool TrackContextMenuWidget::buildPreparedLayout(UiPreparedLayout& out,
                                                 const UiState&,
                                                 const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 32U);
    const uint16_t selectedIndex = clampIndex_(navState.sceneActionIndex);
    const Item selectedItem = itemFromIndex_(selectedIndex);

    char title[128]{};
    std::snprintf(title, sizeof(title), " %s T%u ", layout_.title.c_str(), static_cast<unsigned>(navState.selectedTrack + 1U));

    UiPreparedParams preparedParams{};
    preparedParams.text["status.scene.title"] = title;
    preparedParams.text["status.action"] = itemStatus_(selectedItem);
    preparedParams.text["status.keys"] = layout_.keysHint;
    preparedParams.rows["menu_list"] = {"LOAD SAMPLE", "CLEAR", "LOAD FX"};
    preparedParams.integer["menu_list.selectedRow"] = static_cast<int32_t>(selectedIndex);

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("track_menu")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(9);

    UiNodeComponentComposer::compose(UiScene::TrackContext, *layoutTemplate_, preparedParams, builder);

    out = std::move(builder).build();
    return true;
}

void TrackContextMenuWidget::render(UiTextBuffer& out,
                                    const UiState&,
                                    const UiNavState& navState) {
    out.clear();

    const std::size_t width = std::max<std::size_t>(frameWidth_, 32U);
    const std::size_t inner = width - 2U;
    const uint16_t selectedIndex = clampIndex_(navState.sceneActionIndex);
    const Item selectedItem = itemFromIndex_(selectedIndex);

    SceneFrame frame{};
    frame.width = static_cast<uint16_t>(width);
    frame.height = 9;
    frame.rects.push_back(SceneFrameRect{
        .x = 0,
        .y = 0,
        .width = static_cast<uint16_t>(width),
        .height = 9,
    });

    int y = 1;
    char title[128]{};
    std::snprintf(title,
                  sizeof(title),
                  " %s T%u ",
                  layout_.title.c_str(),
                  static_cast<unsigned>(navState.selectedTrack + 1U));
    frame.texts.push_back(SceneFrameText{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .text = padRight_(title, inner),
    });
    frame.hlines.push_back(SceneFrameHLine{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .length = static_cast<uint16_t>(inner),
        .glyph = "═",
    });

    for (uint16_t i = 0; i < kItemCount; ++i) {
        std::string row = (i == selectedIndex) ? "> " : "  ";
        row += itemLabel_(itemFromIndex_(i));
        frame.texts.push_back(SceneFrameText{
            .x = 1,
            .y = static_cast<int16_t>(y++),
            .text = padRight_(row, inner),
        });
    }

    frame.hlines.push_back(SceneFrameHLine{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .length = static_cast<uint16_t>(inner),
        .glyph = "═",
    });
    frame.texts.push_back(SceneFrameText{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .text = padRight_(itemStatus_(selectedItem), inner),
    });
    frame.texts.push_back(SceneFrameText{
        .x = 1,
        .y = static_cast<int16_t>(y++),
        .text = padRight_(layout_.keysHint, inner),
    });

    out.lines = SceneFrameAsciiRenderer::render(frame);
}

WidgetOutput TrackContextMenuWidget::applyItem_(Item item,
                                                const UiState& rtState,
                                                UiNavState& navState) const {
    WidgetOutput out{};
    out.handled = true;

    switch (item) {
        case Item::LoadSample: {
            navState.scene = UiScene::Manager;
            navState.cursor = 0;
            navState.scroll = 0;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            out.intents.push_back(std::move(it));
        } break;
        case Item::Clear: {
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
            navState.scene = UiScene::Tracks;
            navState.sceneActionIndex = 0;
            UiIntent back{};
            back.type = UiIntentType::Back;
            out.intents.push_back(std::move(back));
        } break;
        case Item::LoadFx: {
            if (rtState.tracks.empty()) {
                break;
            }
            navState.scene = UiScene::FxList;
            navState.selectedFx = 0;
            navState.fxAddPopupOpen = false;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            out.intents.push_back(std::move(it));
        } break;
        default:
            break;
    }
    return out;
}

WidgetOutput TrackContextMenuWidget::onGesture(UiGesture action,
                                               const UiState& rtState,
                                               UiNavState& navState) {
    switch (action) {
        case UiGesture::ListUp:
            navState.sceneActionIndex = wrapPrev_(navState.sceneActionIndex);
            navState.cursor = navState.sceneActionIndex;
            return WidgetOutput{true, {}};
        case UiGesture::ListDown:
            navState.sceneActionIndex = wrapNext_(navState.sceneActionIndex);
            navState.cursor = navState.sceneActionIndex;
            return WidgetOutput{true, {}};
        case UiGesture::ListEnter:
            return applyItem_(itemFromIndex_(navState.sceneActionIndex), rtState, navState);
        default:
            return {};
    }
}

UiActionCatalog TrackContextMenuWidget::queryAvailableActions(const UiState& rtState,
                                                              const UiNavState& navState) const {
    UiActionCatalog out{};
    const bool hasTracks = !rtState.tracks.empty();

    UiAction load{};
    load.def.id = UiAction::Id::SceneTrackMenuLoadSample;
    load.def.scope = UiAction::Scope::Scene;
    load.def.execution = UiAction::Execution::ApplyRequired;
    load.def.valueKind = UiAction::ValueKind::None;
    load.def.label = "Load Sample";
    load.state.enabled = hasTracks;
    out.actions.push_back(std::move(load));

    UiAction clear{};
    clear.def.id = UiAction::Id::SceneTrackMenuClear;
    clear.def.scope = UiAction::Scope::Scene;
    clear.def.execution = UiAction::Execution::ApplyRequired;
    clear.def.valueKind = UiAction::ValueKind::None;
    clear.def.label = "Clear";
    clear.state.enabled = hasTracks;
    out.actions.push_back(std::move(clear));

    UiAction fx{};
    fx.def.id = UiAction::Id::SceneTrackMenuFxList;
    fx.def.scope = UiAction::Scope::Scene;
    fx.def.execution = UiAction::Execution::ApplyRequired;
    fx.def.valueKind = UiAction::ValueKind::None;
    fx.def.label = "Load FX";
    fx.state.enabled = hasTracks;
    out.actions.push_back(std::move(fx));

    out.currentIndex = clampIndex_(navState.sceneActionIndex);
    for (std::size_t i = 0; i < out.actions.size(); ++i) {
        out.actions[i].state.selected = (i == out.currentIndex);
    }
    return out;
}

WidgetOutput TrackContextMenuWidget::onAction(UiAction& action,
                                              const UiState& rtState,
                                              UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;

    if (action.op != UiAction::Op::Apply &&
        action.op != UiAction::Op::Press) {
        return out;
    }

    switch (action.def.id) {
        case UiAction::Id::SceneTrackMenuLoadSample:
            return applyItem_(Item::LoadSample, rtState, navState);
        case UiAction::Id::SceneTrackMenuClear:
            return applyItem_(Item::Clear, rtState, navState);
        case UiAction::Id::SceneTrackMenuFxList:
            return applyItem_(Item::LoadFx, rtState, navState);
        default:
            out.handled = false;
            return out;
    }
}

void TrackContextMenuWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "track_menu") {
        return;
    }
    tpl.forEachNode([&](const UiLayoutNode& node) {
        if (node.type == UiLayoutNodeType::StatusBar && !node.text.empty()) {
            layout_.title = node.text;
        }
        if (node.type == UiLayoutNodeType::Text &&
            node.id == "keys_hint" &&
            !node.text.empty()) {
            layout_.keysHint = node.text;
        }
    });
    layout_.enabled = true;
}

} // namespace avantgarde
