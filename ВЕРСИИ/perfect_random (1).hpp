// perfect_random.hpp
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
// Cascade PRNG on Mersenne primes: cascade_prng (M_31) and cascade_prng64 (M_61).
// Modern C++ interface:
//   - UniformRandomBitGenerator / std::uniform_random_bit_generator (C++20)
//   - SeedSequence support (std::seed_seq and any custom sequence)
//   - stream serialization (operator<< / operator>>)
//   - discard(n), block generation, state comparison
// Fully autonomous — no external dependencies.
//
// Usage:
//   perfect_random::cascade_prng  g(42);          // 31-bit engine
//   perfect_random::cascade_prng64 g64(42);       // 61-bit engine (Monte Carlo)
//
//   std::uniform_int_distribution<int> d(0, 9);   // works out of the box
//   std::shuffle(v.begin(), v.end(), g);          // works out of the box
//
//   std::seed_seq seq{1, 2, 3, 4, 5};
//   perfect_random::cascade_prng gs(seq);         // SeedSequence seeding
//
//   g.discard(1'000'000);                         // skip values
//   std::uint32_t buf[64]; g.generate_raw_block(buf, 64);
//   std::cout << g;                               // save state
//
// Compile: g++ -std=c++17 -O2  (C++20 for concept checks)

#ifndef PERFECT_RANDOM_HPP
#define PERFECT_RANDOM_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <type_traits>

#if defined(__SIZEOF_INT128__) || defined(__int128)
    #define PR_HAS_INT128 1
#else
    #define PR_HAS_INT128 0
#endif

namespace perfect_random {

// ─── SeedSequence detection ─────────────────────────────────────
// A type is treated as a SeedSequence if it has generate(Iterator, Iterator)
// and is not an arithmetic type (so plain uint64_t seeds keep working).
namespace detail {

template <class T, class = void>
struct has_generate : std::false_type {};

template <class T>
struct has_generate<T, std::void_t<decltype(std::declval<T&>().generate(
        std::declval<std::uint32_t*>(), std::declval<std::uint32_t*>()))>>
    : std::true_type {};

template <class T>
inline constexpr bool is_seed_sequence_v =
    detail::has_generate<T>::value && !std::is_arithmetic<T>::value;

// Fold a 32-bit SeedSequence into a 64-bit state.
inline std::uint64_t fold_seed_seq(const std::uint32_t* first, const std::uint32_t* last) {
    std::uint64_t s = 0;
    for (const std::uint32_t* p = first; p != last; ++p) {
        s ^= static_cast<std::uint64_t>(*p);
        s *= 0x9E3779B97F4A7C15ULL;
        s ^= s >> 29;
    }
    return s;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════
//  cascade_prng — 31-bit cascade PRNG on Mersenne prime M_31
// ═══════════════════════════════════════════════════════════════

class cascade_prng
{
public:
    // C++ UniformRandomBitGenerator interface
    using result_type = std::uint32_t;
    static constexpr std::uint64_t M = 2147483647ULL;   // 2^31 - 1

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return static_cast<result_type>(M - 1); }

private:
    static constexpr std::uint64_t MUL_A = 954437177ULL;   // 5/9 mod M
    static constexpr std::uint64_t MUL_B = 715827884ULL;   // 5/3 mod M

    std::uint64_t a, b, c;
    int      counter;
    int      out_idx;
    std::size_t N;

    void step()
    {
        a = (a * MUL_A) % M;
        b = (b * MUL_B) % M;
        c = (c * c + a + b) % M;
        if (++counter >= static_cast<int>(N)) {
            std::uint64_t tmp = a;
            a = c;
            c = b;
            b = tmp;
            counter = 0;
            out_idx = (out_idx + 1) % 3;
        }
    }

    std::uint32_t current_value() const
    {
        return static_cast<std::uint32_t>((a * b + c) % M);
    }

    void seed_standard(std::uint64_t s) {
        if (s == 0) s = 1;
        a = (s * MUL_A) % M;
        if (a == 0) a = 1;
        b = (s * MUL_B) % M;
        if (b == 0) b = 1;
        c = (s * s + 1) % M;
        if (c == 0) c = 1;
    }

public:
    // ── construction / seeding ────────────────────────────────
    explicit cascade_prng(std::uint64_t s = 1, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    // SeedSequence (std::seed_seq and any compatible type)
    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    explicit cascade_prng(const SeedSeq& seq, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(seq);
    }

    void seed(std::uint64_t s) {
        seed_standard(s);
        counter = 0;
        out_idx = 0;
    }

    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    void seed(const SeedSeq& seq) {
        std::uint32_t buf[6];
        seq.generate(buf, buf + 6);
        seed_standard(detail::fold_seed_seq(buf, buf + 6));
        counter = 0;
        out_idx = 0;
    }

    // ── generation ────────────────────────────────────────────
    // double in [0, 1)
    double generate() {
        step();
        return static_cast<double>(current_value()) / static_cast<double>(M);
    }

    // raw uint32_t in [0, M-1]
    std::uint32_t generate_raw() {
        step();
        return current_value();
    }

    // C++ UniformRandomBitGenerator
    result_type operator()() { return generate_raw(); }

    // Block generation: fill dst with n raw values (sequential state)
    void generate_raw_block(std::uint32_t* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) dst[i] = generate_raw();
    }

    // Skip z values without materializing them
    void discard(std::uint64_t z) { while (z--) step(); }

    // ── state access ──────────────────────────────────────────
    std::uint64_t get_a() const { return a; }
    std::uint64_t get_b() const { return b; }
    std::uint64_t get_c() const { return c; }
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }
    std::size_t get_N() const { return N; }

    // ── equality / serialization ──────────────────────────────
    friend bool operator==(const cascade_prng& x, const cascade_prng& y) {
        return x.a == y.a && x.b == y.b && x.c == y.c &&
               x.counter == y.counter && x.out_idx == y.out_idx && x.N == y.N;
    }
    friend bool operator!=(const cascade_prng& x, const cascade_prng& y) {
        return !(x == y);
    }

    friend std::ostream& operator<<(std::ostream& os, const cascade_prng& r) {
        return os << r.a << ' ' << r.b << ' ' << r.c << ' '
                  << r.counter << ' ' << r.out_idx << ' ' << r.N;
    }
    friend std::istream& operator>>(std::istream& is, cascade_prng& r) {
        return is >> r.a >> r.b >> r.c >> r.counter >> r.out_idx >> r.N;
    }
};

// ═══════════════════════════════════════════════════════════════
//  cascade_prng64 — 61-bit cascade PRNG on Mersenne prime M_61
//  For high-precision Monte Carlo. 183 bits of state, tail resolution ~1e-18.
// ═══════════════════════════════════════════════════════════════

class cascade_prng64
{
public:
    // C++ UniformRandomBitGenerator interface
    using result_type = std::uint64_t;
    static constexpr std::uint64_t M = 2305843009213693951ULL;  // 2^61 - 1

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return static_cast<result_type>(M - 1); }

private:
    static constexpr std::uint64_t MUL_A = 0x0E38E38E38E38E39ULL;  // 5/9 mod M
    static constexpr std::uint64_t MUL_B = 0x0AAAAAAAAAAAAAACULL;  // 5/3 mod M

    std::uint64_t a, b, c;
    int      counter;
    int      out_idx;
    std::size_t N;

    static inline std::uint64_t mulmod(std::uint64_t x, std::uint64_t y) {
#if PR_HAS_INT128
        return static_cast<std::uint64_t>((static_cast<__uint128_t>(x) * y) % M);
#else
        // MSVC fallback
        std::uint64_t x_lo = x & 0xFFFFFFFF, x_hi = x >> 32;
        std::uint64_t y_lo = y & 0xFFFFFFFF, y_hi = y >> 32;
        std::uint64_t lo  = x_lo * y_lo;
        std::uint64_t mid = x_lo * y_hi + x_hi * y_lo;
        std::uint64_t hi  = x_hi * y_hi;
        std::uint64_t mid_lo = mid << 32;
        std::uint64_t mid_hi = mid >> 32;
        std::uint64_t r_lo = lo + mid_lo;
        std::uint64_t carry = (r_lo < lo) ? 1 : 0;
        std::uint64_t r_hi = hi + mid_hi + carry;
        // M = 2^61-1, 2^64 mod M = 8
        std::uint64_t t = (r_hi * 8 + r_lo) % M;
        return t;
#endif
    }

    void step() {
        a = mulmod(a, MUL_A);
        b = mulmod(b, MUL_B);
        c = (mulmod(c, c) + a + b) % M;
        if (++counter >= static_cast<int>(N)) {
            std::uint64_t tmp = a;
            a = c;
            c = b;
            b = tmp;
            counter = 0;
            out_idx = (out_idx + 1) % 3;
        }
    }

    std::uint64_t current_value_mod() const
    {
        return (mulmod(a, b) + c) % M;
    }

    void seed_standard(std::uint64_t s) {
        if (s == 0) s = 1;
        a = mulmod(s, MUL_A);
        if (a == 0) a = 1;
        b = mulmod(s, MUL_B);
        if (b == 0) b = 1;
        c = (mulmod(s, s) + 1) % M;
        if (c == 0) c = 1;
    }

public:
    explicit cascade_prng64(std::uint64_t s = 1, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    explicit cascade_prng64(const SeedSeq& seq, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(seq);
    }

    void seed(std::uint64_t s) {
        seed_standard(s);
        counter = 0;
        out_idx = 0;
    }

    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    void seed(const SeedSeq& seq) {
        std::uint32_t buf[6];
        seq.generate(buf, buf + 6);
        seed_standard(detail::fold_seed_seq(buf, buf + 6));
        counter = 0;
        out_idx = 0;
    }

    // double [0,1) — 53 bits (standard Monte Carlo)
    double generate() {
        step();
        return static_cast<double>(current_value_mod()) / static_cast<double>(M);
    }

    // Raw 61-bit output in [0, M-1]
    std::uint64_t generate_raw() {
        step();
        return current_value_mod();
    }

    // C++ UniformRandomBitGenerator
    result_type operator()() { return generate_raw(); }

    void generate_raw_block(std::uint64_t* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) dst[i] = generate_raw();
    }

    void discard(std::uint64_t z) { while (z--) step(); }

    std::uint64_t get_a() const { return a; }
    std::uint64_t get_b() const { return b; }
    std::uint64_t get_c() const { return c; }
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }
    std::size_t get_N() const { return N; }

    friend bool operator==(const cascade_prng64& x, const cascade_prng64& y) {
        return x.a == y.a && x.b == y.b && x.c == y.c &&
               x.counter == y.counter && x.out_idx == y.out_idx && x.N == y.N;
    }
    friend bool operator!=(const cascade_prng64& x, const cascade_prng64& y) {
        return !(x == y);
    }

    friend std::ostream& operator<<(std::ostream& os, const cascade_prng64& r) {
        return os << r.a << ' ' << r.b << ' ' << r.c << ' '
                  << r.counter << ' ' << r.out_idx << ' ' << r.N;
    }
    friend std::istream& operator>>(std::istream& is, cascade_prng64& r) {
        return is >> r.a >> r.b >> r.c >> r.counter >> r.out_idx >> r.N;
    }
};

// ─── Interface conformance checks ───────────────────────────────
static_assert(cascade_prng::min() < cascade_prng::max(),
              "cascade_prng: min() must be less than max()");
static_assert(cascade_prng64::min() < cascade_prng64::max(),
              "cascade_prng64: min() must be less than max()");

#if defined(__cpp_lib_concepts) || (__cplusplus >= 202002L)
#include <random>
#include <concepts>
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng>,
              "cascade_prng must satisfy std::uniform_random_bit_generator");
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng64>,
              "cascade_prng64 must satisfy std::uniform_random_bit_generator");
#endif

} // namespace perfect_random

#endif // PERFECT_RANDOM_HPP
