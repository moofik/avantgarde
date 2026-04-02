#include "service/ui/layout/UiLayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace avantgarde {
namespace {

struct Size2 {
    uint16_t w{0};
    uint16_t h{0};
};

bool isColumn(const UiLayoutNode& node) noexcept {
    return node.type == UiLayoutNodeType::Column ||
           node.type == UiLayoutNodeType::TrackView ||
           node.type == UiLayoutNodeType::ManagerView ||
           node.type == UiLayoutNodeType::FxListView ||
           node.type == UiLayoutNodeType::FxEditorView;
}

bool isRow(const UiLayoutNode& node) noexcept {
    return node.type == UiLayoutNodeType::Row;
}

bool isContainer(const UiLayoutNode& node) noexcept {
    return isColumn(node) || isRow(node);
}

uint16_t clampU16(int value) noexcept {
    if (value <= 0) {
        return 0;
    }
    if (value > 65535) {
        return 65535;
    }
    return static_cast<uint16_t>(value);
}

uint16_t resolveSize(const UiLayoutSize& size,
                    uint16_t available,
                    uint16_t autoValue) noexcept {
    switch (size.unit) {
        case UiLayoutSize::Unit::Px:
            return clampU16(static_cast<int>(std::lround(size.value)));
        case UiLayoutSize::Unit::Percent:
            return clampU16(static_cast<int>(std::lround(
                static_cast<float>(available) * std::clamp(size.value, 0.0f, 100.0f) / 100.0f)));
        case UiLayoutSize::Unit::Auto:
        default:
            return autoValue;
    }
}

Size2 defaultLeafMetrics(const UiLayoutNode& node) {
    switch (node.type) {
        case UiLayoutNodeType::StatusBar:
        case UiLayoutNodeType::Text:
            return Size2{1, 1};
        case UiLayoutNodeType::TrackView:
        case UiLayoutNodeType::ManagerView:
        case UiLayoutNodeType::FxListView:
        case UiLayoutNodeType::FxEditorView:
            return Size2{0, 1};
        case UiLayoutNodeType::List:
            return Size2{0, 1};
        case UiLayoutNodeType::Separator:
            return Size2{0, 1};
        case UiLayoutNodeType::Knob:
            return Size2{18, 1};
        case UiLayoutNodeType::AnimSlot: {
            uint16_t w = 20;
            uint16_t h = 6;
            if (node.width.unit == UiLayoutSize::Unit::Px && node.width.value > 0.0f) {
                w = clampU16(static_cast<int>(std::lround(node.width.value)));
            }
            if (node.height.unit == UiLayoutSize::Unit::Px && node.height.value > 0.0f) {
                h = clampU16(static_cast<int>(std::lround(node.height.value)));
            }
            return Size2{w, h};
        }
        case UiLayoutNodeType::Spacer:
            return Size2{0, 1};
        case UiLayoutNodeType::Column:
        case UiLayoutNodeType::Row:
        case UiLayoutNodeType::Unknown:
        default:
            return Size2{0, 0};
    }
}

Size2 measureNode(const UiLayoutNode& node,
                  uint16_t availableW,
                  uint16_t availableH,
                  const UiLayoutEngine::MeasureFn& customMeasure,
                  std::unordered_map<const UiLayoutNode*, Size2>& cache) {
    auto itCached = cache.find(&node);
    if (itCached != cache.end()) {
        return itCached->second;
    }

    const uint16_t padding = node.padding;
    const uint16_t contentAvailW = (availableW > padding * 2U) ? static_cast<uint16_t>(availableW - padding * 2U) : 0U;
    const uint16_t contentAvailH = (availableH > padding * 2U) ? static_cast<uint16_t>(availableH - padding * 2U) : 0U;

    Size2 intrinsic{};
    if (isContainer(node)) {
        if (isColumn(node)) {
            uint16_t childMainSum = 0;
            uint16_t childCrossMax = 0;
            for (const UiLayoutNode& child : node.children) {
                const Size2 childSize = measureNode(child, contentAvailW, contentAvailH, customMeasure, cache);
                childMainSum = static_cast<uint16_t>(childMainSum + childSize.h);
                childCrossMax = std::max<uint16_t>(childCrossMax, childSize.w);
            }
            const uint16_t gaps = (node.children.size() > 1U)
                                      ? static_cast<uint16_t>((node.children.size() - 1U) * node.gap)
                                      : 0U;
            intrinsic.w = childCrossMax;
            intrinsic.h = static_cast<uint16_t>(childMainSum + gaps);
        } else {
            // Row с переносом: если очередной child не влезает по ширине, начинаем
            // новую строку и накапливаем высоту всех строк.
            uint16_t lineW = 0;
            uint16_t lineH = 0;
            uint16_t maxLineW = 0;
            uint16_t totalH = 0;
            bool hasLine = false;

            for (const UiLayoutNode& child : node.children) {
                const Size2 childSize = measureNode(child, contentAvailW, contentAvailH, customMeasure, cache);
                const uint16_t childW = childSize.w;
                const uint16_t childH = childSize.h;
                const uint16_t addGap = hasLine ? node.gap : 0U;
                const uint16_t candidateW = static_cast<uint16_t>(lineW + addGap + childW);

                if (hasLine &&
                    contentAvailW > 0U &&
                    candidateW > contentAvailW) {
                    maxLineW = std::max<uint16_t>(maxLineW, lineW);
                    totalH = static_cast<uint16_t>(totalH + lineH + node.gap);
                    lineW = childW;
                    lineH = childH;
                    hasLine = true;
                } else {
                    lineW = candidateW;
                    lineH = std::max<uint16_t>(lineH, childH);
                    hasLine = true;
                }
            }

            if (hasLine) {
                maxLineW = std::max<uint16_t>(maxLineW, lineW);
                totalH = static_cast<uint16_t>(totalH + lineH);
            }
            intrinsic.w = maxLineW;
            intrinsic.h = totalH;
        }
    } else {
        if (customMeasure) {
            const UiLayoutEngine::NodeMetrics custom = customMeasure(node);
            intrinsic.w = custom.minWidth;
            intrinsic.h = custom.minHeight;
        } else {
            intrinsic = defaultLeafMetrics(node);
        }
    }

    const uint16_t baseW = static_cast<uint16_t>(intrinsic.w + padding * 2U);
    const uint16_t baseH = static_cast<uint16_t>(intrinsic.h + padding * 2U);

    Size2 result{};
    result.w = resolveSize(node.width, availableW, baseW);
    result.h = resolveSize(node.height, availableH, baseH);

    cache.emplace(&node, result);
    return result;
}

void arrangeNode(const UiLayoutNode& node,
                 const SceneFrameRect& rect,
                 uint16_t depth,
                 const std::unordered_map<const UiLayoutNode*, Size2>& measured,
                 UiLayoutEngine::Result& out) {
    out.boxes.push_back(UiLayoutBox{.node = &node, .rect = rect, .depth = depth});

    if (!isContainer(node) || node.children.empty()) {
        return;
    }

    const uint16_t padding = node.padding;
    const int contentX = rect.x + static_cast<int>(padding);
    const int contentY = rect.y + static_cast<int>(padding);
    const uint16_t contentW = (rect.width > padding * 2U) ? static_cast<uint16_t>(rect.width - padding * 2U) : 0U;
    const uint16_t contentH = (rect.height > padding * 2U) ? static_cast<uint16_t>(rect.height - padding * 2U) : 0U;

    if (contentW == 0U || contentH == 0U) {
        return;
    }

    const bool vertical = isColumn(node);
    const bool rowWrap = !vertical;
    const uint16_t gap = node.gap;
    const std::size_t n = node.children.size();
    const int gapTotal = (n > 1U) ? static_cast<int>((n - 1U) * gap) : 0;
    const int mainAvailable = std::max<int>(0, static_cast<int>(vertical ? contentH : contentW) - gapTotal);

    struct ChildGeometry {
        uint16_t main{0};
        uint16_t cross{0};
        bool mainAuto{false};
    };
    std::vector<ChildGeometry> geoms(n);

    int mainUsed = 0;
    std::size_t autoCount = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const UiLayoutNode& child = node.children[i];
        const auto it = measured.find(&child);
        const Size2 minSize = (it != measured.end()) ? it->second : Size2{};

        const uint16_t minMain = vertical ? minSize.h : minSize.w;
        const uint16_t minCross = vertical ? minSize.w : minSize.h;

        const UiLayoutSize& mainRule = vertical ? child.height : child.width;
        const UiLayoutSize& crossRule = vertical ? child.width : child.height;

        const bool mainAuto = (mainRule.unit == UiLayoutSize::Unit::Auto);
        const bool mainAutoFlex = mainAuto &&
            (child.type == UiLayoutNodeType::Spacer ||
             child.type == UiLayoutNodeType::List ||
             child.type == UiLayoutNodeType::TrackView ||
             child.type == UiLayoutNodeType::ManagerView ||
             child.type == UiLayoutNodeType::FxListView ||
             child.type == UiLayoutNodeType::FxEditorView ||
             child.type == UiLayoutNodeType::Column ||
             child.type == UiLayoutNodeType::Row);
        uint16_t main = resolveSize(mainRule, static_cast<uint16_t>(mainAvailable), minMain);
        uint16_t cross = resolveSize(crossRule,
                                     vertical ? contentW : contentH,
                                     minCross);

        if (crossRule.unit == UiLayoutSize::Unit::Auto) {
            if (vertical && (child.type == UiLayoutNodeType::Text ||
                             child.type == UiLayoutNodeType::StatusBar ||
                             child.type == UiLayoutNodeType::TrackView ||
                             child.type == UiLayoutNodeType::ManagerView ||
                             child.type == UiLayoutNodeType::FxListView ||
                             child.type == UiLayoutNodeType::FxEditorView ||
                             child.type == UiLayoutNodeType::List ||
                             child.type == UiLayoutNodeType::Separator ||
                             child.type == UiLayoutNodeType::Spacer)) {
                cross = contentW;
            }
            if (!vertical && (child.type == UiLayoutNodeType::Text ||
                              child.type == UiLayoutNodeType::StatusBar ||
                              child.type == UiLayoutNodeType::TrackView ||
                              child.type == UiLayoutNodeType::ManagerView ||
                              child.type == UiLayoutNodeType::FxListView ||
                              child.type == UiLayoutNodeType::FxEditorView ||
                              child.type == UiLayoutNodeType::List ||
                              child.type == UiLayoutNodeType::Separator ||
                              child.type == UiLayoutNodeType::Spacer)) {
                cross = contentH;
            }
        }

        if (vertical) {
            cross = std::min<uint16_t>(cross, contentW);
        } else {
            cross = std::min<uint16_t>(cross, contentH);
        }

        geoms[i].main = main;
        geoms[i].cross = cross;
        geoms[i].mainAuto = mainAutoFlex;
        mainUsed += main;
        if (mainAutoFlex) {
            ++autoCount;
        }
    }

    int leftover = mainAvailable - mainUsed;
    if (!rowWrap && leftover > 0 && autoCount > 0U) {
        int extraEach = leftover / static_cast<int>(autoCount);
        int rest = leftover % static_cast<int>(autoCount);
        for (ChildGeometry& g : geoms) {
            if (!g.mainAuto) {
                continue;
            }
            g.main = clampU16(static_cast<int>(g.main) + extraEach + (rest > 0 ? 1 : 0));
            if (rest > 0) {
                --rest;
            }
        }
    }

    if (vertical) {
        int cursorMain = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const ChildGeometry& g = geoms[i];
            const int childX = contentX;
            const int childY = contentY + cursorMain;
            const uint16_t childW = g.cross;
            const uint16_t childH = g.main;

            if (childW > 0U && childH > 0U) {
                const SceneFrameRect childRect{
                    .x = static_cast<int16_t>(childX),
                    .y = static_cast<int16_t>(childY),
                    .width = childW,
                    .height = childH,
                };
                arrangeNode(node.children[i], childRect, static_cast<uint16_t>(depth + 1U), measured, out);
            }

            cursorMain += static_cast<int>(g.main);
            if (i + 1U < n) {
                cursorMain += gap;
            }
        }
    } else {
        // Row: поддерживаем перенос child-блоков по ширине contentW.
        int cursorX = 0;
        int cursorY = 0;
        int lineHeight = 0;

        for (std::size_t i = 0; i < n; ++i) {
            const ChildGeometry& g = geoms[i];
            const int childW = static_cast<int>(g.main);
            const int childH = static_cast<int>(g.cross);
            if (childW <= 0 || childH <= 0) {
                continue;
            }

            if (cursorX > 0 &&
                childW > 0 &&
                (cursorX + childW) > static_cast<int>(contentW)) {
                cursorX = 0;
                cursorY += lineHeight + gap;
                lineHeight = 0;
            }

            const SceneFrameRect childRect{
                .x = static_cast<int16_t>(contentX + cursorX),
                .y = static_cast<int16_t>(contentY + cursorY),
                .width = static_cast<uint16_t>(childW),
                .height = static_cast<uint16_t>(childH),
            };
            arrangeNode(node.children[i], childRect, static_cast<uint16_t>(depth + 1U), measured, out);

            cursorX += childW;
            if (i + 1U < n) {
                cursorX += gap;
            }
            lineHeight = std::max(lineHeight, childH);
        }
    }
}

} // namespace

UiLayoutEngine::Result UiLayoutEngine::arrange(const UiLayoutNode& root,
                                               uint16_t width,
                                               uint16_t height,
                                               const MeasureFn& measure) {
    Result result{};
    std::unordered_map<const UiLayoutNode*, Size2> measured{};
    (void)measureNode(root, width, height, measure, measured);

    const SceneFrameRect rootRect{
        .x = 0,
        .y = 0,
        .width = width,
        .height = height,
    };
    arrangeNode(root, rootRect, 0U, measured, result);
    return result;
}

const UiLayoutBox* UiLayoutEngine::findById(const Result& result, std::string_view id) noexcept {
    if (id.empty()) {
        return nullptr;
    }
    for (const UiLayoutBox& box : result.boxes) {
        if (box.node && box.node->id == id) {
            return &box;
        }
    }
    return nullptr;
}

std::vector<const UiLayoutBox*> UiLayoutEngine::findByType(const Result& result, UiLayoutNodeType type) {
    std::vector<const UiLayoutBox*> out;
    for (const UiLayoutBox& box : result.boxes) {
        if (box.node && box.node->type == type) {
            out.push_back(&box);
        }
    }
    return out;
}

} // namespace avantgarde
