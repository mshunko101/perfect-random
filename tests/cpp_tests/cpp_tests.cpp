// cpp_tests.cpp
// Тесты и интегрированный скоростной бенчмарк perfect_random (C++ и C).
// Вся генерация — только через STD-интерфейс:
//   URBG-адаптеры (operator(), min/max/result_type) + std::uniform_real_distribution.
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
//
// Сборка:
//   g++ -std=c++17 -O2 -o cpp_tests cpp_tests.cpp
//     или
//   cl /std:c++17 /O2 cpp_tests.cpp
//
// Запуск:
//   ./cpp_tests             — все тесты + бенчмарк (10 млн чисел)
//   ./cpp_tests 500000000   — свой объём бенчмарка
#include "../../versions/non-crypto/perfect_random.hpp"

// ── Совместимость со старыми версиями заголовка ──────────────────
#if defined(HAS_INT128) && !defined(PR_HAS_INT128)
#undef HAS_INT128
#endif

// ── Подключение C-версии ─────────────────────────────────────────
// C-типы (cascade_prng, cascade_prng64) не конфликтуют с C++ типами,
// т.к. C++-классы живут в namespace perfect_random.
extern "C" {
#include "../../versions/non-crypto/perfect_random_c.h"
}

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <algorithm>
#include <functional>
#include "bench_speed.h"
using namespace std;

// ── Константы ────────────────────────────────────────────────────
static constexpr std::size_t PERIOD       = 40;    // длина каскада в тестах
static constexpr std::size_t BENCH_PERIOD = 73;    // длина каскада в бенчмарке
static constexpr unsigned    BENCH_SEED   = 12345;

// ===============================================═
//  EngineType — 4 движка
// ===============================================═
enum class EngineType {
    CPP_Standard,    // 31-bit C++, cascade_prng
    CPP_MonteCarlo,  // 61-bit C++, cascade_prng64
    C_Standard,      // 31-bit C
    C_MonteCarlo     // 61-bit C
};

static std::string EngineToStr(EngineType e) {
    switch (e) {
    case EngineType::CPP_Standard:   return "CPP_Std";
    case EngineType::CPP_MonteCarlo: return "CPP_MC";
    case EngineType::C_Standard:     return "C_Std";
    case EngineType::C_MonteCarlo:   return "C_MC";
    }
    return "?";
}

static bool Is64Bit(EngineType e) {
    return e == EngineType::CPP_MonteCarlo || e == EngineType::C_MonteCarlo;
}

// ===============================================═
//  STD-адаптеры — единственный канал генерации в тестах
//  (неконстантный lvalue-объект: распределения STL требуют URBG&)
// ===============================================═
struct Cpp31Adapter {
    perfect_random::cascade_prng* p;
    using result_type = std::uint32_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() {
        return (result_type)(perfect_random::cascade_prng::M - 1);
    }
    result_type operator()() { return p->generate_raw(); }
};

struct Cpp64Adapter {
    perfect_random::cascade_prng64* p;
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return perfect_random::cascade_prng64::M - 1; }
    result_type operator()() { return p->generate_raw(); }
};

struct C31Adapter {
    cascade_prng* p;
    using result_type = std::uint32_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return (result_type)(CASCADE_PRNG_M - 1); }
    result_type operator()() { return cascade_prng_generate_raw(p); }
};

struct C64Adapter {
    cascade_prng64* p;
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return CASCADE_PRNG64_M - 1; }
    result_type operator()() { return cascade_prng64_generate_raw(p); }
};

// ===============================================═
//  GenWrapper — универсальная обёртка для всех 4 движков
// ===============================================═
struct GenWrapper {
    EngineType engine;
    std::size_t n_;

    perfect_random::cascade_prng*   cpp31 = nullptr;
    perfect_random::cascade_prng64* cpp64 = nullptr;
    cascade_prng*   c31 = nullptr;
    cascade_prng64* c64 = nullptr;

    std::uniform_real_distribution<double> dist{0.0, 1.0};

    GenWrapper(unsigned int seed, std::size_t n, EngineType eng)
        : engine(eng), n_(n)
    {
        switch (eng) {
        case EngineType::CPP_Standard:
            cpp31 = new perfect_random::cascade_prng(seed, n);
            break;
        case EngineType::CPP_MonteCarlo:
            cpp64 = new perfect_random::cascade_prng64(seed, n);
            break;
        case EngineType::C_Standard:
            c31 = new cascade_prng;
            cascade_prng_init(c31, seed, n);
            break;
        case EngineType::C_MonteCarlo:
            c64 = new cascade_prng64;
            cascade_prng64_init(c64, seed, n);
            break;
        }
    }

    ~GenWrapper() {
        // C-структуры не имеют free-функций — просто delete
        if (cpp31) delete cpp31;
        if (cpp64) delete cpp64;
        if (c31)   delete c31;
        if (c64)   delete c64;
    }

    GenWrapper(const GenWrapper&) = delete;
    GenWrapper& operator=(const GenWrapper&) = delete;

    // STD-канал №2: double [0,1) через std::uniform_real_distribution
    double generate() {
        if (cpp31) { Cpp31Adapter g{cpp31}; return dist(g); }
        if (cpp64) { Cpp64Adapter g{cpp64}; return dist(g); }
        if (c31)   { C31Adapter   g{c31};   return dist(g); }
        C64Adapter g{c64};
        return dist(g);
    }

    // STD-канал №1: raw по ширине движка (31 или 61 бит)
    std::uint64_t raw() {
        if (cpp31) return cpp31->generate_raw();
        if (cpp64) return cpp64->generate_raw();
        if (c31)   return cascade_prng_generate_raw(c31);
        return cascade_prng64_generate_raw(c64);
    }

    int bits() const { return Is64Bit(engine) ? 61 : 31; }

    // Пересидирование: сброс к тому же сиду восстанавливает поток
    void reseed(unsigned int s) {
        if (cpp31) cpp31->seed(s);
        if (cpp64) cpp64->seed(s);
        if (c31)   cascade_prng_init(c31, s, n_);
        if (c64)   cascade_prng64_init(c64, s, n_);
    }
};

// ── Тестовый фреймворк ────────────────────────────────────────────
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

struct TestResult {
    bool        passed;
    std::string name;
    std::string detail;
};

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) return {false, name, msg}

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) return {false, name, msg}

#define ASSERT_NEAR(a, b, eps, msg) \
    if (std::abs((double)(a) - (double)(b)) > (eps)) return {false, name, msg}

#define RUN_TEST(test_func, engine)                                    \
    do {                                                               \
        g_tests_run++;                                                 \
        TestResult r = test_func(#test_func, engine);                  \
        std::string full = std::string(EngineToStr(engine)) + "::" + r.name; \
        if (r.passed) {                                                \
            g_tests_passed++;                                          \
            std::cout << "  [PASS] " << full << "\n";                  \
        } else {                                                       \
            g_tests_failed++;                                          \
            std::cout << "  [FAIL] " << full                           \
                      << " — " << r.detail << "\n";                    \
        }                                                              \
    } while (0)

// ===============================================═
//  Базовые тесты (все движки)
// ===============================================═

// Тест 1: Воспроизводимость
TestResult test_reproducibility(const std::string& name, EngineType eng) {
    GenWrapper g1(42, PERIOD, eng);
    GenWrapper g2(42, PERIOD, eng);
    for (int i = 0; i < 10000; i++) {
        double v1 = g1.generate();
        double v2 = g2.generate();
        ASSERT_EQ(v1, v2, "sequences diverged at step " + std::to_string(i));
    }
    return { true, name, "" };
}

// Тест 2: Разный seed → разный результат
TestResult test_seed_sensitivity(const std::string& name, EngineType eng) {
    GenWrapper g1(1000, PERIOD, eng);
    GenWrapper g2(1001, PERIOD, eng);
    int diff = 0;
    for (int i = 0; i < 100; i++) {
        if (g1.generate() != g2.generate())
            diff++;
    }
    ASSERT_TRUE(diff >= 95,
        "only " + std::to_string(diff) + "/100 differ (expected >= 95)");
    return { true, name, "" };
}

// Тест 3: Seed=0 не должен крашить
TestResult test_seed_zero(const std::string& name, EngineType eng) {
    GenWrapper g(0, PERIOD, eng);
    double v = g.generate();
    ASSERT_TRUE(v >= 0.0 && v < 1.0,
        "value out of range [0,1) for seed=0");
    return { true, name, "" };
}

// Тест 4: Значения в диапазоне [0, 1)
TestResult test_range(const std::string& name, EngineType eng) {
    GenWrapper g(999, PERIOD, eng);
    for (int i = 0; i < 100000; i++) {
        double v = g.generate();
        ASSERT_TRUE(v >= 0.0 && v < 1.0,
            "value " + std::to_string(v) + " out of [0,1)");
    }
    return { true, name, "" };
}

// ===============================================═
//  Статистические тесты (все движки)
// ===============================================═

// Тест 5: Среднее ~0.5
TestResult test_mean(const std::string& name, EngineType eng) {
    GenWrapper g(12345, PERIOD, eng);
    const int N = 100000;
    double sum = 0.0;
    for (int i = 0; i < N; i++)
        sum += g.generate();
    double mean = sum / N;
    ASSERT_NEAR(mean, 0.5, 0.01,
        "mean = " + std::to_string(mean) + " (expected ~0.5)");
    return { true, name, "" };
}

// Тест 6: СКО ~ sqrt(1/12) ≈ 0.2887
TestResult test_stddev(const std::string& name, EngineType eng) {
    GenWrapper g(777, PERIOD, eng);
    const int N = 100000;
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < N; i++) {
        double v = g.generate();
        sum  += v;
        sum2 += v * v;
    }
    double mean = sum / N;
    double sd = std::sqrt(sum2 / N - mean * mean);
    ASSERT_NEAR(sd, 0.288675, 0.01,
        "stddev = " + std::to_string(sd) + " (expected ~0.2887)");
    return { true, name, "" };
}

// Тест 7: Хи-квадрат по 10 корзинам
TestResult test_chi_square(const std::string& name, EngineType eng) {
    GenWrapper g(5150, PERIOD, eng);
    const int N = 100000;
    const int BINS = 10;
    double bins[BINS] = { 0 };
    for (int i = 0; i < N; i++)
        bins[(int)(g.generate() * BINS)] += 1.0;
    double expected = (double)N / BINS;
    double chi2 = 0.0;
    for (int i = 0; i < BINS; i++) {
        double d = bins[i] - expected;
        chi2 += d * d / expected;
    }
    // 9 степеней свободы, уровень 0.001 → 27.9
    ASSERT_TRUE(chi2 < 27.9,
        "chi2 = " + std::to_string(chi2) + " (expected < 27.9)");
    return { true, name, "" };
}

// Тест 8: Автокорреляция (лаги 1..5)
TestResult test_autocorrelation(const std::string& name, EngineType eng) {
    GenWrapper g(6060, PERIOD, eng);
    const int N = 100000;
    std::vector<double> v(N);
    for (int i = 0; i < N; i++)
        v[i] = g.generate() - 0.5;
    for (int lag = 1; lag <= 5; lag++) {
        double c0 = 0.0, ck = 0.0;
        for (int i = 0; i < N; i++) {
            c0 += v[i] * v[i];
            ck += v[i] * v[(i + lag) % N];
        }
        double corr = ck / c0;
        ASSERT_TRUE(std::abs(corr) < 0.02,
            "lag=" + std::to_string(lag) + " corr=" + std::to_string(corr));
    }
    return { true, name, "" };
}

// Тест 9: Монобит (частота 0/1) — биты из raw по ширине движка
TestResult test_monobit(const std::string& name, EngineType eng) {
    GenWrapper g(2024, PERIOD, eng);
    const int N = 10000;
    int width = g.bits();
    std::uint64_t ones = 0, total_bits = 0;
    for (int i = 0; i < N; i++) {
        std::uint64_t bits = g.raw();
        for (int b = 0; b < width; b++) {
            if (bits & (1ULL << b)) ones++;
            total_bits++;
        }
    }
    double ratio = (double)ones / (double)total_bits;
    ASSERT_NEAR(ratio, 0.5, 0.01,
        "bit ratio = " + std::to_string(ratio) + " (expected ~0.5)");
    return { true, name, "" };
}

// Тест 10: Runs test (серии)
TestResult test_runs(const std::string& name, EngineType eng) {
    GenWrapper g(888, PERIOD, eng);
    const int N = 100000;
    int runs = 1;
    double prev = g.generate();
    for (int i = 1; i < N; i++) {
        double v = g.generate();
        if ((v > 0.5) != (prev > 0.5))
            runs++;
        prev = v;
    }
    double expected_runs = N / 2.0;
    double z = (runs - expected_runs) / std::sqrt(N / 4.0);
    ASSERT_TRUE(std::abs(z) < 3.09,
        "z = " + std::to_string(z) + " (|z| < 3.09 required)");
    return { true, name, "" };
}

// Тест 11: Критерий Колмогорова–Смирнова
TestResult test_ks(const std::string& name, EngineType eng) {
    GenWrapper g(1717, PERIOD, eng);
    const int N = 10000;
    std::vector<double> v(N);
    for (int i = 0; i < N; i++)
        v[i] = g.generate();
    std::sort(v.begin(), v.end());
    double dmax = 0.0;
    for (int i = 0; i < N; i++) {
        double f_up   = (double)(i + 1) / N - v[i];
        double f_down = v[i] - (double)i / N;
        dmax = std::max(dmax, std::max(f_up, f_down));
    }
    ASSERT_TRUE(dmax < 0.02,
        "KS D = " + std::to_string(dmax) + " (expected < 0.02)");
    return { true, name, "" };
}

// Тест 12: Gap test
TestResult test_gap(const std::string& name, EngineType eng) {
    GenWrapper g(27182, PERIOD, eng);
    const int N = 100000;
    int gaps[10] = { 0 };
    int gap = 0;
    for (int i = 0; i < N; i++) {
        double v = g.generate();
        if (v < 0.1 || v >= 0.9) {
            if (gap < 10) gaps[gap]++;
            gap = 0;
        } else {
            gap++;
        }
    }
    int total_gaps = std::accumulate(gaps, gaps + 10, 0);
    ASSERT_TRUE(total_gaps > 100,
        "too few gaps: " + std::to_string(total_gaps));
    return { true, name, "" };
}

// Тест 13: Poker test (5 бросков в 10 корзин, доля «все разные»)
TestResult test_poker(const std::string& name, EngineType eng) {
    GenWrapper g(9090, PERIOD, eng);
    const int TRIALS = 50000;
    const int BINS = 10;
    int all_distinct = 0;
    for (int t = 0; t < TRIALS; t++) {
        int seen[BINS] = { 0 };
        for (int k = 0; k < 5; k++)
            seen[(int)(g.generate() * BINS)]++;
        int distinct = 0;
        for (int k = 0; k < BINS; k++)
            if (seen[k]) distinct++;
        if (distinct == 5) all_distinct++;
    }
    double freq = (double)all_distinct / TRIALS;
    // P(все разные) = 10*9*8*7*6 / 10^5 = 0.3024
    ASSERT_NEAR(freq, 0.3024, 0.03,
        "all-distinct freq = " + std::to_string(freq) + " (expected ~0.3024)");
    return { true, name, "" };
}

// Тест 14: Coupon collector (сбор всех 10 купонов)
TestResult test_coupon(const std::string& name, EngineType eng) {
    GenWrapper g(1414, PERIOD, eng);
    const int TRIALS = 50;
    const int BINS = 10;
    double total_draws = 0.0;
    for (int t = 0; t < TRIALS; t++) {
        int seen[BINS] = { 0 };
        int collected = 0, draws = 0;
        while (collected < BINS && draws < 100000) {
            int b = (int)(g.generate() * BINS);
            if (!seen[b]) { seen[b] = 1; collected++; }
            draws++;
        }
        ASSERT_TRUE(collected == BINS, "coupon collection failed");
        total_draws += draws;
    }
    double mean_draws = total_draws / TRIALS;
    // E = 10 * H_10 ≈ 29.29
    ASSERT_NEAR(mean_draws, 29.29, 3.0,
        "mean draws = " + std::to_string(mean_draws) + " (expected ~29.3)");
    return { true, name, "" };
}

// Тест 15: Битовая автокорреляция (raw, ширина движка)
TestResult test_bit_autocorr(const std::string& name, EngineType eng) {
    GenWrapper g(99999, PERIOD, eng);
    const int N = 10000;
    int width = g.bits();
    std::vector<std::uint64_t> vals(N);
    for (int i = 0; i < N; i++)
        vals[i] = g.raw();
    for (int lag = 1; lag <= 5; lag++) {
        std::uint64_t match = 0, total = 0;
        for (int i = 0; i < N - lag; i++) {
            for (int b = 0; b < width; b++) {
                if (((vals[i] >> b) & 1) == ((vals[i + lag] >> b) & 1))
                    match++;
                total++;
            }
        }
        double ratio = (double)match / (double)total;
        ASSERT_NEAR(ratio, 0.5, 0.01,
            "bit autocorr lag=" + std::to_string(lag) + " ratio=" + std::to_string(ratio));
    }
    return { true, name, "" };
}

// Тест 16: 2D-спектральный тест (равномерность точек на плоскости)
TestResult test_2d_spectral(const std::string& name, EngineType eng) {
    GenWrapper g(2626, PERIOD, eng);
    const int N = 100000;
    const int G = 20; // сетка 20x20, ожидание 250 на ячейку
    int cells[G * G] = { 0 };
    for (int i = 0; i < N; i++) {
        double x = g.generate(), y = g.generate();
        int cx = (int)(x * G); if (cx >= G) cx = G - 1;
        int cy = (int)(y * G); if (cy >= G) cy = G - 1;
        cells[cy * G + cx]++;
    }
    for (int i = 0; i < G * G; i++) {
        ASSERT_TRUE(cells[i] >= 150 && cells[i] <= 350,
            "cell " + std::to_string(i) + " count = " + std::to_string(cells[i])
            + " (expected 150..350)");
    }
    return { true, name, "" };
}

// Тест 17: Перестановочный тест — перемешивание
TestResult test_shuffle(const std::string& name, EngineType eng) {
    GenWrapper g(42, PERIOD, eng);
    std::vector<int> arr(100);
    for (int i = 0; i < 100; i++) arr[i] = i;
    for (int i = 99; i > 0; i--) {
        int j = (int)(g.generate() * (i + 1));
        if (j > i) j = i;
        std::swap(arr[i], arr[j]);
    }
    std::vector<int> sorted = arr;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(sorted[i], i,
            "shuffle lost element " + std::to_string(i));
    }
    int displaced = 0;
    for (int i = 0; i < 100; i++)
        if (arr[i] != i) displaced++;
    ASSERT_TRUE(displaced >= 80,
        "only " + std::to_string(displaced) + "/100 displaced (expected >= 80)");
    return { true, name, "" };
}

// ===============================================═
//  Структурные тесты (все движки)
// ===============================================═

// Тест 18: Разное смещение потребления → расхождение последовательностей
TestResult test_period_affects_sequence(const std::string& name, EngineType eng) {
    GenWrapper g1(42, PERIOD, eng);
    GenWrapper g2(42, PERIOD, eng);
    g1.generate(); g1.generate(); g1.generate();
    g2.generate();
    int diff = 0;
    for (int i = 0; i < 100; i++) {
        if (g1.generate() != g2.generate())
            diff++;
    }
    ASSERT_TRUE(diff >= 50,
        "only " + std::to_string(diff) + "/100 differ (consumption offset must diverge)");
    return { true, name, "" };
}

// Тест 19: Первый элемент уникален для разных seed
TestResult test_seed_uniqueness(const std::string& name, EngineType eng) {
    int diff = 0;
    for (unsigned int s = 1; s <= 100; s++) {
        GenWrapper g1(s, PERIOD, eng);
        GenWrapper g2(s + 1, PERIOD, eng);
        if (g1.generate() != g2.generate())
            diff++;
    }
    ASSERT_TRUE(diff >= 95,
        "only " + std::to_string(diff) + "/100 first values differ");
    return { true, name, "" };
}

// Тест 20: Большая серия в диапазоне [0,1)
TestResult test_large_batch(const std::string& name, EngineType eng) {
    GenWrapper g(31337, PERIOD, eng);
    double min_v = 1.0, max_v = 0.0;
    for (int i = 0; i < 1000000; i++) {
        double v = g.generate();
        ASSERT_TRUE(v >= 0.0 && v < 1.0,
            "value out of [0,1) at step " + std::to_string(i));
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    ASSERT_TRUE(max_v - min_v > 0.9,
        "range too narrow: [" + std::to_string(min_v) + ", " + std::to_string(max_v) + "]");
    return { true, name, "" };
}

// Тест 21: Пересидирование тем же сидом восстанавливает поток
TestResult test_reseed_reproducibility(const std::string& name, EngineType eng) {
    GenWrapper g(42, PERIOD, eng);
    GenWrapper ref(42, PERIOD, eng);
    for (int i = 0; i < 500; i++)
        g.generate(); // расход состояния
    g.reseed(42);
    for (int i = 0; i < 1000; i++) {
        double v1 = g.generate();
        double v2 = ref.generate();
        ASSERT_EQ(v1, v2, "reseed mismatch at step " + std::to_string(i));
    }
    return { true, name, "" };
}

// Тест 22: Долгая стабильность (3 млн значений)
TestResult test_long_run_stability(const std::string& name, EngineType eng) {
    GenWrapper g(42, PERIOD, eng);
    double s1 = 0, s2 = 0, s3 = 0;
    const int chunk = 1000000;
    for (int i = 0; i < chunk; i++) s1 += g.generate();
    for (int i = 0; i < chunk; i++) s2 += g.generate();
    for (int i = 0; i < chunk; i++) s3 += g.generate();
    double m1 = s1 / chunk, m2 = s2 / chunk, m3 = s3 / chunk;
    ASSERT_TRUE(std::abs(m1 - 0.5) < 0.01 && std::abs(m2 - 0.5) < 0.01 && std::abs(m3 - 0.5) < 0.01,
        "mean drift: m1=" + std::to_string(m1) + " m2=" + std::to_string(m2) + " m3=" + std::to_string(m3));
    return { true, name, "" };
}

// ===============================================═
//  Monte Carlo тесты (только 61-bit движки)
// ===============================================═

// Тест 23: Оценка числа Пи
TestResult test_mc_pi(const std::string& name, EngineType eng) {
    GenWrapper g(31415, PERIOD, eng);
    const int N = 1000000;
    int hits = 0;
    for (int i = 0; i < N; i++) {
        double x = g.generate(), y = g.generate();
        if (x * x + y * y <= 1.0) hits++;
    }
    double pi_est = 4.0 * hits / N;
    ASSERT_NEAR(pi_est, 3.14159, 0.05,
        "pi estimate = " + std::to_string(pi_est));
    return { true, name, "" };
}

// Тест 24: Матожидание произведения двух независимых равномерных
TestResult test_mc_2d_sample(const std::string& name, EngineType eng) {
    GenWrapper g(2222, PERIOD, eng);
    const int N = 500000;
    double sum = 0.0;
    for (int i = 0; i < N; i++)
        sum += g.generate() * g.generate();
    double mean = sum / N;
    ASSERT_NEAR(mean, 0.25, 0.02,
        "E[x*y] = " + std::to_string(mean) + " (expected ~0.25)");
    return { true, name, "" };
}

// Тест 25: Интеграл x^2 на [0,1] методом Монте-Карло
TestResult test_mc_integral(const std::string& name, EngineType eng) {
    GenWrapper g(3333, PERIOD, eng);
    const int N = 500000;
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        double x = g.generate();
        sum += x * x;
    }
    double integral = sum / N;
    ASSERT_NEAR(integral, 1.0 / 3.0, 0.01,
        "integral = " + std::to_string(integral) + " (expected ~0.3333)");
    return { true, name, "" };
}

// Тест 26: Редкое событие P(x < 0.01) ≈ 0.01
TestResult test_mc_rare_event(const std::string& name, EngineType eng) {
    GenWrapper g(4444, PERIOD, eng);
    const int N = 1000000;
    int hits = 0;
    for (int i = 0; i < N; i++)
        if (g.generate() < 0.01) hits++;
    double freq = (double)hits / N;
    ASSERT_NEAR(freq, 0.01, 0.002,
        "rare event freq = " + std::to_string(freq) + " (expected ~0.01)");
    return { true, name, "" };
}

// ===============================================═
//  Кросс-движковые тесты
// ===============================================═

// Тест 27: C++ standard и C standard дают одинаковый результат
TestResult test_cross_cpp_c_standard(const std::string& name, EngineType) {
    GenWrapper gcpp(42, PERIOD, EngineType::CPP_Standard);
    GenWrapper gc(42, PERIOD, EngineType::C_Standard);
    for (int i = 0; i < 50000; i++) {
        double v1 = gcpp.generate();
        double v2 = gc.generate();
        ASSERT_EQ(v1, v2,
            "C++ vs C diverged at step " + std::to_string(i));
    }
    return { true, name, "" };
}

// Тест 28: 31-bit и 61-bit дают РАЗНЫЕ результаты (разные модули)
TestResult test_cross_31_vs_61(const std::string& name, EngineType) {
    GenWrapper g31(42, PERIOD, EngineType::CPP_Standard);
    GenWrapper g64(42, PERIOD, EngineType::CPP_MonteCarlo);
    int diff = 0;
    for (int i = 0; i < 1000; i++) {
        if (g31.generate() != g64.generate())
            diff++;
    }
    ASSERT_TRUE(diff >= 990,
        "31-bit vs 61-bit only " + std::to_string(diff) + "/1000 differ (expected >= 990)");
    return { true, name, "" };
}

//  C++ URBG тесты (STD-контракт)
// ===============================================================

// Тест 29: URBG-контракт 31-битного движка
TestResult test_urbg_31bit(const std::string& name, EngineType) {
    using G = perfect_random::cascade_prng;

    ASSERT_EQ((std::uint64_t)G::min(), 0, "min() must be 0 (honest trimming)");
    ASSERT_EQ((std::uint64_t)G::max(), G::M - 1, "max() must be M-1");

    // discard(n) ≡ n вызовов operator()
    G g1(42, PERIOD), g2(42, PERIOD);
    for (int i = 0; i < 1000; i++) g2();
    g1.discard(1000);
    for (int i = 0; i < 100; i++) {
        std::uint32_t v1 = g1(), v2 = g2();
        ASSERT_EQ(v1, v2, "discard mismatch at step " + std::to_string(i));
    }

    // uniform_int_distribution по STD-контракту
    std::uniform_int_distribution<int> d(0, 9);
    for (int i = 0; i < 10000; i++) {
        int v = d(g1);
        ASSERT_TRUE(v >= 0 && v <= 9, "uniform_int out of range");
    }

    // Сериализация состояния: roundtrip через поток
    std::stringstream ss;
    ss << g1;
    G g3;
    ss >> g3;
    for (int i = 0; i < 100; i++) {
        std::uint32_t v1 = g1(), v3 = g3();
        ASSERT_EQ(v1, v3, "serialization roundtrip mismatch at " + std::to_string(i));
    }
    return { true, name, "" };
}

// Тест 30: URBG-контракт 61-битного движка
TestResult test_urbg_64bit(const std::string& name, EngineType) {
    using G = perfect_random::cascade_prng64;

    ASSERT_EQ((std::uint64_t)G::min(), 0, "min() must be 0 (honest trimming)");
    ASSERT_EQ((std::uint64_t)G::max(), G::M - 1, "max() must be M-1 (not UINT64_MAX)");

    G g1(42, PERIOD), g2(42, PERIOD);
    for (int i = 0; i < 1000; i++) g2();
    g1.discard(1000);
    for (int i = 0; i < 100; i++) {
        std::uint64_t v1 = g1(), v2 = g2();
        ASSERT_EQ(v1, v2, "discard mismatch at step " + std::to_string(i));
    }

    std::uniform_int_distribution<int> d(0, 9);
    for (int i = 0; i < 10000; i++) {
        int v = d(g1);
        ASSERT_TRUE(v >= 0 && v <= 9, "uniform_int out of range");
    }

    std::stringstream ss;
    ss << g1;
    G g3;
    ss >> g3;
    for (int i = 0; i < 100; i++) {
        std::uint64_t v1 = g1(), v3 = g3();
        ASSERT_EQ(v1, v3, "serialization roundtrip mismatch at " + std::to_string(i));
    }
    return { true, name, "" };
}


// ===============================================================
//  Main
// ===============================================================
int main(int argc, char* argv[]) {
    setlocale(0, "ru-ru");
    // Объём бенчмарка: по умолчанию 10 млн (быстро после тестов)
    std::size_t bench_N = 10'000'000;
    if (argc > 1) bench_N = std::stoull(argv[1]);
/*
    std::cout << "===================================================\n";
    std::cout << "  perfect_random — тесты + бенчмарк (STD-интерфейс)\n";
    std::cout << "  MSHUNKO 2026 © Шунько Михаил Геннадьевич\n";
    std::cout << "  30 тестов, 4 движка, 100 запусков\n";
    std::cout << "===================================================\n";

    EngineType engines[] = {
        EngineType::CPP_Standard,
        EngineType::CPP_MonteCarlo,
        EngineType::C_Standard,
        EngineType::C_MonteCarlo
    };

    // ── Базовые тесты (все 4 движка) ──
    for (auto eng : engines) {
        std::cout << "\n--- Basic [" << EngineToStr(eng) << "] ---\n";
        RUN_TEST(test_reproducibility, eng);
        RUN_TEST(test_seed_sensitivity, eng);
        RUN_TEST(test_seed_zero, eng);
        RUN_TEST(test_range, eng);
    }

    // ── Статистические тесты (все 4 движка) ──
    for (auto eng : engines) {
        std::cout << "\n--- Statistical [" << EngineToStr(eng) << "] ---\n";
        RUN_TEST(test_mean, eng);
        RUN_TEST(test_stddev, eng);
        RUN_TEST(test_chi_square, eng);
        RUN_TEST(test_autocorrelation, eng);
        RUN_TEST(test_monobit, eng);
        RUN_TEST(test_runs, eng);
        RUN_TEST(test_ks, eng);
        RUN_TEST(test_gap, eng);
        RUN_TEST(test_poker, eng);
        RUN_TEST(test_coupon, eng);
        RUN_TEST(test_bit_autocorr, eng);
        RUN_TEST(test_2d_spectral, eng);
        RUN_TEST(test_shuffle, eng);
    }

    // ── Структурные тесты (все 4 движка) ──
    for (auto eng : engines) {
        std::cout << "\n--- Structural [" << EngineToStr(eng) << "] ---\n";
        RUN_TEST(test_period_affects_sequence, eng);
        RUN_TEST(test_seed_uniqueness, eng);
        RUN_TEST(test_large_batch, eng);
        RUN_TEST(test_reseed_reproducibility, eng);
        RUN_TEST(test_long_run_stability, eng);
    }

    // ── Monte Carlo тесты (только 61-bit движки) ──
    {
        EngineType mc_engines[] = {
            EngineType::CPP_MonteCarlo,
            EngineType::C_MonteCarlo
        };
        for (auto eng : mc_engines) {
            std::cout << "\n--- Monte Carlo [" << EngineToStr(eng) << "] ---\n";
            RUN_TEST(test_mc_pi, eng);
            RUN_TEST(test_mc_2d_sample, eng);
            RUN_TEST(test_mc_integral, eng);
            RUN_TEST(test_mc_rare_event, eng);
        }
    }

    // ── Кросс-движковые тесты ──
    std::cout << "\n--- Cross-engine ---\n";
    RUN_TEST(test_cross_cpp_c_standard, EngineType::CPP_Standard);
    RUN_TEST(test_cross_31_vs_61, EngineType::CPP_Standard);

    // ── C++ URBG тесты ──
    std::cout << "\n--- C++ URBG ---\n";
    RUN_TEST(test_urbg_31bit, EngineType::CPP_Standard);
    RUN_TEST(test_urbg_64bit, EngineType::CPP_MonteCarlo);

    // ── Итоги тестов ──
    std::cout << "\n===================================================\n";
    std::cout << "  Тесты: " << g_tests_run
              << " | PASS: " << g_tests_passed
              << " | FAIL: " << g_tests_failed << "\n";
    std::cout << "===================================================\n";
    */
    #include <cstdlib>
 
    // ── Скоростной бенчмарк ──
    bentch_speed(); 

    return (g_tests_failed > 0) ? 1 : 0;
}
