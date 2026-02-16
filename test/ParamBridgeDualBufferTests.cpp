#include <catch2/catch_all.hpp>
#include <memory>
#include <vector>

#include "contracts/types.h"
#include "contracts/IParamBridge.h"
#include "contracts/IParameterized.h"
#include "runtime/ParamBridgeDualBuffer.cpp"

using namespace avantgarde;

// ----- Адекватный мок модуля, реализующий ВЕСЬ интерфейс IParameterized -----
struct MockModule final : IParameterized {
    explicit MockModule(std::size_t n, std::string name = "mock")
            : vals(n, 0.0f)
            , name_(std::move(name))
    {}

    std::size_t getParamCount() const override { return vals.size(); }

    float getParam(std::size_t index) const override {
        REQUIRE(index < vals.size()); // в проде — assert/UB по контракту
        return vals[index];
    }

    void setParam(std::size_t index, float value) override {
        REQUIRE(index < vals.size());
        // Контракт: значения нормализованы [0..1]
        REQUIRE(value >= 0.0f);
        REQUIRE(value <= 1.0f);
        vals[index] = value;
        writes.emplace_back(index, value);
    }

    const ParamMeta& getParamMeta(std::size_t index) const override {
        REQUIRE(index < vals.size());
        static ParamMeta meta; // простой статический экземпляр
        meta.name        = name_;
        meta.minValue    = 0.0f;
        meta.maxValue    = 1.0f;
        meta.logarithmic = false;
        meta.unit        = "%";
        return meta;
    }

    std::vector<float> vals;
    std::vector<std::pair<std::size_t,float>> writes;

private:
    std::string name_;
};

// ----- Простой граф: [trackId][slotId] → IParameterized* -----
struct MockGraph {
    MockGraph(int tracks, int slots, std::size_t paramsPerModule)
            : T(tracks), S(slots) {
        grid.resize(T);
        for (int t=0; t<T; ++t) {
            grid[t].reserve(S);
            for (int s=0; s<S; ++s) {
                grid[t].push_back(std::make_unique<MockModule>(paramsPerModule, "m"+std::to_string(t)+"_"+std::to_string(s)));
            }
        }
    }

    IParameterized* resolve(Target tg) noexcept {
        if (tg.trackId < 0 || tg.trackId >= T) return nullptr;
        if (tg.slotId  < 0 || tg.slotId  >= S) return nullptr;
        return grid[tg.trackId][tg.slotId].get();
    }

    MockModule* mod(Target tg) {
        return static_cast<MockModule*>(resolve(tg));
    }

    int T{}, S{};
    std::vector<std::vector<std::unique_ptr<MockModule>>> grid;
};

// Глобальный резолвер под сигнатуру ParamBridgeDualBuffer::ResolverFn
static MockGraph* g_graph = nullptr;
static IParameterized* GlobalResolver(Target tg) noexcept {
    return g_graph ? g_graph->resolve(tg) : nullptr;
}

// ----------------------------- Тесты -----------------------------

TEST_CASE("ParamBridge: basic control→RT apply via resolver") {
    MockGraph graph(2, 2, 8); g_graph = &graph;
    ParamBridgeDualBuffer bridge(/*capacityPerPage=*/32, /*resolver=*/&GlobalResolver);

    bridge.pushParam(Target{0,0}, /*index=*/3, 0.25f);
    bridge.pushParam(Target{1,1}, /*index=*/7, 1.20f); // кламп → 1.0

    bridge.swapBuffers(); // RT пролог

    auto* m00 = graph.mod(Target{0,0});
    auto* m11 = graph.mod(Target{1,1});

    REQUIRE(m00->getParam(3) == Catch::Approx(0.25f));
    REQUIRE(m11->getParam(7) == Catch::Approx(1.00f));

    // Параметр-мета возвращается и имеет корректные поля
    const ParamMeta& meta = m00->getParamMeta(3);
    REQUIRE(meta.minValue == Catch::Approx(0.0f));
    REQUIRE(meta.maxValue == Catch::Approx(1.0f));
    REQUIRE_FALSE(meta.logarithmic);
}

TEST_CASE("ParamBridge: clamping [-inf, +inf] → [0,1]") {
    MockGraph graph(1, 1, 4); g_graph = &graph;
    ParamBridgeDualBuffer bridge(8, &GlobalResolver);

    bridge.pushParam(Target{0,0}, 0, -10.0f);
    bridge.pushParam(Target{0,0}, 1, +10.0f);
    bridge.swapBuffers();

    auto* m = graph.mod(Target{0,0});
    REQUIRE(m->getParam(0) == Catch::Approx(0.0f));
    REQUIRE(m->getParam(1) == Catch::Approx(1.0f));
}

TEST_CASE("ParamBridge: last-wins within one page") {
    MockGraph graph(1, 1, 4); g_graph = &graph;
    ParamBridgeDualBuffer bridge(16, &GlobalResolver);

    bridge.pushParam(Target{0,0}, 2, 0.10f);
    bridge.pushParam(Target{0,0}, 2, 0.40f);
    bridge.pushParam(Target{0,0}, 2, 0.80f);

    bridge.swapBuffers();

    auto* m = graph.mod(Target{0,0});
    REQUIRE(m->getParam(2) == Catch::Approx(0.80f));
    REQUIRE_FALSE(m->writes.empty());
    REQUIRE(m->writes.back().first  == 2);
    REQUIRE(m->writes.back().second == Catch::Approx(0.80f));
}

TEST_CASE("ParamBridge: capacity overflow is safe and values normalized") {
    MockGraph graph(1, 1, 8); g_graph = &graph;
    ParamBridgeDualBuffer bridge(/*capacityPerPage=*/3, &GlobalResolver);

    // 5 апдейтов при ёмкости 3
    bridge.pushParam(Target{0,0}, 0, 0.10f);
    bridge.pushParam(Target{0,0}, 1, 0.20f);
    bridge.pushParam(Target{0,0}, 2, 0.30f);
    bridge.pushParam(Target{0,0}, 3, 0.40f); // переполнение
    bridge.pushParam(Target{0,0}, 1, 0.50f); // переполнение

    bridge.swapBuffers();

    auto* m = graph.mod(Target{0,0});
    // Какая именно политика переполнения — зафиксируй в CONTRACTS.md;
    // Здесь проверяем инварианты безопасности: значения в [0..1], модуль остаётся в валидном состоянии.
    for (std::size_t i = 0; i < m->getParamCount(); ++i) {
        const float v = m->getParam(i);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}

TEST_CASE("ParamBridge: repeated swap without new writes is idempotent (drain)") {
    ParamBridgeDualBuffer bridge(/*capacityPerPage=*/8, /*resolver=*/nullptr);

    bridge.pushParam(Target{0,0}, 0, 0.1f);
    bridge.pushParam(Target{0,0}, 1, 0.2f);

    bridge.swapBuffers();

    // вручную снимаем и применяем
    std::vector<ParamBridgeDualBuffer::Update> page(8);
    const std::size_t n = bridge.drainRead(page.data(), page.size());
    REQUIRE(n == 2);

    float p0 = 0.f, p1 = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        if (page[i].index == 0) p0 = page[i].value01;
        if (page[i].index == 1) p1 = page[i].value01;
    }
    REQUIRE(p0 == Catch::Approx(0.1f));
    REQUIRE(p1 == Catch::Approx(0.2f));

    // второй swap без новых push — no-op, содержимое read-страницы не меняется
    bridge.swapBuffers();
    const std::size_t n2 = bridge.drainRead(page.data(), page.size());
    REQUIRE(n2 == 2);
}
