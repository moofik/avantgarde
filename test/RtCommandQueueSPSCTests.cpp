// Catch2 v3: add to your test target: -D CATCH_CONFIG_ENABLE_BENCHMARKING (optional)
#include <catch2/catch_all.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include "contracts/IRtCommandQueue.h"
#include "runtime/RtCommandQueueSPSC.cpp"

using namespace avantgarde;

static RtCommand makeCmd(uint16_t id, int16_t track = 0, int16_t slot = 0,
                         uint16_t index = 0, float value = 0.f) {
    RtCommand c{};
    c.id = id; c.track = track; c.slot = slot; c.index = index; c.value = value;
    return c;
}

TEST_CASE("RtCommandQueueSPSC: basic push/pop") {
    RtCommandQueueSPSC q(8); // степень двойки

    REQUIRE(q.capacity() >= 8);
    REQUIRE(q.size() == 0);

    RtCommand in = makeCmd(1, 2, 3, 4, 0.5f);
    RtCommand out{};

    REQUIRE(q.push(in));
    REQUIRE(q.size() == 1);
    REQUIRE(q.pop(out));
    REQUIRE(q.size() == 0);

    CHECK(out.id == in.id);
    CHECK(out.track == in.track);
    CHECK(out.slot == in.slot);
    CHECK(out.index == in.index);
    CHECK(out.value == Catch::Approx(in.value));
}

TEST_CASE("RtCommandQueueSPSC: pop on empty returns false") {
    RtCommandQueueSPSC q(4);
    RtCommand out{};
    REQUIRE_FALSE(q.pop(out));
    REQUIRE(q.size() == 0);
}

TEST_CASE("RtCommandQueueSPSC: fill ring and overflow flag") {
    // У типичных SPSC одно место пустое: usable = capacity - 1
    RtCommandQueueSPSC q(8);
    const std::size_t usable = q.capacity() - 1;

    RtCommand dummy{};
    for (std::size_t i = 0; i < usable; ++i) {
    dummy = makeCmd(static_cast<uint16_t>(i));
    REQUIRE(q.push(dummy));
    }
    REQUIRE(q.size() == usable);

    // Следующий push должен отказать и поднять overflow-флаг
    REQUIRE_FALSE(q.push(makeCmd(999)));
    REQUIRE(q.overflowFlagAndReset());      // был overflow
    REQUIRE_FALSE(q.overflowFlagAndReset()); // теперь сброшен

    // Опустошаем
    RtCommand out{};
    std::size_t popped = 0;
    while (q.pop(out)) { ++popped; }
    REQUIRE(popped == usable);
    REQUIRE(q.size() == 0);
}

TEST_CASE("RtCommandQueueSPSC: clear empties and resets overflow flag") {
    RtCommandQueueSPSC q(8);

    // провоцируем overflow
    const std::size_t usable = q.capacity() - 1;
    for (std::size_t i = 0; i < usable; ++i) REQUIRE(q.push(makeCmd(1)));
    REQUIRE_FALSE(q.push(makeCmd(2)));
    REQUIRE(q.overflowFlagAndReset());

    q.clear();
    REQUIRE(q.size() == 0);
    REQUIRE_FALSE(q.overflowFlagAndReset());
    RtCommand out{};
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("RtCommandQueueSPSC: size matches push-pop counts") {
    RtCommandQueueSPSC q(16);
    for (int i = 0; i < 5; ++i) REQUIRE(q.push(makeCmd(static_cast<uint16_t>(i))));
    REQUIRE(q.size() == 5);

    RtCommand out{};
    REQUIRE(q.pop(out));
    REQUIRE(q.size() == 4);

    for (int i = 0; i < 3; ++i) REQUIRE(q.push(makeCmd(100 + i)));
    REQUIRE(q.size() == 7);

    while (q.pop(out)) {}
    REQUIRE(q.size() == 0);
}

TEST_CASE("RtCommandQueueSPSC: single-producer single-consumer stress") {
    RtCommandQueueSPSC q(1 << 12); // 4096
    constexpr int N = 100000;      // намеренно больше 65535, чтобы проверить wrap-around

    std::atomic<bool> start{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::thread producer([&](){
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < N; ++i) {
            RtCommand c{};
            c.id    = static_cast<uint16_t>(i & 0xFFFF);     // wrap каждые 65536
            c.track = static_cast<int16_t>(i & 0x7FFF);
            // ждём место без сна (SPSC)
            while (!q.push(c)) { /* busy spin */ }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&](){
        while (!start.load(std::memory_order_acquire)) {}
        RtCommand c{};
        // ожидаем, что id идёт строго по FIFO с учётом wrap
        uint16_t expected = 0; // первый будет 0
        while (consumed.load(std::memory_order_relaxed) < N) {
            if (q.pop(c)) {
                REQUIRE(c.id == expected);
                expected = static_cast<uint16_t>(expected + 1); // корректно оборачивается
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    REQUIRE(produced.load() == N);
    REQUIRE(consumed.load() == N);

    // Т.к. продюсер мог несколько раз попасть в полный буфер,
    // overflow-флаг – это телеметрия, а не провал данных.
    // Поэтому просто сбрасываем его и НЕ делаем REQUIRE_FALSE.
    (void)q.overflowFlagAndReset();

    REQUIRE(q.size() == 0);
}

TEST_CASE("RtCommandQueueSPSC: capacity normalized to power-of-two") {
    // Если реализация нормализует, ёмкость должна стать степенью двойки ≥ запрошенного
    RtCommandQueueSPSC q1(7);
    auto cap1 = q1.capacity();
    // простая проверка степени двойки
    auto isPow2 = [](std::size_t x){ return x && ((x & (x - 1)) == 0); };
    REQUIRE(isPow2(cap1));
    REQUIRE(cap1 >= 7);

    RtCommandQueueSPSC q2(1024);
    REQUIRE(q2.capacity() == 1024);
}
