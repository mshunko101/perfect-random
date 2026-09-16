// perfect_random.hpp
// MSHUNKO 2026
// Полностью автономный ГПСЧ на CascadePRNG — никаких внешних зависимостей
#ifndef PERFECT_RANDOM_HPP
#define PERFECT_RANDOM_HPP

#define _USE_MATH_DEFINES
#include <iostream>
#include <cmath>
#include <iomanip>
#include <math.h>
#include <bitset>
#include <cstdlib>
#include <string>
#include <chrono>
#include <vector>
#include <numeric>
#include <map>
#include <algorithm>
#include <fstream>
#include <ctime>
#include <stdexcept>
#include <unordered_set>
#include <cstdint>

using namespace std;

// ═══════════════════════════════════════════════════════════════
//  CascadePRNG — каскадный ГПСЧ на простом числе Мерсенна M₃₁
// ═══════════════════════════════════════════════════════════════

class CascadePRNG
{
public:                                              // FIX: M перенесён в public
    static constexpr uint64_t M = 2147483647ULL;       // 2^31 - 1
private:
    static constexpr uint64_t MUL_A = 954437177ULL;     // 5/9 mod M
    static constexpr uint64_t MUL_B = 715827884ULL;     // 5/3 mod M

    uint64_t a, b, c;
    int      counter;
    int      out_idx;
    size_t   N;

    inline void step() {
        a = (a * MUL_A) % M;
        b = (b * MUL_B) % M;
        c = (c * c + a + b) % M;
        if (++counter >= (int)N) {
            uint64_t tmp = a;
            a = c;
            c = b;
            b = tmp;
            counter = 0;
            out_idx = (out_idx + 1) % 3;
        }
    }

    inline uint32_t current_value() const {
        return (uint32_t)((a * b + c) % M);
    }

public:
    CascadePRNG(uint64_t s = 1, size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    inline void seed(uint64_t s) {
        if (s == 0) s = 1;
        a = (s * MUL_A) % M;
        if (a == 0) a = 1;
        b = (s * MUL_B) % M;
        if (b == 0) b = 1;
        c = (s * s + 1) % M;
        if (c == 0) c = 1;
        counter = 0;
        out_idx = 0;
    }

    inline double generate() {
        step();
        return (double)current_value() / (double)M;
    }

    inline uint32_t generate_raw() {
        step();
        return current_value();
    }

    uint64_t get_a() const { return a; }
    uint64_t get_b() const { return b; }
    uint64_t get_c() const { return c; }
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }

    size_t get_period() {
        CascadePRNG probe(7, N);
        uint64_t init_a = probe.a;
        uint64_t init_b = probe.b;
        uint64_t init_c = probe.c;
        int      init_counter = probe.counter;
        int      init_out = probe.out_idx;

        const size_t LIMIT = 10000000;
        for (size_t i = 0; i < LIMIT; i++) {
            probe.step();
            if (probe.a == init_a &&
                probe.b == init_b &&
                probe.c == init_c &&
                probe.counter == init_counter &&
                probe.out_idx == init_out)
            {
                return i + 1;
            }
        }
        return SIZE_MAX;
    }
};

// ═══════════════════════════════════════════════════════════════
//  Геометрия — расчёт периода вращения
// ═══════════════════════════════════════════════════════════════

struct Point {
    double x, y, z;
    Point(double x = 0, double y = 0, double z = 0) : x(x), y(y), z(z) {}
};

class RotationCalculator {
private:
    Point A, B, O_okr;
    double time_AB;

    Point vectorBetween(const Point& p1, const Point& p2) const {
        return Point(p1.x - p2.x, p1.y - p2.y, p1.z - p2.z);
    }
    double dotProduct(const Point& v1, const Point& v2) const {
        return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    }
    double vectorLength(const Point& v) const {
        return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }
    Point calculateOA() const { return vectorBetween(A, O_okr); }
    Point calculateOB() const { return vectorBetween(B, O_okr); }
    double calculateAngle() const {
        Point OA = calculateOA();
        Point OB = calculateOB();
        double cos_angle = dotProduct(OA, OB) / (vectorLength(OA) * vectorLength(OB));
        return acos(cos_angle);
    }
    double calculateOmega() const { return calculateAngle() / time_AB; }

public:
    RotationCalculator(const Point& a, const Point& b, const Point& o, double t)
        : A(a), B(b), O_okr(o), time_AB(t) {
    }
    RotationCalculator(double time) {
        A = Point(-3, -0.5, -2.5);
        B = Point(-5.0 / 3.00, -5.0 / 6.00, 25.0 / 4.00);
        O_okr = Point(-2.64, -7.91, 1.65);
        time_AB = time;
    }
    double getPeriod() const { return 2 * M_PI / calculateOmega(); }
    void showResults() const {
        cout << fixed << setprecision(2);
        cout << "Результаты расчёта:\n";
        Point OA = calculateOA();
        Point OB = calculateOB();
        cout << "Длина вектора OA: " << vectorLength(OA) << "\n";
        cout << "Длина вектора OB: " << vectorLength(OB) << "\n";
        cout << "Угол между векторами (рад): " << calculateAngle() << "\n";
        cout << "Угловая скорость (рад/год): " << calculateOmega() << "\n";
        cout << "Период вращения (лет): " << getPeriod() << "\n";
    }
};

// ═══════════════════════════════════════════════════════════════
//  Каскадное порождение под-зёрен
// ═══════════════════════════════════════════════════════════════

struct SeedCascade {
    static constexpr uint64_t SEED_DELTA = 0x9E3779B97F4A7C15ULL;

    static uint64_t derive(uint64_t master_seed, int index) {
        CascadePRNG factory(master_seed + index * SEED_DELTA, 49);
        return (uint64_t)factory.generate_raw() << 32 | factory.generate_raw();
    }
};

// ═══════════════════════════════════════════════════════════════
//  Ядро 1 — ассоциативность
// ═══════════════════════════════════════════════════════════════

class AssociativityCore {
private:
    CascadePRNG rng;
public:
    AssociativityCore(uint64_t s) : rng(s, 48) {}

    inline double generate() {
        return rng.generate();
    }

    // FIX: добавлен generate_raw() для корректной детекции коллизий
    inline uint32_t generate_raw() {
        return rng.generate_raw();
    }

    inline uint64_t state_seed() {
        uint64_t a = rng.get_a();
        uint64_t b = rng.get_b();
        uint64_t c = rng.get_c();
        return (a << 31) ^ (b << 13) ^ c ^ (rng.get_counter() * 0x100000001B3ULL);
    }
};

// ═══════════════════════════════════════════════════════════════
//  Ядро 2 — среднее значение
// ═══════════════════════════════════════════════════════════════

class MeanCore {
private:
    double mean;
    CascadePRNG rng;
public:
    MeanCore(double m, uint64_t s) : mean(m), rng(s, 49) {}

    // FIX: UINT_MAX → CascadePRNG::M
    inline double adjust(double base) {
        return (base / static_cast<double>(CascadePRNG::M)) * mean + rng.generate();
    }
};

// ═══════════════════════════════════════════════════════════════
//  Ядро 3 — фантазия
// ═══════════════════════════════════════════════════════════════

class FantasyCore {
private:
    CascadePRNG rng;
public:
    std::vector<std::vector<int>> dimensions;

    FantasyCore(uint64_t s) : rng(s, 50) {}

    inline void add_collision(unsigned int a, unsigned int b) {
        if (a == b) return;

        std::bitset<32> bits_a(a);
        std::bitset<32> bits_b(b);

        for (int i = 0; i < 32; i++) {
            if (bits_a[i] != bits_b[i]) {
                if (std::find(dimensions.begin(), dimensions.end(),
                    std::vector<int>{bits_a[i] ? 1 : 0, bits_b[i] ? 0 : 1}) == dimensions.end()) {
                    dimensions.push_back({
                        bits_a[i] ? 1 : 0,
                        bits_b[i] ? 0 : 1
                        });
                }
            }
        }
    }

    inline double apply_fantasy(double base) {
        for (const auto& dim : dimensions) {
            if (rng.generate() == 0) {
                base += dim[0] * 0.01;
            }
            else {
                base += dim[1] * 0.01;
            }
        }
        return base;
    }
};

// ═══════════════════════════════════════════════════════════════
//  Главный класс RNG
// ═══════════════════════════════════════════════════════════════

class RNG {

private:
    uint64_t           master_seed;
    AssociativityCore  assocCore;
    MeanCore           meanCore;
    FantasyCore        fantasyCore;
    std::unordered_set<uint32_t> history;             // FIX: unsigned int → uint32_t
    static constexpr int MAX_RETRIES = 49;
    static constexpr int MAX_HISTORY_SIZE = 49;
    size_t             inc_counter;
    size_t             inc_max;

    inline void reseed(uint64_t new_seed) {
        master_seed = new_seed;
        uint64_t s1 = SeedCascade::derive(new_seed, 0);
        uint64_t s2 = SeedCascade::derive(new_seed, 1);
        uint64_t s3 = SeedCascade::derive(new_seed, 2);

        assocCore = AssociativityCore(s1);
        meanCore = MeanCore(0.5, s2);
        fantasyCore = FantasyCore(s3);
    }

public:
    RNG(unsigned int seed, double period)
        : master_seed(seed),
        assocCore(SeedCascade::derive(seed, 0)),
        meanCore(0.5, SeedCascade::derive(seed, 1)),
        fantasyCore(SeedCascade::derive(seed, 2)),
        inc_counter(0)
    {
        RotationCalculator rc(period);
        inc_max = (size_t)round((rc.getPeriod() * 365.25 * 24 * 3600) / 8.0);
    }

    size_t get_period()  {
        return inc_max;
    }

    // FIX: unsigned int → uint32_t
    inline bool isCollision(uint32_t value) {
        if (history.count(value) > 0) return true;
        history.insert(value);
        return false;
    }

    inline void handle_collision(uint32_t a, uint32_t b) {
        fantasyCore.add_collision(a, b);
    }

    inline double generate() {
        if (inc_counter >= inc_max) {
            uint64_t fresh_seed = assocCore.state_seed();
            reseed(fresh_seed ^ master_seed);
            inc_counter = 0;
        }

        // FIX: коллизии проверяются по raw 32-битным значениям,
        //      а не по double, который обрезался до 0
        uint32_t base_raw = assocCore.generate_raw();
        double base = (double)base_raw / (double)CascadePRNG::M;

        int retries = 0;
        uint32_t previous_raw = base_raw;

        while (isCollision(base_raw) && retries < MAX_RETRIES) {
            previous_raw = base_raw;
            base_raw = assocCore.generate_raw();
            if (base_raw == previous_raw) {
                handle_collision(previous_raw, base_raw);
            }
            retries++;
        }

        if (retries >= MAX_RETRIES) {
            uint64_t fresh_seed = assocCore.state_seed();
            reseed(fresh_seed);
            base_raw = assocCore.generate_raw();
            base = (double)base_raw / (double)CascadePRNG::M;
            clearHistory();
            fantasyCore.dimensions.clear();
        }

        if (getHistorySize() >= MAX_HISTORY_SIZE) {
            clearHistory();
            fantasyCore.dimensions.clear();
        }

        double mean_adjusted = meanCore.adjust(base);
        double current = fantasyCore.apply_fantasy(mean_adjusted);
        inc_counter += 8;
        return current;
    }

    inline void clearHistory() { history.clear(); }
    inline size_t getHistorySize() const { return history.size(); }

    inline void reset() {
        reseed(master_seed + 1);
        clearHistory();
        fantasyCore.dimensions.clear();
        inc_counter = 0;
    }
};

#endif // PERFECT_RANDOM_HPP
