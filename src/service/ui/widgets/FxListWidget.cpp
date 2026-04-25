#include "service/ui/widgets/FxListWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "contracts/FxRegistry.h"

namespace avantgarde {
namespace {

std::string trimMiddle(const std::string& s, std::size_t width) {
    if (s.size() <= width) {
        return s;
    }
    if (width <= 3) {
        return s.substr(0, width);
    }
    const std::size_t left = (width - 3U) / 2U;
    const std::size_t right = width - 3U - left;
    return s.substr(0, left) + "..." + s.substr(s.size() - right);
}

} // namespace

FxListWidget::FxListWidget(uint16_t frameWidth,
                           std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : frameWidth_(frameWidth) {
    if (layoutTemplate.has_value()) {
        layoutTemplate_ = layoutTemplate;
        buildLayoutModel_(*layoutTemplate);
    }
}

const char* FxListWidget::id() const noexcept {
    return "fx_list";
}

uint8_t FxListWidget::clampTrack_(uint8_t track, std::size_t totalTracks) noexcept {
    if (totalTracks == 0) {
        return 0;
    }
    return (track >= totalTracks) ? static_cast<uint8_t>(totalTracks - 1U) : track;
}

uint16_t FxListWidget::clampFx_(uint16_t fx, std::size_t fxCount) noexcept {
    if (fxCount == 0) {
        return 0;
    }
    return (fx >= fxCount) ? static_cast<uint16_t>(fxCount - 1U) : fx;
}

uint16_t FxListWidget::clampFxCursor_(uint16_t fxCursor, std::size_t fxCount) noexcept {
    // Курсор может указывать на "виртуальный пустой слот" = fxCount.
    const std::size_t maxIndex = fxCount;
    return (fxCursor > maxIndex) ? static_cast<uint16_t>(maxIndex) : fxCursor;
}

std::string FxListWidget::fxName_(const UiTrackStateView& track, uint16_t slot) {
    if (slot < track.fxChainIds.size()) {
        const std::string& id = track.fxChainIds[slot];
        if (const FxDescriptor* d = FxRegistry::find(id)) {
            return std::string(d->displayName);
        }
        if (!id.empty()) {
            return id;
        }
    }
    return "Unknown FX";
}

bool FxListWidget::fxEnabled_(const UiTrackStateView& track, uint16_t slot) noexcept {
    if (slot < track.fxEnabled.size()) {
        return track.fxEnabled[slot] != 0U;
    }
    return true;
}

const std::array<FxListWidget::FxTypeOption, 5>& FxListWidget::fxTypeOptions_() noexcept {
    static const std::array<FxTypeOption, 5> kTypes{{
        {FxRegistry::kReverbSchroederId, "Reverb"},
        {FxRegistry::kHpfOnePoleId, "HPF"},
        {FxRegistry::kStutterId, "Stutter"},
        {FxRegistry::kBufferFxId, "Buffer FX"},
        {FxRegistry::kSuperGlitchId, "Super Glitch"},
    }};
    return kTypes;
}

uint16_t FxListWidget::clampFxType_(uint16_t typeIndex) noexcept {
    const std::size_t total = fxTypeOptions_().size();
    if (total == 0U) {
        return 0U;
    }
    return (typeIndex >= total) ? static_cast<uint16_t>(total - 1U) : typeIndex;
}

bool FxListWidget::buildPreparedLayout(UiPreparedLayout& out,
                                       const UiState& rtState,
                                       const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 28U);
    const std::size_t inner = static_cast<std::size_t>(frameWidth - 2U);
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const UiTrackStateView* tr = rtState.tracks.empty() ? nullptr : &rtState.tracks[track];
    const std::size_t fxCount = tr ? tr->fxCount : 0U;
    const uint16_t fxCursor = clampFxCursor_(navState.selectedFx, fxCount);
    const bool popupOpen = navState.fxAddPopupOpen;
    const uint16_t selectedFxType = clampFxType_(navState.selectedFxType);

    std::vector<std::string> rows{};
    rows.reserve(listRows_);
    int32_t selectedRow = -1;
    std::string quickLine{};
    std::string actionLine{};
    std::string keys{};
    char title[160]{};

    if (popupOpen) {
        const auto& types = fxTypeOptions_();
        for (std::size_t i = 0; i < listRows_; ++i) {
            if (i < types.size()) {
                std::string line = std::string(types[i].label);
                rows.push_back(std::move(line));
                if (i == static_cast<std::size_t>(selectedFxType)) {
                    selectedRow = static_cast<int32_t>(i);
                }
            } else {
                rows.emplace_back(" ");
            }
        }
        if (selectedRow < 0 && !types.empty()) {
            selectedRow = 0;
        }
        std::snprintf(title, sizeof(title), " ADD FX T%u ", static_cast<unsigned>(track + 1U));
        quickLine = " popup: choose FX type ";
        const FxTypeOption& type = fxTypeOptions_()[selectedFxType];
        char action[192]{};
        std::snprintf(action,
                      sizeof(action),
                      " action:add %s to S%u ",
                      std::string(type.label).c_str(),
                      static_cast<unsigned>(fxCount + 1U));
        actionLine = action;
        keys = " keys [F5/F6 choose] [F1 add] [esc cancel] ";
    } else {
        const std::size_t listWidth = (inner > 12U) ? (inner - 12U) : inner;
        const std::size_t totalRows = fxCount + 1U; // +1 = виртуальный пустой слот для Add FX.
        const std::size_t start = (totalRows > listRows_ && fxCursor >= listRows_)
                                      ? static_cast<std::size_t>(fxCursor + 1U - listRows_)
                                      : 0U;
        for (std::size_t row = 0; row < listRows_; ++row) {
            const std::size_t idx = start + row;
            if (idx >= totalRows) {
                rows.emplace_back(" ");
                continue;
            }

            if (idx < fxCount && tr != nullptr) {
                std::string line = "S";
                line += std::to_string(static_cast<unsigned>(idx + 1U));
                line += fxEnabled_(*tr, static_cast<uint16_t>(idx)) ? " [ON] " : " [OFF] ";
                line += trimMiddle(fxName_(*tr, static_cast<uint16_t>(idx)), listWidth);
                rows.push_back(std::move(line));
            } else {
                char empty[128]{};
                std::snprintf(empty, sizeof(empty), "S%u [EMPTY] Add FX",
                              static_cast<unsigned>(idx + 1U));
                rows.emplace_back(empty);
            }
            if (idx == static_cast<std::size_t>(fxCursor)) {
                selectedRow = static_cast<int32_t>(row);
            }
        }
        if (selectedRow < 0) {
            selectedRow = 0;
        }

        std::snprintf(title,
                      sizeof(title),
                      " %s T%u slots:%u ",
                      layout_.title.c_str(),
                      static_cast<unsigned>(track + 1U),
                      static_cast<unsigned>(fxCount));
        quickLine = " quick: F1 edit/add | F7 bypass | F8 remove ";
        const bool onEmptySlot = (fxCursor >= fxCount);
        if (onEmptySlot) {
            actionLine = " action:F1 open Add FX popup ";
        } else {
            actionLine = " action:F1 edit | F7 bypass | F8 remove ";
        }
        keys = !layout_.keysHint.empty()
                   ? layout_.keysHint
                   : " keys [F5/F6 slot] [F1 apply] [F7 bypass] [F8 remove] [esc] ";
    }

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("fx_list")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(static_cast<uint16_t>(7U + listRows_))
        .addComponent(UiStatusBarBuilder("header_title").text(title))
        .addComponent(UiSeparatorBuilder("sep_top").style(UiSeparatorComponent::Style::Heavy))
        .addComponent(UiTextBuilder("fx_type_status").text(std::move(quickLine)))
        .addComponent(UiListBuilder("fx_list")
                          .rows(std::move(rows))
                          .selectedRow(selectedRow)
                          .marker(UiListComponent::Marker::Arrow))
        .addComponent(UiSeparatorBuilder("sep_bottom").style(UiSeparatorComponent::Style::Heavy))
        .addComponent(UiTextBuilder("action_status").text(std::move(actionLine)))
        .addComponent(UiTextBuilder("keys_hint").text(keys));

    out = std::move(builder).build();
    return true;
}

WidgetOutput FxListWidget::onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t cursorMax = static_cast<uint16_t>(fxCount); // +1 виртуальный пустой слот.
    const uint16_t cursor = clampFxCursor_(navState.selectedFx, fxCount);

    if (navState.fxAddPopupOpen) {
        if (action == UiGesture::ListUp || action == UiGesture::ListDown) {
            const std::size_t totalTypes = fxTypeOptions_().size();
            if (totalTypes == 0U) {
                out.handled = true;
                return out;
            }
            const uint16_t current = clampFxType_(navState.selectedFxType);
            if (action == UiGesture::ListUp) {
                navState.selectedFxType = (current == 0U)
                                              ? static_cast<uint16_t>(totalTypes - 1U)
                                              : static_cast<uint16_t>(current - 1U);
            } else {
                navState.selectedFxType = static_cast<uint16_t>((current + 1U) % totalTypes);
            }
            out.handled = true;
            return out;
        }
        if (action == UiGesture::ListEnter) {
            if (rtState.tracks.empty()) {
                out.handled = true;
                navState.fxAddPopupOpen = false;
                return out;
            }
            const FxTypeOption& type = fxTypeOptions_()[clampFxType_(navState.selectedFxType)];
            UiIntent it{};
            it.type = UiIntentType::AddFxToTrack;
            it.track = track;
            it.path = std::string(type.id);
            out.handled = true;
            out.intents.push_back(std::move(it));
            navState.fxAddPopupOpen = false;
            navState.selectedFx = static_cast<uint16_t>(fxCount);
            return out;
        }
        if (action == UiGesture::ListParent || action == UiGesture::BackScene) {
            navState.fxAddPopupOpen = false;
            out.handled = true;
            return out;
        }
        return {};
    }

    if (action == UiGesture::ListUp) {
        navState.selectedFx = (cursor == 0U) ? cursorMax : static_cast<uint16_t>(cursor - 1U);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListDown) {
        navState.selectedFx = (cursor >= cursorMax) ? 0U : static_cast<uint16_t>(cursor + 1U);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListEnter) {
        navState.selectedFx = cursor;
        if (cursor >= fxCount) {
            navState.fxAddPopupOpen = true;
            out.handled = true;
            return out;
        }

        UiIntent it{};
        it.type = UiIntentType::OpenScene;
        it.scene = UiScene::FxEditor;
        it.resetSceneActionIndex = true;
        it.track = track;
        it.fxSlot = static_cast<uint8_t>(cursor);
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }
    if (action == UiGesture::ListParent || action == UiGesture::BackScene) {
        UiIntent it{};
        it.type = UiIntentType::Back;
        it.scene = UiScene::Tracks;
        it.resetSceneActionIndex = true;
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }

    return {};
}

UiActionCatalog FxListWidget::queryAvailableActions(const UiState& rtState, const UiNavState& navState) const {
    UiActionCatalog out{};
    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t track = clampTrack_(navState.selectedTrack, totalTracks);
    const std::size_t fxCount = (totalTracks == 0) ? 0U : rtState.tracks[track].fxCount;
    const uint16_t selectedFxType = clampFxType_(navState.selectedFxType);
    const uint16_t selectedFx = clampFxCursor_(navState.selectedFx, fxCount);
    const bool selectedExistingSlot = (selectedFx < fxCount);

    auto push = [&out](UiAction a) {
        out.actions.push_back(std::move(a));
    };

    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxSlotSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Integer;
        a.def.label = "FX Slot";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, fxCount + 1U));
        a.def.step = 1.0f;
        a.state.enabled = true;
        a.state.value = static_cast<float>(selectedFx + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxEnabled;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Bool;
        a.def.label = "FX On/Off";
        a.def.minValue = 0.0f;
        a.def.maxValue = 1.0f;
        a.def.step = 1.0f;
        a.state.enabled = (totalTracks > 0U) && selectedExistingSlot && !navState.fxAddPopupOpen;
        a.state.value = ((totalTracks > 0U) && selectedExistingSlot && fxEnabled_(rtState.tracks[track], selectedFx)) ? 1.0f : 0.0f;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxTypeSelect;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ImmediateStep;
        a.def.valueKind = UiAction::ValueKind::Enum;
        a.def.label = "FX Type";
        a.def.minValue = 1.0f;
        a.def.maxValue = static_cast<float>(fxTypeOptions_().size());
        a.def.step = 1.0f;
        a.state.enabled = (totalTracks > 0U);
        a.state.value = static_cast<float>(selectedFxType + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneAddFx;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Add FX";
        a.state.enabled = (totalTracks > 0U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxOpenEditor;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Open FX Editor";
        a.state.enabled = (totalTracks > 0U) && selectedExistingSlot && !navState.fxAddPopupOpen;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxRemove;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Remove FX";
        a.state.enabled = (totalTracks > 0U) && selectedExistingSlot && !navState.fxAddPopupOpen;
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxBack;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Back To Tracks";
        a.state.enabled = true;
        push(std::move(a));
    }

    if (!out.actions.empty()) {
        out.currentIndex = std::min<uint16_t>(navState.sceneActionIndex, static_cast<uint16_t>(out.actions.size() - 1U));
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            out.actions[i].state.selected = (i == out.currentIndex);
        }
    }

    return out;
}

WidgetOutput FxListWidget::onAction(UiAction& action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    out.handled = true;

    const std::size_t totalTracks = rtState.tracks.size();
    const uint8_t track = clampTrack_(navState.selectedTrack, totalTracks);
    const std::size_t fxCount = (totalTracks == 0) ? 0U : rtState.tracks[track].fxCount;

    auto pushIntent = [&out](UiIntent intent) {
        out.intents.push_back(std::move(intent));
    };

    switch (action.def.id) {
        case UiAction::Id::SceneFxSlotSelect: {
            const uint16_t current = clampFxCursor_(navState.selectedFx, fxCount);
            if (action.op == UiAction::Op::AdjustPrev) {
                const uint16_t cursorMax = static_cast<uint16_t>(fxCount);
                navState.selectedFx = (current == 0U) ? cursorMax : static_cast<uint16_t>(current - 1U);
            } else if (action.op == UiAction::Op::AdjustNext) {
                const uint16_t cursorMax = static_cast<uint16_t>(fxCount);
                navState.selectedFx = (current >= cursorMax) ? 0U : static_cast<uint16_t>(current + 1U);
            } else if (action.op == UiAction::Op::Apply ||
                       action.op == UiAction::Op::Press) {
                navState.selectedFx = current;
                if (current >= fxCount) {
                    navState.fxAddPopupOpen = true;
                    break;
                }
                UiIntent it{};
                it.type = UiIntentType::OpenScene;
                it.scene = UiScene::FxEditor;
                it.resetSceneActionIndex = true;
                it.track = track;
                it.fxSlot = static_cast<uint8_t>(current);
                pushIntent(std::move(it));
            }
        } break;

        case UiAction::Id::SceneFxEnabled: {
            if (fxCount == 0U || navState.selectedFx >= fxCount || navState.fxAddPopupOpen) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press &&
                action.op != UiAction::Op::AdjustPrev &&
                action.op != UiAction::Op::AdjustNext) {
                break;
            }
            const uint16_t current = clampFx_(navState.selectedFx, fxCount);
            const bool enabledNow = fxEnabled_(rtState.tracks[track], current);
            UiIntent it{};
            it.type = UiIntentType::SetFxEnabled;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(current);
            it.value = enabledNow ? 0.0f : 1.0f;
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneFxTypeSelect: {
            const std::size_t totalTypes = fxTypeOptions_().size();
            if (totalTypes == 0U) {
                break;
            }
            const uint16_t current = clampFxType_(navState.selectedFxType);
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.selectedFxType = (current == 0U)
                                              ? static_cast<uint16_t>(totalTypes - 1U)
                                              : static_cast<uint16_t>(current - 1U);
            } else if (action.op == UiAction::Op::AdjustNext) {
                navState.selectedFxType = static_cast<uint16_t>((current + 1U) % totalTypes);
            }
        } break;

        case UiAction::Id::SceneAddFx: {
            if (totalTracks == 0U) {
                break;
            }
            const std::size_t totalTypes = fxTypeOptions_().size();
            if (totalTypes == 0U) {
                break;
            }
            const uint16_t currentType = clampFxType_(navState.selectedFxType);
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.selectedFxType = (currentType == 0U)
                                              ? static_cast<uint16_t>(totalTypes - 1U)
                                              : static_cast<uint16_t>(currentType - 1U);
                break;
            }
            if (action.op == UiAction::Op::AdjustNext) {
                navState.selectedFxType = static_cast<uint16_t>((currentType + 1U) % totalTypes);
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            const FxTypeOption& type = fxTypeOptions_()[clampFxType_(navState.selectedFxType)];
            UiIntent it{};
            it.type = UiIntentType::AddFxToTrack;
            it.track = track;
            it.path = std::string(type.id);
            pushIntent(std::move(it));
            navState.selectedFx = static_cast<uint16_t>(fxCount);
            navState.fxAddPopupOpen = false;
        } break;

        case UiAction::Id::SceneFxOpenEditor: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            const uint16_t current = clampFxCursor_(navState.selectedFx, fxCount);
            navState.selectedFx = current;
            if (current >= fxCount) {
                navState.fxAddPopupOpen = true;
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::OpenScene;
            it.scene = UiScene::FxEditor;
            it.resetSceneActionIndex = true;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(current);
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneFxRemove: {
            if (fxCount == 0U || navState.selectedFx >= fxCount || navState.fxAddPopupOpen) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            const uint16_t current = clampFx_(navState.selectedFx, fxCount);
            UiIntent it{};
            it.type = UiIntentType::RemoveFxFromTrack;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(current);
            pushIntent(std::move(it));

            // Локально поджимаем выделение под ожидаемое "после удаления".
            if (fxCount <= 1U) {
                navState.selectedFx = 0U;
            } else if (current >= fxCount - 1U) {
                navState.selectedFx = static_cast<uint16_t>(fxCount - 2U);
            } else {
                navState.selectedFx = current;
            }
        } break;

        case UiAction::Id::SceneFxBack: {
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            if (navState.fxAddPopupOpen) {
                navState.fxAddPopupOpen = false;
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::Back;
            it.scene = UiScene::Tracks;
            it.resetSceneActionIndex = true;
            pushIntent(std::move(it));
        } break;

        default:
            out.handled = false;
            break;
    }

    return out;
}

std::string FxListWidget::buildActionStatusLine_(const UiState& rtState, const UiNavState& navState) const {
    const UiActionCatalog catalog = queryAvailableActions(rtState, navState);
    if (catalog.actions.empty()) {
        return " action:- ";
    }

    const uint16_t selectedFxType = clampFxType_(navState.selectedFxType);
    const std::size_t idx = std::min<std::size_t>(catalog.currentIndex, catalog.actions.size() - 1U);
    const UiAction& a = catalog.actions[idx];
    char buf[196]{};

    switch (a.def.id) {
        case UiAction::Id::SceneFxSlotSelect:
            std::snprintf(buf,
                          sizeof(buf),
                          " action:%s = %u ",
                          a.def.label.c_str(),
                          static_cast<unsigned>(std::lround(a.state.value)));
            break;
        case UiAction::Id::SceneFxEnabled:
            std::snprintf(buf,
                          sizeof(buf),
                          " action:%s = %s ",
                          a.def.label.c_str(),
                          (a.state.value >= 0.5f) ? "ON" : "OFF");
            break;
        case UiAction::Id::SceneFxTypeSelect: {
            const FxTypeOption& type = fxTypeOptions_()[selectedFxType];
            std::snprintf(buf, sizeof(buf), " action:%s = %s ", a.def.label.c_str(), std::string(type.label).c_str());
        } break;
        case UiAction::Id::SceneAddFx: {
            const FxTypeOption& type = fxTypeOptions_()[selectedFxType];
            std::snprintf(buf, sizeof(buf), " action:%s = %s (apply) ",
                          a.def.label.c_str(),
                          std::string(type.label).c_str());
        } break;
        case UiAction::Id::SceneFxOpenEditor:
        case UiAction::Id::SceneFxRemove:
        case UiAction::Id::SceneFxBack:
            std::snprintf(buf, sizeof(buf), " action:%s (apply) ", a.def.label.c_str());
            break;
        default:
            std::snprintf(buf, sizeof(buf), " action:%s ", a.def.label.c_str());
            break;
    }
    return std::string(buf);
}

void FxListWidget::collectNodes_(const UiLayoutNode& root, std::vector<const UiLayoutNode*>& out) noexcept {
    out.push_back(&root);
    for (const UiLayoutNode& child : root.children) {
        collectNodes_(child, out);
    }
}

void FxListWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "fx_list") {
        return;
    }
    std::vector<const UiLayoutNode*> nodes{};
    collectNodes_(tpl.root, nodes);
    for (const UiLayoutNode* node : nodes) {
        if (!node) {
            continue;
        }
        if (node->type == UiLayoutNodeType::StatusBar && !node->text.empty()) {
            layout_.title = node->text;
        }
        if (node->type == UiLayoutNodeType::Text &&
            node->id == "keys_hint" &&
            !node->text.empty()) {
            layout_.keysHint = node->text;
        }
    }
    layout_.enabled = true;
}

} // namespace avantgarde
