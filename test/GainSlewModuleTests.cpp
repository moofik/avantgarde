// tests/GainSlewModule.tests.cpp
#include "contracts/IAudioModule.h"
#include "module/GainSlewModule.cpp"
#include <catch2/catch_all.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace avantgarde;

TEST_CASE("GainSlew: pass-through при постоянном gain=1", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod; // по умолчанию PerBlocks, blocks=1
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);

    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();   // const float** указывает на ЖИВУЮ std::array
    ctx.out     = outPtrs.data();  // float**      указывает на ЖИВУЮ std::array
    ctx.nframes = N;

    // Заполняем тестовой синусоидой
    for (std::size_t i = 0; i < N; ++i) {
        float s = std::sin(2.f * float(M_PI) * (float)i / 32.f);
        inL[i] = s;
        inR[i] = s * 0.5f;
    }

    // gain=1
    mod.setParam(GainSlewModule::P_GAIN, 1.0f);
    mod.beginBlock();      // фиксируем таргет на блок
    mod.process(ctx);

    for (std::size_t i = 0; i < N; ++i) {
        REQUIRE(outL[i] == Catch::Approx(inL[i]).margin(1e-6f));
        REQUIRE(outR[i] == Catch::Approx(inR[i]).margin(1e-6f));
    }
}

TEST_CASE("GainSlew: тишина при постоянном gain=0", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod;
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();
    ctx.out     = outPtrs.data();
    ctx.nframes = N;

    // любой ненулевой вход
    for (std::size_t i = 0; i < N; ++i) { inL[i] = (i&1)? 0.3f : -0.7f; inR[i] = 0.5f * inL[i]; }

    // Блок 1: ставим цель 0.0, даём блоку отработать рампу 1→0
    mod.setParam(GainSlewModule::P_GAIN, 0.0f);
    mod.beginBlock();
    mod.process(ctx);

    // Блок 2: цель прежняя (0.0), тут уже должно быть тихо на всём интервале
    std::fill(outL.begin(), outL.end(), 1.f);
    std::fill(outR.begin(), outR.end(), 1.f);
    mod.beginBlock();
    mod.process(ctx);

    for (std::size_t i = 0; i < N; ++i) {
        REQUIRE(outL[i] == Catch::Approx(0.f).margin(1e-6f));
        REQUIRE(outR[i] == Catch::Approx(0.f).margin(1e-6f));
    }
}

TEST_CASE("GainSlew: линейная рампа за 1 блок (PerBlocks, blocks=1)", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod(GainSlewModule::SlewMode::PerBlocks, /*blocks*/1);
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);

    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();   // const float** указывает на ЖИВУЮ std::array
    ctx.out     = outPtrs.data();  // float**      указывает на ЖИВУЮ std::array
    ctx.nframes = N;

    // Вход — единицы, чтобы выход равнялся текущему g
    std::fill(inL.begin(), inL.end(), 1.f);
    std::fill(inR.begin(), inR.end(), 1.f);

    // Приведём состояние к gain=0 (первый блок)
    mod.setParam(GainSlewModule::P_GAIN, 0.0f);
    mod.beginBlock();
    mod.process(ctx);

    // Теперь таргет = 1. Рампа должна пройти ровно за один блок.
    mod.setParam(GainSlewModule::P_GAIN, 1.0f);
    mod.beginBlock();
    mod.process(ctx);

    // Проверяем монотонный рост и попадание в таргет на конце
    REQUIRE(outL.front() >= 0.f);
    REQUIRE(outL.back()  == Catch::Approx(1.f).margin(1e-6f));

    for (std::size_t i = 1; i < N; ++i) {
        REQUIRE(outL[i] >= outL[i-1]); // неубывающая
    }
}

TEST_CASE("GainSlew: рампа за 2 блока (PerBlocks, blocks=2)", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod(GainSlewModule::SlewMode::PerBlocks, /*blocks*/2);
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();
    ctx.out     = outPtrs.data();
    ctx.nframes = N;

    std::fill(inL.begin(), inL.end(), 1.f);
    std::fill(inR.begin(), inR.end(), 1.f);

    // Подготовка: сначала доведём 1 -> 0 за 2 блока, чтобы стартовать 0 -> 1 «чисто»
    mod.setParam(GainSlewModule::P_GAIN, 0.0f);
    mod.beginBlock(); mod.process(ctx); // 1.0 -> ~0.5
    mod.beginBlock(); mod.process(ctx); // ~0.5 -> 0.0

    // Теперь запускаем проверяемый переход 0 -> 1 за 2 блока
    mod.setParam(GainSlewModule::P_GAIN, 1.0f);

    // Блок 1: 0.0 -> ~0.5
    mod.beginBlock(); mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(0.5f).margin(1e-3f));

    // Блок 2: ~0.5 -> 1.0
    mod.beginBlock(); mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(1.0f).margin(1e-6f));
}

TEST_CASE("GainSlew: FixedMs — рампа за заданное время", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 240; // 5 мс при 48 кГц
    // Цель: проверить 0 -> 1 за 10 мс (два блока по 5 мс)

    GainSlewModule mod(GainSlewModule::SlewMode::FixedMs, /*blocks ignored*/1, /*ms*/10.0f);
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();
    ctx.out     = outPtrs.data();
    ctx.nframes = N;

    std::fill(inL.begin(), inL.end(), 1.f);
    std::fill(inR.begin(), inR.end(), 1.f);

    // Подготовка: доводим unity (1) -> 0 за 10 мс, чтобы стартовать 0 -> 1 «с нуля»
    mod.setParam(GainSlewModule::P_GAIN, 0.0f);
    mod.beginBlock(); mod.process(ctx); // за 5 мс: 1.0 -> ~0.5
    mod.beginBlock(); mod.process(ctx); // ещё 5 мс: ~0.5 -> 0.0

    // Проверяемый переход: 0 -> 1 за 10 мс
    mod.setParam(GainSlewModule::P_GAIN, 1.0f);

    // Первая половина времени (5 мс): ожидаем ~0.5 на конце блока
    mod.beginBlock(); mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(0.5f).margin(0.01f));

    // Вторая половина (ещё 5 мс): ожидаем 1.0
    mod.beginBlock(); mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(1.0f).margin(1e-3f));
}

TEST_CASE("GainSlew: snapshot на beginBlock — mid-block изменения игнорируются", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod(GainSlewModule::SlewMode::PerBlocks, 1);
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);

    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();   // const float** указывает на ЖИВУЮ std::array
    ctx.out     = outPtrs.data();  // float**      указывает на ЖИВУЮ std::array
    ctx.nframes = N;
    std::fill(inL.begin(), inL.end(), 1.f);
    std::fill(inR.begin(), inR.end(), 1.f);

    // Приводим к gain=0
    mod.setParam(GainSlewModule::P_GAIN, 0.f);
    mod.beginBlock();
    mod.process(ctx);

    // Новый блок: перед beginBlock() уже установлен таргет 1
    mod.setParam(GainSlewModule::P_GAIN, 1.f);
    mod.beginBlock();

    // Симулируем "mid-block": ещё раз меняем на 0.2f — НЕ должно повлиять на текущий блок
    mod.setParam(GainSlewModule::P_GAIN, 0.2f);

    mod.process(ctx);
    // Поскольку PerBlocks=1, в конце блока должны прийти к 1.0, а не к 0.2
    REQUIRE(outL.back() == Catch::Approx(1.0f).margin(1e-6f));

    // На следующем блоке уже применится 0.2f
    mod.beginBlock();
    mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(0.2f).margin(1e-6f));
}

// ─────────────────────────────────────────────────────────────────────────────
// GainSlew: reset возвращает gState к текущему read_.gain
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("GainSlew: reset возвращает gState к текущему read_.gain", "[GainSlew]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t N = 256;

    GainSlewModule mod(GainSlewModule::SlewMode::PerBlocks, /*blocks*/2);
    mod.init(sr, N);

    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    std::array<const float*, 2> inPtrs  = { inL.data(), inR.data() };
    std::array<float*, 2>       outPtrs = { outL.data(), outR.data() };

    AudioProcessContext ctx{};
    ctx.in      = inPtrs.data();
    ctx.out     = outPtrs.data();
    ctx.nframes = N;

    std::fill(inL.begin(), inL.end(), 1.f);
    std::fill(inR.begin(), inR.end(), 1.f);

    // 1) Создаём реальную «середину рампы»: из 1.0 идём к 0.0 с blocks=2 (за 1 блок доезжаем до ~0.5)
    mod.setParam(GainSlewModule::P_GAIN, 0.0f);
    mod.beginBlock();
    mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(0.5f).margin(1e-3f));

    // 2) Теперь хотим мгновенно перейти к 1.0 без рампы — выставляем цель и делаем reset()
    mod.setParam(GainSlewModule::P_GAIN, 1.0f);  // write_ = 1.0
    mod.reset();                                  // read_=1.0, gState_=1.0, рампа сброшена

    // 3) Следующий блок должен идти уже без рампы — сразу unity на всём блоке
    mod.beginBlock();
    mod.process(ctx);
    REQUIRE(outL.back() == Catch::Approx(1.0f).margin(1e-6f));
}
