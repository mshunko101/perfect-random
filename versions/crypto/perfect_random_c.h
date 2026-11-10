// perfect_random_c.h
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
// C-версия perfect_random.hpp — только standard-режим. ВЕРСИЯ 2.
//
// Изменения версии 2 (синхронизировано с C++-header'ом v2):
//   1. Усечённая обратная связь: выход предыдущего шага проходит через
//      mix64 и его верхние 16 бит входят в квадратичный член:
//      c = (c^2 + a + b + fb) mod M. Потерянные биты делают шаг
//      необратимым — откат назад требует перебора 2^16 вариантов на шаг.
//   2. Прямое заполнение состояния (seed_words): каждое слово сида
//      попадает в свою компоненту. Потолок энтропии старой инициализации
//      (сид срезался до s mod M) устранён.
//   3. Двухсидное сидирование: cascade_prng_seed2 / cascade_prng_init2.
//      s1 и s2 должны приходить из НЕЗАВИСИМЫХ источников
//      (random_device, время, события ОС).
//   4. Прогрев: init/init2/reset выполняют discard(N) после сидирования —
//      размазывает линейность инициализации.
//   5. Исправлен резерв mulmod для MSVC: старая версия теряла биты при
//      mid >= 2^32 (сдвиг mid << 32 обрезал) и могла переполнить r_hi*8.
//      Новая схема редуцирует по частям: все промежуточные < 2^64.
//   6. Состояние теперь включает prev_out: equals и ручной дамп учитывают
//      его. Формат состояния версии 2 несовместим с сохранёнными
//      состояниями версии 1.
//
// Использование (C):
//   cascade_prng rng;
//   cascade_prng_init(&rng, seed, 49);            // один сид
//   cascade_prng_init2(&rng, s1, s2, 49);         // два сида (рекомендуется)
//
//   double   x = cascade_prng_generate(&rng);      // double в [0, 1)
//   uint32_t r = cascade_prng_generate_raw(&rng);  // сырое в [0, M-1]
//
//   cascade_prng_discard(&rng, 1000000);           // пропуск значений
//   cascade_prng_generate_raw_block(&rng, buf, n); // блочная генерация
//   cascade_prng_reset(&rng);                      // возврат к исходному сиду
//   cascade_prng_equals(&rng, &rng2);              // сравнение состояний
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

// ─── Общий микшер (splitmix64 finalizer) ───────────────────────
// Биективное необратимое перемешивание 64-битного слова.
static inline uint64_t pr_mix64(uint64_t z) {
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ULL;
    z ^= z >> 27; z *= 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// ═══════════════════════════════════════════════════════════════
//  CascadePRNG — 31-битный каскадный ГПСЧ на простом Мерсенна M_31
//  Честная урезка: вывод в [0, M-1], старший бит 32-битной ячейки всегда 0.
// ═══════════════════════════════════════════════════════════════

#define CASCADE_PRNG_M       2147483647ULL      // 2^31 - 1
#define CASCADE_PRNG_MUL_A   954437177ULL       // 5/9 mod M
#define CASCADE_PRNG_MUL_B   715827884ULL       // 5/3 mod M
#define CASCADE_PRNG_FB_BITS 16                 // бит обратной связи
#define CASCADE_PRNG_GOLDEN  0x9E3779B97F4A7C15ULL
#define CASCADE_PRNG_K1      0xC2B2AE3D27D4EB4FULL
#define CASCADE_PRNG_K2      0x165667B19E3779F9ULL

typedef struct {
    uint64_t a, b, c;
    uint64_t prev_out;   // выход предыдущего шага — источник обратной связи
    int      counter;
    int      out_idx;
    size_t   N;
    uint64_t seed0;      // исходный сид(ы) — для reset()
    uint64_t seed1;
    int      two_seed;   // 1, если сидирован парой сидов
} cascade_prng;

static inline void cascade_prng_discard(cascade_prng *rng, uint64_t z);

// Прямое заполнение состояния: каждое слово — в свою компоненту
static inline void cascade_prng_seed_words(cascade_prng *rng,
                                           uint64_t w0, uint64_t w1, uint64_t w2) {
    rng->a = w0 % CASCADE_PRNG_M;  if (rng->a == 0) rng->a = 1;
    rng->b = w1 % CASCADE_PRNG_M;  if (rng->b == 0) rng->b = 1;
    rng->c = w2 % CASCADE_PRNG_M;  if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
    rng->prev_out = (rng->a * rng->b + rng->c) % CASCADE_PRNG_M;
}

// Односидное сидирование (стандартный путь): s == 0 -> 1
static inline void cascade_prng_seed(cascade_prng *rng, uint64_t s) {
    if (s == 0) s = 1;
    rng->seed0 = s;
    rng->seed1 = 0;
    rng->two_seed = 0;
    rng->a = (s * CASCADE_PRNG_MUL_A) % CASCADE_PRNG_M;
    if (rng->a == 0) rng->a = 1;
    rng->b = (s * CASCADE_PRNG_MUL_B) % CASCADE_PRNG_M;
    if (rng->b == 0) rng->b = 1;
    rng->c = (s * s + 1) % CASCADE_PRNG_M;
    if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
    rng->prev_out = (rng->a * rng->b + rng->c) % CASCADE_PRNG_M;
}

// Двухсидное сидирование: s1 и s2 из независимых источников.
// Связь — только через биективный mix64, без полиномиальных крючков.
static inline void cascade_prng_seed2(cascade_prng *rng, uint64_t s1, uint64_t s2) {
    rng->seed0 = s1;
    rng->seed1 = s2;
    rng->two_seed = 1;
    cascade_prng_seed_words(rng,
        pr_mix64(s1 ^ CASCADE_PRNG_GOLDEN),
        pr_mix64(s2 ^ CASCADE_PRNG_K1),
        pr_mix64((s1 ^ s2) ^ CASCADE_PRNG_K2));
}

static inline void cascade_prng_init(cascade_prng *rng, uint64_t s, size_t n) {
    rng->N = (n < 1) ? 1 : n;
    cascade_prng_seed(rng, s);
    cascade_prng_discard(rng, rng->N);   // прогрев: размазать линейность инициализации
}

// Двухсидная инициализация
static inline void cascade_prng_init2(cascade_prng *rng, uint64_t s1, uint64_t s2, size_t n) {
    rng->N = (n < 1) ? 1 : n;
    cascade_prng_seed2(rng, s1, s2);
    cascade_prng_discard(rng, rng->N);
}

// Возврат к исходному сиду (учитывает способ сидирования)
static inline void cascade_prng_reset(cascade_prng *rng) {
    if (rng->two_seed) cascade_prng_seed2(rng, rng->seed0, rng->seed1);
    else               cascade_prng_seed(rng, rng->seed0);
    cascade_prng_discard(rng, rng->N);
}

static inline void cascade_prng_step(cascade_prng *rng) {
    // Усечённая обратная связь: верхние FB_BITS бит выхода прошлого шага
    const uint64_t fb = pr_mix64(rng->prev_out) >> (64 - CASCADE_PRNG_FB_BITS);
    rng->a = (rng->a * CASCADE_PRNG_MUL_A) % CASCADE_PRNG_M;
    rng->b = (rng->b * CASCADE_PRNG_MUL_B) % CASCADE_PRNG_M;
    rng->c = (rng->c * rng->c + rng->a + rng->b + fb) % CASCADE_PRNG_M;
    if (++rng->counter >= (int)rng->N) {
        uint64_t tmp = rng->a;
        rng->a = rng->c;
        rng->c = rng->b;
        rng->b = tmp;
        rng->counter = 0;
        rng->out_idx = (rng->out_idx + 1) % 3;
    }
    rng->prev_out = (rng->a * rng->b + rng->c) % CASCADE_PRNG_M;
}

// Честная урезка: значение в [0, M-1], 0 достижим
static inline uint32_t cascade_prng_current_value(const cascade_prng *rng) {
    return (uint32_t)rng->prev_out;
}

static inline double cascade_prng_generate(cascade_prng *rng) {
    cascade_prng_step(rng);
    return (double)cascade_prng_current_value(rng) / (double)CASCADE_PRNG_M;
}

static inline uint32_t cascade_prng_generate_raw(cascade_prng *rng) {
    cascade_prng_step(rng);
    return cascade_prng_current_value(rng);
}

// Блочная генерация: заполнить dst n сырыми значениями (состояние последовательное)
static inline void cascade_prng_generate_raw_block(cascade_prng *rng, uint32_t *dst, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = cascade_prng_generate_raw(rng);
}

// Пропуск z значений без их материализации
static inline void cascade_prng_discard(cascade_prng *rng, uint64_t z) {
    while (z--) cascade_prng_step(rng);
}

// Сравнение полного состояния, включая prev_out, counter и out_idx
static inline int cascade_prng_equals(const cascade_prng *x, const cascade_prng *y) {
    return x->a == y->a && x->b == y->b && x->c == y->c &&
           x->prev_out == y->prev_out &&
           x->counter == y->counter && x->out_idx == y->out_idx && x->N == y->N;
}

static inline uint64_t cascade_prng_get_a(const cascade_prng *rng)         { return rng->a; }
static inline uint64_t cascade_prng_get_b(const cascade_prng *rng)         { return rng->b; }
static inline uint64_t cascade_prng_get_c(const cascade_prng *rng)         { return rng->c; }
static inline uint64_t cascade_prng_get_prev_out(const cascade_prng *rng)  { return rng->prev_out; }
static inline int      cascade_prng_get_counter(const cascade_prng *rng)   { return rng->counter; }
static inline int      cascade_prng_get_out_idx(const cascade_prng *rng)   { return rng->out_idx; }
static inline size_t   cascade_prng_get_N(const cascade_prng *rng)         { return rng->N; }

// ═══════════════════════════════════════════════════════════════
//  CascadePRNG64 — 61-битный каскадный ГПСЧ на простом Мерсенна M_61
//  Для Монте-Карло высокой точности. 183 бита состояния,
//  разрешение хвоста ~1e-18. Честная урезка: вывод в [0, M-1],
//  три старших бита 64-битной ячейки всегда 0 — без расширения.
// ═══════════════════════════════════════════════════════════════

#define CASCADE_PRNG64_M       2305843009213693951ULL  // 2^61 - 1
#define CASCADE_PRNG64_MUL_A   0x0E38E38E38E38E39ULL   // 5/9 mod M
#define CASCADE_PRNG64_MUL_B   0x0AAAAAAAAAAAAAACULL   // 5/3 mod M
#define CASCADE_PRNG64_FB_BITS 16
#define CASCADE_PRNG64_GOLDEN  0x9E3779B97F4A7C15ULL
#define CASCADE_PRNG64_K1      0xC2B2AE3D27D4EB4FULL
#define CASCADE_PRNG64_K2      0x165667B19E3779F9ULL

typedef struct {
    uint64_t a, b, c;
    uint64_t prev_out;   // выход предыдущего шага — источник обратной связи
    int      counter;
    int      out_idx;
    size_t   N;
    uint64_t seed0;      // исходный сид(ы) — для reset()
    uint64_t seed1;
    int      two_seed;   // 1, если сидирован парой сидов
} cascade_prng64;

static inline void cascade_prng64_discard(cascade_prng64 *rng, uint64_t z);

static inline uint64_t cascade_prng64_mulmod(uint64_t x, uint64_t y) {
#if PR_HAS_INT128
    return (uint64_t)((__uint128_t)x * y % CASCADE_PRNG64_M);
#else
    // Резерв для MSVC. x*y = x1y1*2^64 + (x1y0 + x0y1)*2^32 + x0y0;
    // 2^64 ≡ 8 (mod M). Все промежуточные величины < 2^64.
    uint64_t x_lo = x & 0xFFFFFFFFULL, x_hi = x >> 32;
    uint64_t y_lo = y & 0xFFFFFFFFULL, y_hi = y >> 32;
    uint64_t t_hi  = (8ULL * (x_hi * y_hi % CASCADE_PRNG64_M)) % CASCADE_PRNG64_M;
    uint64_t t_lo  = (x_lo * y_lo) % CASCADE_PRNG64_M;
    uint64_t s     = x_hi * y_lo + x_lo * y_hi;        // < 2^65
    uint64_t s0    = s & 0xFFFFFFFFULL;
    uint64_t s1    = s >> 32;                          // < 2^33
    uint64_t t_mid = (s0 << 32) % CASCADE_PRNG64_M;    // s0 * 2^32
    uint64_t t_cry = (s1 * 8ULL) % CASCADE_PRNG64_M;   // s1 * 2^64 ≡ 8*s1
    // сумма четырёх вычетов < 4*M < 2^63 — не переполняется
    return (t_hi + t_lo + t_mid + t_cry) % CASCADE_PRNG64_M;
#endif
}

// Прямое заполнение состояния: каждое слово — в свою компоненту
static inline void cascade_prng64_seed_words(cascade_prng64 *rng,
                                             uint64_t w0, uint64_t w1, uint64_t w2) {
    rng->a = w0 % CASCADE_PRNG64_M;  if (rng->a == 0) rng->a = 1;
    rng->b = w1 % CASCADE_PRNG64_M;  if (rng->b == 0) rng->b = 1;
    rng->c = w2 % CASCADE_PRNG64_M;  if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
    rng->prev_out = (cascade_prng64_mulmod(rng->a, rng->b) + rng->c) % CASCADE_PRNG64_M;
}

// Односидное сидирование (стандартный путь): s == 0 -> 1
static inline void cascade_prng64_seed(cascade_prng64 *rng, uint64_t s) {
    if (s == 0) s = 1;
    rng->seed0 = s;
    rng->seed1 = 0;
    rng->two_seed = 0;
    rng->a = cascade_prng64_mulmod(s, CASCADE_PRNG64_MUL_A);
    if (rng->a == 0) rng->a = 1;
    rng->b = cascade_prng64_mulmod(s, CASCADE_PRNG64_MUL_B);
    if (rng->b == 0) rng->b = 1;
    rng->c = (cascade_prng64_mulmod(s, s) + 1) % CASCADE_PRNG64_M;
    if (rng->c == 0) rng->c = 1;
    rng->counter = 0;
    rng->out_idx = 0;
    rng->prev_out = (cascade_prng64_mulmod(rng->a, rng->b) + rng->c) % CASCADE_PRNG64_M;
}

// Двухсидное сидирование: s1 и s2 из независимых источников.
// Вся энтропия обоих сидов доходит до состояния (3 x 61 = 183 бита).
static inline void cascade_prng64_seed2(cascade_prng64 *rng, uint64_t s1, uint64_t s2) {
    rng->seed0 = s1;
    rng->seed1 = s2;
    rng->two_seed = 1;
    cascade_prng64_seed_words(rng,
        pr_mix64(s1 ^ CASCADE_PRNG64_GOLDEN),
        pr_mix64(s2 ^ CASCADE_PRNG64_K1),
        pr_mix64((s1 ^ s2) ^ CASCADE_PRNG64_K2));
}

static inline void cascade_prng64_init(cascade_prng64 *rng, uint64_t s, size_t n) {
    rng->N = (n < 1) ? 1 : n;
    cascade_prng64_seed(rng, s);
    cascade_prng64_discard(rng, rng->N);
}

// Двухсидная инициализация
static inline void cascade_prng64_init2(cascade_prng64 *rng, uint64_t s1, uint64_t s2, size_t n) {
    rng->N = (n < 1) ? 1 : n;
    cascade_prng64_seed2(rng, s1, s2);
    cascade_prng64_discard(rng, rng->N);
}

// Возврат к исходному сиду (учитывает способ сидирования)
static inline void cascade_prng64_reset(cascade_prng64 *rng) {
    if (rng->two_seed) cascade_prng64_seed2(rng, rng->seed0, rng->seed1);
    else               cascade_prng64_seed(rng, rng->seed0);
    cascade_prng64_discard(rng, rng->N);
}

static inline void cascade_prng64_step(cascade_prng64 *rng) {
    // Усечённая обратная связь: верхние FB_BITS бит выхода прошлого шага
    const uint64_t fb = pr_mix64(rng->prev_out) >> (64 - CASCADE_PRNG64_FB_BITS);
    rng->a = cascade_prng64_mulmod(rng->a, CASCADE_PRNG64_MUL_A);
    rng->b = cascade_prng64_mulmod(rng->b, CASCADE_PRNG64_MUL_B);
    rng->c = (cascade_prng64_mulmod(rng->c, rng->c) + rng->a + rng->b + fb) % CASCADE_PRNG64_M;
    if (++rng->counter >= (int)rng->N) {
        uint64_t tmp = rng->a;
        rng->a = rng->c;
        rng->c = rng->b;
        rng->b = tmp;
        rng->counter = 0;
        rng->out_idx = (rng->out_idx + 1) % 3;
    }
    rng->prev_out = (cascade_prng64_mulmod(rng->a, rng->b) + rng->c) % CASCADE_PRNG64_M;
}

// Честная урезка: значение в [0, M-1], 0 достижим
static inline uint64_t cascade_prng64_current_value(const cascade_prng64 *rng) {
    return rng->prev_out;
}

// double [0,1) — 53 бита (стандартный Монте-Карло)
static inline double cascade_prng64_generate(cascade_prng64 *rng) {
    cascade_prng64_step(rng);
    return (double)cascade_prng64_current_value(rng) / (double)CASCADE_PRNG64_M;
}

// Сырой 61-битный вывод в [0, M-1]
static inline uint64_t cascade_prng64_generate_raw(cascade_prng64 *rng) {
    cascade_prng64_step(rng);
    return cascade_prng64_current_value(rng);
}

// Блочная генерация: заполнить dst n сырыми значениями
static inline void cascade_prng64_generate_raw_block(cascade_prng64 *rng, uint64_t *dst, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = cascade_prng64_generate_raw(rng);
}

// Пропуск z значений без их материализации
static inline void cascade_prng64_discard(cascade_prng64 *rng, uint64_t z) {
    while (z--) cascade_prng64_step(rng);
}

// Сравнение полного состояния, включая prev_out, counter и out_idx
static inline int cascade_prng64_equals(const cascade_prng64 *x, const cascade_prng64 *y) {
    return x->a == y->a && x->b == y->b && x->c == y->c &&
           x->prev_out == y->prev_out &&
           x->counter == y->counter && x->out_idx == y->out_idx && x->N == y->N;
}

static inline uint64_t cascade_prng64_get_a(const cascade_prng64 *rng)        { return rng->a; }
static inline uint64_t cascade_prng64_get_b(const cascade_prng64 *rng)        { return rng->b; }
static inline uint64_t cascade_prng64_get_c(const cascade_prng64 *rng)        { return rng->c; }
static inline uint64_t cascade_prng64_get_prev_out(const cascade_prng64 *rng) { return rng->prev_out; }
static inline int      cascade_prng64_get_counter(const cascade_prng64 *rng)  { return rng->counter; }
static inline int      cascade_prng64_get_out_idx(const cascade_prng64 *rng)  { return rng->out_idx; }
static inline size_t   cascade_prng64_get_N(const cascade_prng64 *rng)        { return rng->N; }

#ifdef __cplusplus
}
#endif

#endif // PERFECT_RANDOM_H_C
