#include "service/ui/FxListWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "contracts/FxRegistry.h"

namespace avantgarde {
namespace {

constexpr const char* kFrameTop = "╔";
constexpr const char* kFrameTopRight = "╗";
constexpr const char* kFrameMid = "╠";
constexpr const char* kFrameMidRight = "╣";
constexpr const char* kFrameBottom = "╚";
constexpr const char* kFrameBottomRight = "╝";
constexpr const char* kFrameVert = "║";
constexpr const char* kFrameH = "═";

std::string repeatToken(std::string_view token, std::size_t count) {
    std::string out;
    out.reserve(token.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += token;
    }
    return out;
}

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

std::string padRight(const std::string& s, std::size_t width) {
    if (s.size() >= width) {
        return s.substr(0, width);
    }
    std::string out = s;
    out.append(width - s.size(), ' ');
    return out;
}

} // namespace

FxListWidget::FxListWidget(uint16_t frameWidth) noexcept
    : frameWidth_(frameWidth) {}

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

void FxListWidget::render(UiTextBuffer& out, const UiState& rtState, const UiNavState& navState) {
    const std::size_t width = frameWidth_ < 28 ? 28 : frameWidth_;
    const std::size_t inner = width - 2U;
    const std::size_t listWidth = inner > 6 ? inner - 6 : inner;
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const UiTrackStateView* tr = rtState.tracks.empty() ? nullptr : &rtState.tracks[track];
    const std::size_t fxCount = tr ? tr->fxCount : 0U;
    const uint16_t selectedFx = clampFx_(navState.selectedFx, fxCount);
    const std::size_t start = (fxCount > listRows_ && selectedFx >= listRows_)
                                  ? static_cast<std::size_t>(selectedFx + 1U - listRows_)
                                  : 0U;

    out.clear();
    out.lines.reserve(static_cast<std::size_t>(listRows_) + 10U);

    out.lines.push_back(std::string(kFrameTop) + repeatToken(kFrameH, inner) + kFrameTopRight);
    {
        char title[128]{};
        std::snprintf(title,
                      sizeof(title),
                      " FX LIST T%u  slots:%u ",
                      static_cast<unsigned>(track + 1U),
                      static_cast<unsigned>(fxCount));
        out.lines.push_back(std::string(kFrameVert) + padRight(title, inner) + kFrameVert);
    }
    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);

    if (fxCount == 0) {
        out.lines.push_back(std::string(kFrameVert) + padRight(" (empty) add effect via action: Add FX ", inner) + kFrameVert);
        for (std::size_t i = 1; i < listRows_; ++i) {
            out.lines.push_back(std::string(kFrameVert) + padRight(" ", inner) + kFrameVert);
        }
    } else {
        for (std::size_t row = 0; row < listRows_; ++row) {
            const std::size_t idx = start + row;
            std::string line = " ";
            if (idx < fxCount) {
                const bool selected = (idx == selectedFx);
                line += selected ? "> " : "  ";
                line += "S";
                line += std::to_string(static_cast<unsigned>(idx + 1U));
                line += " ";
                line += trimMiddle(fxName_(*tr, static_cast<uint16_t>(idx)), listWidth);
            }
            out.lines.push_back(std::string(kFrameVert) + padRight(line, inner) + kFrameVert);
        }
    }

    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);
    out.lines.push_back(std::string(kFrameVert) + padRight(buildActionStatusLine_(rtState, navState), inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameVert) + padRight(" keys [j/k slot] [;/' focus] [/? adj] [o apply] [esc] ", inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameBottom) + repeatToken(kFrameH, inner) + kFrameBottomRight);
}

WidgetOutput FxListWidget::onGesture(UiGesture action, const UiState& rtState, UiNavState& navState) {
    WidgetOutput out{};
    const uint8_t track = clampTrack_(navState.selectedTrack, rtState.tracks.size());
    const std::size_t fxCount = (rtState.tracks.empty()) ? 0U : rtState.tracks[track].fxCount;

    if (action == UiGesture::ListUp && fxCount > 0) {
        if (navState.selectedFx == 0U) {
            navState.selectedFx = static_cast<uint16_t>(fxCount - 1U);
        } else {
            navState.selectedFx = static_cast<uint16_t>(navState.selectedFx - 1U);
        }
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListDown && fxCount > 0) {
        navState.selectedFx = static_cast<uint16_t>((clampFx_(navState.selectedFx, fxCount) + 1U) % fxCount);
        out.handled = true;
        return out;
    }
    if (action == UiGesture::ListEnter && fxCount > 0) {
        navState.scene = UiScene::FxEditor;
        navState.selectedFx = clampFx_(navState.selectedFx, fxCount);
        navState.sceneActionIndex = 0;
        UiIntent it{};
        it.type = UiIntentType::OpenFxEditor;
        it.track = track;
        it.fxSlot = static_cast<uint8_t>(navState.selectedFx);
        out.handled = true;
        out.intents.push_back(std::move(it));
        return out;
    }
    if (action == UiGesture::ListParent) {
        navState.scene = UiScene::Tracks;
        navState.sceneActionIndex = 0;
        UiIntent it{};
        it.type = UiIntentType::Back;
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
    const uint16_t selectedFx = clampFx_(navState.selectedFx, fxCount);

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
        a.def.maxValue = static_cast<float>(std::max<std::size_t>(1U, fxCount));
        a.def.step = 1.0f;
        a.state.enabled = (fxCount > 0U);
        a.state.value = static_cast<float>(selectedFx + 1U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneAddReverb;
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
        a.state.enabled = (fxCount > 0U);
        push(std::move(a));
    }
    {
        UiAction a{};
        a.def.id = UiAction::Id::SceneFxRemove;
        a.def.scope = UiAction::Scope::Scene;
        a.def.execution = UiAction::Execution::ApplyRequired;
        a.def.valueKind = UiAction::ValueKind::None;
        a.def.label = "Remove FX";
        a.state.enabled = (fxCount > 0U);
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
            if (fxCount == 0U) {
                break;
            }
            const uint16_t current = clampFx_(navState.selectedFx, fxCount);
            if (action.op == UiAction::Op::AdjustPrev) {
                navState.selectedFx = (current == 0U) ? static_cast<uint16_t>(fxCount - 1U) : static_cast<uint16_t>(current - 1U);
            } else if (action.op == UiAction::Op::AdjustNext) {
                navState.selectedFx = static_cast<uint16_t>((current + 1U) % fxCount);
            } else if (action.op == UiAction::Op::Apply ||
                       action.op == UiAction::Op::Press) {
                navState.selectedFx = current;
                navState.scene = UiScene::FxEditor;
                navState.sceneActionIndex = 0;
                UiIntent it{};
                it.type = UiIntentType::OpenFxEditor;
                it.track = track;
                it.fxSlot = static_cast<uint8_t>(current);
                pushIntent(std::move(it));
            }
        } break;

        case UiAction::Id::SceneAddReverb: {
            if (totalTracks == 0U) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            UiIntent it{};
            it.type = UiIntentType::AddFxToTrack;
            it.track = track;
            it.path = "fx.reverb.schroeder";
            pushIntent(std::move(it));
            navState.selectedFx = static_cast<uint16_t>(fxCount);
        } break;

        case UiAction::Id::SceneFxOpenEditor: {
            if (fxCount == 0U) {
                break;
            }
            if (action.op != UiAction::Op::Apply &&
                action.op != UiAction::Op::Press) {
                break;
            }
            navState.selectedFx = clampFx_(navState.selectedFx, fxCount);
            navState.scene = UiScene::FxEditor;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::OpenFxEditor;
            it.track = track;
            it.fxSlot = static_cast<uint8_t>(navState.selectedFx);
            pushIntent(std::move(it));
        } break;

        case UiAction::Id::SceneFxRemove: {
            if (fxCount == 0U) {
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
            navState.scene = UiScene::Tracks;
            navState.sceneActionIndex = 0;
            UiIntent it{};
            it.type = UiIntentType::Back;
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
        case UiAction::Id::SceneAddReverb:
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

} // namespace avantgarde
