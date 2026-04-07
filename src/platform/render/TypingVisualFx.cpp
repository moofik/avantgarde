#include "platform/render/TypingVisualFx.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

namespace avantgarde {
namespace {

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

std::string TypingVisualFx::buildStateKey_(const VisualFxRequest& request) const {
    if (!request.instanceKey.empty()) {
        return request.instanceKey;
    }
    if (!request.nodeId.empty()) {
        return request.nodeId;
    }
    if (!request.nodeText.empty()) {
        return request.nodeText;
    }
    return "__fx_typing__";
}

uint32_t TypingVisualFx::computeDurationMs_(const VisualFxRequest& request, float speed) const {
    // Базово ~12 символов/сек при speed=1.0.
    const float cps = std::clamp(12.0f * speed, 1.0f, 96.0f);
    const std::size_t glyphCount = std::max<std::size_t>(1U, request.nodeText.size());
    const float ms = static_cast<float>(glyphCount) * (1000.0f / cps);
    return static_cast<uint32_t>(std::clamp<float>(ms, 80.0f, 12000.0f));
}

VisualFxBlockStyle TypingVisualFx::resolve(const VisualFxRequest& request) {
    VisualFxBlockStyle style{};
    TypingVisualFxPayload payload{};
    const std::string key = buildStateKey_(request);
    const std::string trigger = toLowerAscii(request.effectTrigger);

    float speed = request.effectSpeed;
    if (speed <= 0.0f) {
        speed = 1.0f;
    }
    speed = std::clamp(speed, 0.05f, 8.0f);
    const uint32_t durationMs = computeDurationMs_(request, speed);

    bool active = false;
    uint64_t elapsedMs = 0U;

    if (trigger == "change") {
        if (!request.hasValue01) {
            return style;
        }
        const float value01 = std::clamp(request.value01, 0.0f, 1.0f);
        const auto it = lastValue01_.find(key);
        if (it == lastValue01_.end()) {
            // Baseline: без автозапуска.
            lastValue01_[key] = value01;
            return style;
        }
        const bool changed = (std::fabs(it->second - value01) > 0.0005f);
        it->second = value01;
        if (changed) {
            startMs_[key] = request.nowMs;
        }
        const auto st = startMs_.find(key);
        if (st == startMs_.end()) {
            return style;
        }
        elapsedMs = (request.nowMs >= st->second) ? (request.nowMs - st->second) : 0ULL;
        active = (elapsedMs < durationMs);
    } else if (trigger == "time") {
        uint32_t intervalMs = (request.effectIntervalMs > 0U) ? request.effectIntervalMs : 2600U;
        intervalMs = std::max<uint32_t>(200U, intervalMs);
        const uint64_t interval = static_cast<uint64_t>(intervalMs);

        auto [epochIt, inserted] = timeEpochMs_.emplace(key, request.nowMs);
        if (inserted) {
            epochIt->second = request.nowMs;
            return style;
        }
        const uint64_t elapsed =
            (request.nowMs >= epochIt->second) ? (request.nowMs - epochIt->second) : 0ULL;
        if (elapsed < interval) {
            return style;
        }
        const uint64_t phase = (elapsed - interval) % interval;
        elapsedMs = phase;
        active = (elapsedMs < durationMs);
    } else {
        // always/empty: стартуем один раз и печатаем до конца.
        auto [st, inserted] = startMs_.emplace(key, request.nowMs);
        if (inserted) {
            st->second = request.nowMs;
        }
        elapsedMs = (request.nowMs >= st->second) ? (request.nowMs - st->second) : 0ULL;
        active = (elapsedMs < durationMs);
    }

    if (!active) {
        return style;
    }

    payload.durationMs = durationMs;
    payload.phase01 = std::clamp<float>(
        static_cast<float>(elapsedMs) / static_cast<float>(std::max<uint32_t>(1U, durationMs)),
        0.0f,
        1.0f);
    payload.reveal01 = payload.phase01;
    style.active = true;
    style.payload = payload;
    return style;
}

bool TypingVisualFx::applyRgba(VisualFxRgbaView& view, const VisualFxRequest& request) {
    if (!view.pixels || view.width == 0U || view.height == 0U || view.strideBytes < 4U) {
        return false;
    }
    const VisualFxBlockStyle style = resolve(request);
    if (!style.active) {
        return false;
    }
    const auto* typing = std::get_if<TypingVisualFxPayload>(&style.payload);
    if (!typing) {
        return false;
    }

    const float reveal01 = std::clamp(typing->reveal01, 0.0f, 1.0f);
    if (reveal01 >= 0.999f) {
        return false;
    }

    const std::size_t w = view.width;
    const std::size_t h = view.height;
    const std::size_t n = w * h;
    std::vector<int32_t> labels(n, -1);
    struct Component {
        int minX{INT32_MAX};
        int maxX{INT32_MIN};
        uint32_t pixelCount{0U};
        std::vector<std::size_t> pixels{};
    };
    std::vector<Component> comps{};

    auto idxOf = [&](int x, int y) -> std::size_t {
        return static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
    };
    auto alphaAt = [&](int x, int y) -> uint8_t {
        const std::size_t p =
            static_cast<std::size_t>(y) * view.strideBytes + static_cast<std::size_t>(x) * 4U + 3U;
        return view.pixels[p];
    };

    for (int y = 0; y < static_cast<int>(h); ++y) {
        for (int x = 0; x < static_cast<int>(w); ++x) {
            if (alphaAt(x, y) <= 8U) {
                continue;
            }
            const std::size_t seed = idxOf(x, y);
            if (labels[seed] >= 0) {
                continue;
            }
            const int32_t id = static_cast<int32_t>(comps.size());
            comps.push_back(Component{});
            Component& c = comps.back();
            std::queue<std::pair<int, int>> q;
            q.push({x, y});
            labels[seed] = id;
            while (!q.empty()) {
                const auto [cx, cy] = q.front();
                q.pop();
                const std::size_t pi = idxOf(cx, cy);
                c.pixels.push_back(pi);
                c.pixelCount += 1U;
                c.minX = std::min(c.minX, cx);
                c.maxX = std::max(c.maxX, cx);

                const int nx[4] = {cx - 1, cx + 1, cx, cx};
                const int ny[4] = {cy, cy, cy - 1, cy + 1};
                for (int k = 0; k < 4; ++k) {
                    const int xx = nx[k];
                    const int yy = ny[k];
                    if (xx < 0 || xx >= static_cast<int>(w) || yy < 0 || yy >= static_cast<int>(h)) {
                        continue;
                    }
                    if (alphaAt(xx, yy) <= 8U) {
                        continue;
                    }
                    const std::size_t ni = idxOf(xx, yy);
                    if (labels[ni] >= 0) {
                        continue;
                    }
                    labels[ni] = id;
                    q.push({xx, yy});
                }
            }
        }
    }

    if (comps.empty()) {
        return false;
    }

    // Группируем компоненты в "буквы" по соседству по X.
    std::vector<int32_t> order(comps.size(), 0);
    for (std::size_t i = 0; i < comps.size(); ++i) {
        order[i] = static_cast<int32_t>(i);
    }
    std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
        return comps[static_cast<std::size_t>(a)].minX < comps[static_cast<std::size_t>(b)].minX;
    });
    struct GlyphGroup {
        int minX{INT32_MAX};
        int maxX{INT32_MIN};
        std::vector<int32_t> compIds{};
    };
    std::vector<GlyphGroup> groups{};
    for (int32_t cid : order) {
        const Component& c = comps[static_cast<std::size_t>(cid)];
        if (groups.empty()) {
            groups.push_back(GlyphGroup{c.minX, c.maxX, {cid}});
            continue;
        }
        GlyphGroup& g = groups.back();
        // Если компонент пересекается/почти примыкает по X — считаем частью одной буквы.
        if (c.minX <= g.maxX + 1) {
            g.maxX = std::max(g.maxX, c.maxX);
            g.compIds.push_back(cid);
        } else {
            groups.push_back(GlyphGroup{c.minX, c.maxX, {cid}});
        }
    }

    const std::size_t groupCount = groups.size();
    const std::size_t visibleGroups = std::min<std::size_t>(
        groupCount,
        static_cast<std::size_t>(std::floor(static_cast<double>(groupCount) *
                                            static_cast<double>(reveal01) + 1e-6)));

    bool modified = false;
    for (std::size_t gi = visibleGroups; gi < groupCount; ++gi) {
        for (int32_t cid : groups[gi].compIds) {
            const Component& c = comps[static_cast<std::size_t>(cid)];
            for (std::size_t pi : c.pixels) {
                const uint16_t y = static_cast<uint16_t>(pi / w);
                const uint16_t x = static_cast<uint16_t>(pi % w);
                const std::size_t idx = static_cast<std::size_t>(y) * view.strideBytes +
                                        static_cast<std::size_t>(x) * 4U;
                if (view.pixels[idx + 3U] == 0U) {
                    continue;
                }
                view.pixels[idx + 0U] = 0U;
                view.pixels[idx + 1U] = 0U;
                view.pixels[idx + 2U] = 0U;
                view.pixels[idx + 3U] = 0U;
                modified = true;
            }
        }
    }
    return modified;
}

} // namespace avantgarde
