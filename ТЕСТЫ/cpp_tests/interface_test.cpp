#include "perfect_random.hpp"
#include <random>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstdint>
#include <limits>

int main() {
    // ── URBG usage: distributions, shuffle, generate_canonical ──
    perfect_random::cascade_prng  g(42);
    perfect_random::cascade_prng64 g64(42);

    std::uniform_int_distribution<int> d(0, 9);
    int x = d(g);
    int x64 = d(g64);

    double canon31  = std::generate_canonical<double, 32>(g);
    double canon61  = std::generate_canonical<double, 53>(g64);

    std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8};
    std::shuffle(v.begin(), v.end(), g);
    std::shuffle(v.begin(), v.end(), g64);

    // ── SeedSequence seeding ──
    std::seed_seq seq{1, 2, 3, 4, 5};
    perfect_random::cascade_prng  gs(seq);
    perfect_random::cascade_prng64 gs64(seq);
    gs.seed(seq);
    gs64.seed(seq);

    // ── ranges ──
    for (int i = 0; i < 100000; ++i) {
        auto r1 = g();
        auto r2 = g64();
        if (r1 < perfect_random::cascade_prng::min() ||
            r1 > perfect_random::cascade_prng::max()) {
            std::cerr << "range fail 31\n"; return 1;
        }
        if (r2 < perfect_random::cascade_prng64::min() ||
            r2 > perfect_random::cascade_prng64::max()) {
            std::cerr << "range fail 61\n"; return 1;
        }
    }

    // ── discard / equality / serialization ──
    perfect_random::cascade_prng a1(7), a2(7);
    a1.discard(100000);
    for (std::uint64_t i = 0; i < 100000; ++i) (void)a2();
    if (a1 != a2) { std::cerr << "discard mismatch\n"; return 1; }

    perfect_random::cascade_prng64 b1(7), b2(7);
    b1.discard(100000);
    for (std::uint64_t i = 0; i < 100000; ++i) (void)b2();
    if (b1 != b2) { std::cerr << "discard mismatch 64\n"; return 1; }

    std::ostringstream os; os << a1;
    std::istringstream is(os.str());
    perfect_random::cascade_prng a3; is >> a3;
    if (!(a1 == a3)) { std::cerr << "serialization mismatch\n"; return 1; }

    std::ostringstream os64; os64 << b1;
    std::istringstream is64(os64.str());
    perfect_random::cascade_prng64 b3; is64 >> b3;
    if (!(b1 == b3)) { std::cerr << "serialization mismatch 64\n"; return 1; }

    // ── block generation ──
    std::uint32_t buf32[64];
    std::uint64_t buf64[64];
    a1.generate_raw_block(buf32, 64);
    b1.generate_raw_block(buf64, 64);
    for (int i = 0; i < 64; ++i) {
        if (buf32[i] != a1()) { std::cerr << "block mismatch 31\n"; return 1; }
    }

    std::cout << "OK  x=" << x << " x64=" << x64
              << " canon31=" << canon31 << " canon61=" << canon61
              << " shuffled:";
    for (int e : v) std::cout << ' ' << e;
    std::cout << "\nAll interface checks passed.\n";
    return 0;
}
