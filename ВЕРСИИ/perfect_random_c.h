// perfect_random_c.h
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
// C-версия perfect_random.hpp — только standard-режим (crypto удалён).
// Полностью автономна, внешних зависимостей нет.
//
// Usage (C):
//   cascade_prng rng;
//   cascade_prng_init(&rng, seed, 49);
//   double x   = cascade_prng_generate(&rng);
//   uint32_t r = cascade_prng_generate_raw(&rng);
//
//   cascade_prng64 rng64;
//   cascade_prng64_init(&rng64, seed, 49);
//   double x    = cascade_prng64_generate(&rng64);
//   uint64_t r  = cascade_prng64_generate_raw(&rng64);
//
#ifndef PERFECT_RANDOM_H_C
#define PERFECT_RANDOM_H_C

#include <stdint.h>
#include <stddef.h>

// ─── Определение 128-битной арифметики ──────────────────────────
#if defined(__SIZEOF_INT128__) || defined(__int128)
    #define PR_HAS_INT128 1
#else
    #define PR_HAS_INT128 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════
//  CascadePRNG — 31-битный каскадный ГПСЧ на простом Мерсенна M_31
// ═══════════════════════════════════════════════════════════════

#define CASCADE_PRNG_M     2147483647ULL      // 2^31 - 1
#define CASCADE_PRNG_MUL_A 954437177ULL       // 5/9 mod M
#define CASCADE_PRNG_MUL_B 715827884ULL       // 5/3 mod M

typedef struct {
    uint64_t a, b, c;
    int      counter;
    int      out_idx;
    size_t   N;
} cascade_prng;

static inline void cascade_prng_seed(cascade_prng *rng, uint64_t s) {
    if (s == 0) s = 1;
    rng->a = (s * CASCADE_PRNG_MUL_A) % CASCADE_PRNG_M;
    if (rng->a == 0) rng->a = 1;
    rng->b = (s * CASCADE_PRNG_MUL_B) % CASCADE_PRNG_M;
    if (rng->b == 0) rng->b = 1;
    rng->c = (s * s + 1) % CASCADE_PRNG_M;
    if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
}

static inline void cascade_prng_init(cascade_prng *rng, uint64_t s, size_t n) {
    rng->counter = 0;
    rng->out_idx = 0;
    rng->N = (n < 1) ? 1 : n;
    cascade_prng_seed(rng, s);
}

static inline void cascade_prng_step(cascade_prng *rng) {
    rng->a = (rng->a * CASCADE_PRNG_MUL_A) % CASCADE_PRNG_M;
    rng->b = (rng->b * CASCADE_PRNG_MUL_B) % CASCADE_PRNG_M;
    rng->c = (rng->c * rng->c + rng->a + rng->b) % CASCADE_PRNG_M;
    if (++rng->counter >= (int)rng->N) {
        uint64_t tmp = rng->a;
        rng->a = rng->c;
        rng->c = rng->b;
        rng->b = tmp;
        rng->counter = 0;
        rng->out_idx = (rng->out_idx + 1) % 3;
    }
}

static inline uint32_t cascade_prng_current_value(const cascade_prng *rng) {
    return (uint32_t)((rng->a * rng->b + rng->c) % CASCADE_PRNG_M);
}

static inline double cascade_prng_generate(cascade_prng *rng) {
    cascade_prng_step(rng);
    return (double)cascade_prng_current_value(rng) / (double)CASCADE_PRNG_M;
}

static inline uint32_t cascade_prng_generate_raw(cascade_prng *rng) {
    cascade_prng_step(rng);
    return cascade_prng_current_value(rng);
}

static inline uint64_t cascade_prng_get_a(const cascade_prng *rng)         { return rng->a; }
static inline uint64_t cascade_prng_get_b(const cascade_prng *rng)         { return rng->b; }
static inline uint64_t cascade_prng_get_c(const cascade_prng *rng)         { return rng->c; }
static inline int      cascade_prng_get_counter(const cascade_prng *rng)   { return rng->counter; }
static inline int      cascade_prng_get_out_idx(const cascade_prng *rng)   { return rng->out_idx; }

// ═══════════════════════════════════════════════════════════════
//  CascadePRNG64 — 61-битный каскадный ГПСЧ на простом Мерсенна M_61
//  Для Монте-Карло высокой точности. 183 бита состояния,
//  разрешение хвоста ~1e-18.
// ═══════════════════════════════════════════════════════════════

#define CASCADE_PRNG64_M     2305843009213693951ULL  // 2^61 - 1
#define CASCADE_PRNG64_MUL_A 0x0E38E38E38E38E39ULL   // 5/9 mod M
#define CASCADE_PRNG64_MUL_B 0x0AAAAAAAAAAAAAACULL   // 5/3 mod M

typedef struct {
    uint64_t a, b, c;
    int      counter;
    int      out_idx;
    size_t   N;
} cascade_prng64;

static inline uint64_t cascade_prng64_mulmod(uint64_t x, uint64_t y) {
#if PR_HAS_INT128
    return (uint64_t)((__uint128_t)x * y % CASCADE_PRNG64_M);
#else
    // Fallback для MSVC: умножение через разложение по 32-битным половинам
    uint64_t x_lo = x & 0xFFFFFFFFULL, x_hi = x >> 32;
    uint64_t y_lo = y & 0xFFFFFFFFULL, y_hi = y >> 32;
    uint64_t lo   = x_lo * y_lo;
    uint64_t mid  = x_lo * y_hi + x_hi * y_lo;
    uint64_t hi   = x_hi * y_hi;
    uint64_t mid_lo = mid << 32;
    uint64_t mid_hi = mid >> 32;
    uint64_t r_lo  = lo + mid_lo;
    uint64_t carry = (r_lo < lo) ? 1ULL : 0ULL;
    uint64_t r_hi  = hi + mid_hi + carry;
    // M = 2^61 - 1, значит 2^64 mod M = 8
    return (r_hi * 8ULL + r_lo) % CASCADE_PRNG64_M;
#endif
}

static inline void cascade_prng64_seed(cascade_prng64 *rng, uint64_t s) {
    if (s == 0) s = 1;
    rng->a = cascade_prng64_mulmod(s, CASCADE_PRNG64_MUL_A);
    if (rng->a == 0) rng->a = 1;
    rng->b = cascade_prng64_mulmod(s, CASCADE_PRNG64_MUL_B);
    if (rng->b == 0) rng->b = 1;
    rng->c = (cascade_prng64_mulmod(s, s) + 1) % CASCADE_PRNG64_M;
    if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
}

static inline void cascade_prng64_init(cascade_prng64 *rng, uint64_t s, size_t n) {
    rng->counter = 0;
    rng->out_idx = 0;
    rng->N = (n < 1) ? 1 : n;
    cascade_prng64_seed(rng, s);
}

static inline void cascade_prng64_step(cascade_prng64 *rng) {
    rng->a = cascade_prng64_mulmod(rng->a, CASCADE_PRNG64_MUL_A);
    rng->b = cascade_prng64_mulmod(rng->b, CASCADE_PRNG64_MUL_B);
    rng->c = (cascade_prng64_mulmod(rng->c, rng->c) + rng->a + rng->b) % CASCADE_PRNG64_M;
    if (++rng->counter >= (int)rng->N) {
        uint64_t tmp = rng->a;
        rng->a = rng->c;
        rng->c = rng->b;
        rng->b = tmp;
        rng->counter = 0;
        rng->out_idx = (rng->out_idx + 1) % 3;
    }
}

static inline uint64_t cascade_prng64_current_value(const cascade_prng64 *rng) {
    return (cascade_prng64_mulmod(rng->a, rng->b) + rng->c) % CASCADE_PRNG64_M;
}

// double [0,1) — 53 бита (стандартный Монте-Карло)
static inline double cascade_prng64_generate(cascade_prng64 *rng) {
    cascade_prng64_step(rng);
    return (double)cascade_prng64_current_value(rng) / (double)CASCADE_PRNG64_M;
}

// Сырой 61-битный вывод
static inline uint64_t cascade_prng64_generate_raw(cascade_prng64 *rng) {
    cascade_prng64_step(rng);
    return cascade_prng64_current_value(rng);
}

static inline uint64_t cascade_prng64_get_a(const cascade_prng64 *rng)        { return rng->a; }
static inline uint64_t cascade_prng64_get_b(const cascade_prng64 *rng)        { return rng->b; }
static inline uint64_t cascade_prng64_get_c(const cascade_prng64 *rng)        { return rng->c; }
static inline int      cascade_prng64_get_counter(const cascade_prng64 *rng)  { return rng->counter; }
static inline int      cascade_prng64_get_out_idx(const cascade_prng64 *rng)  { return rng->out_idx; }

#ifdef __cplusplus
}
#endif

#endif // PERFECT_RANDOM_H_C
