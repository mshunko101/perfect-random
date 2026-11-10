// perfect_random.hpp
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
// Каскадный ГПСЧ на простых Мерсенна: cascade_prng (M_31) и cascade_prng64 (M_61).
//
// ВЕРСИЯ 2 (исправленная). Изменения относительно версии 1:
//   1. Усечённая обратная связь: выход предыдущего шага, пропущенный через
//      mix64 и усечённый до FB_BITS, входит в квадратичный член. Потерянные
//      биты делают шаг необратимым — откат назад требует перебора 2^FB_BITS
//      вариантов на каждый шаг.
//   2. Прямое заполнение состояния (seed_words): каждый компонент (a, b, c)
//      получает собственное перемешанное слово. Устранён потолок энтропии
//      старой инициализации (сид срезался до s mod M).
//   3. Двухсидные конструкторы и seed(s1, s2) — для смешивания независимых
//      источников энтропии (random_device + время и т. п.).
//   4. SeedSequence больше не сворачивается в одно слово: буфер раскладывается
//      по трём компонентам состояния.
//   5. Сериализация включает prev_out: формат состояния изменился, старые
//      сохранённые состояния версии 1 несовместимы.
//   6. Прогрев discard(N) после каждого сидирования — размазывает линейность
//      инициализации по состоянию.
//   7. В fallback-ветке mulmod (MSVC) устранено возможное переполнение
//      при редукции: r_lo приводится по модулю до суммирования с r_hi*8.
//
// ВАЖНО: после включения обратной связи оценка периода
//   T = 3 * lcm(ord(A), ord(B)) более недействительна — отображение
//   необратимо, возможны аттракторы. Требуется замер фактического периода
//   и повторный прогон статистических батарей (см. README).
//
// Использование:
//   perfect_random::cascade_prng   g(42);              // 31-битный движок
//   perfect_random::cascade_prng64 g64(42);            // 61-битный движок
//   perfect_random::cascade_prng   g2(rd(), now);      // два независимых сида
//   perfect_random::cascade_prng64 g64(seq);           // SeedSequence
//
//   std::uniform_int_distribution<int> d(0, 9);        // STD из коробки
//   std::shuffle(v.begin(), v.end(), g);               // и shuffle тоже
//
//   g.discard(1'000'000);
//   std::uint32_t buf[64]; g.generate_raw_block(buf, 64);
//   std::cout << g;                                    // сохранение состояния
//

#ifndef PERFECT_RANDOM_HPP
#define PERFECT_RANDOM_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <random>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <vector>
#include <iostream>
#include <limits>
#include <type_traits>
#undef min
#undef max

#if defined(__SIZEOF_INT128__) || defined(__int128)
    #define PR_HAS_INT128 1
#else
    #define PR_HAS_INT128 0
#endif

namespace perfect_random {

// ─── Распознавание SeedSequence ────────────────────────────────
// Тип считается SeedSequence, если у него есть generate(Iterator, Iterator)
// и он не арифметический (чтобы обычный сид uint64_t продолжал работать).
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

// mix64 — финалайзер splitmix64. Биективное необратимое перемешивание:
// лавинность полная, обратное восстановление входа невозможно.
inline std::uint64_t mix64(std::uint64_t z) {
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ULL;
    z ^= z >> 27; z *= 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Разложение буфера SeedSequence на три независимых слова состояния.
// Каждая пара 32-битных слов объединяется и перемешивается со своей
// константой — ни одно слово буфера не теряется, свёртки в одно
// 64-битное значение больше нет.
inline void split_seed_seq(const std::uint32_t* first, const std::uint32_t* last,
                           std::uint64_t out[3]) {
    constexpr std::uint64_t K[3] = {
        0x9E3779B97F4A7C15ULL,   // золотое сечение
        0xC2B2AE3D27D4EB4FULL,   // нечётная константа xxHash
        0x165667B19E3779F9ULL    // нечётная константа XXH64
    };
    std::uint32_t buf[6] = {0, 0, 0, 0, 0, 0};
    std::size_t n = static_cast<std::size_t>(last - first);
    if (n > 6) n = 6;
    for (std::size_t i = 0; i < n; ++i) buf[i] = first[i];
    for (int i = 0; i < 3; ++i) {
        std::uint64_t w = static_cast<std::uint64_t>(buf[2 * i])
                        | (static_cast<std::uint64_t>(buf[2 * i + 1]) << 32);
        out[i] = mix64(w ^ K[i]);
    }
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════
//  cascade_prng — 31-битный каскадный ГПСЧ на простом Мерсенна M_31
// ═══════════════════════════════════════════════════════════════

class cascade_prng
{
public:
    // Интерфейс C++ UniformRandomBitGenerator
    using result_type = std::uint32_t;
    static constexpr std::uint64_t M = 2147483647ULL;   // 2^31 - 1

    // Честная урезка: 0 достижим, значит min() = 0
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return static_cast<result_type>(M - 1); }

private:
    static constexpr std::uint64_t MUL_A = 954437177ULL;   // 5/9 mod M
    static constexpr std::uint64_t MUL_B = 715827884ULL;   // 5/3 mod M
    static constexpr int FB_BITS = 16;   // бит обратной связи (теряется за шаг)

    std::uint64_t a, b, c;
    std::uint64_t prev_out;      // выход предыдущего шага — часть состояния
    int      counter;
    int      out_idx;
    std::size_t N;

    void step()
    {
        const std::uint64_t fb = detail::mix64(prev_out) >> (64 - FB_BITS);
        a = (a * MUL_A) % M;
        b = (b * MUL_B) % M;
        c = (c * c + a + b + fb) % M;
        if (++counter >= static_cast<int>(N)) {
            std::uint64_t tmp = a;
            a = c;
            c = b;
            b = tmp;
            counter = 0;
            out_idx = (out_idx + 1) % 3;
        }
        prev_out = (a * b + c) % M;
    }

    std::uint32_t current_value() const
    {
        return static_cast<std::uint32_t>(prev_out);
    }

    // Прямое заполнение состояния: каждое слово — своя компонента.
    // Никакой свёртки в одно значение: энтропия слов доходит до состояния.
    void seed_words(std::uint64_t w0, std::uint64_t w1, std::uint64_t w2) {
        a = w0 % M;  if (a == 0) a = 1;
        b = w1 % M;  if (b == 0) b = 1;
        c = w2 % M;  if (c == 0) c = 1;
        counter = 0;
        out_idx  = 0;
        prev_out = (a * b + c) % M;      // первая связь — от реального выхода
        discard(static_cast<std::uint64_t>(N));   // прогрев: размазать линейность
    }

public:
    // ── конструирование / сидирование ─────────────────────────
    explicit cascade_prng(std::uint64_t s = 1, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    // Два сида: s1 и s2 должны приходить из независимых источников энтропии
    explicit cascade_prng(std::uint64_t s1, std::uint64_t s2, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s1, s2);
    }

    // SeedSequence (std::seed_seq и любой совместимый тип)
    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    explicit cascade_prng(const SeedSeq& seq, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(seq);
    }

    void seed(std::uint64_t s) {
        constexpr std::uint64_t K0 = 0x9E3779B97F4A7C15ULL;
        constexpr std::uint64_t K1 = 0xC2B2AE3D27D4EB4FULL;
        constexpr std::uint64_t K2 = 0x165667B19E3779F9ULL;
        seed_words(detail::mix64(s ^ K0),
                   detail::mix64(s ^ K1),
                   detail::mix64(s ^ K2));
    }

    // Два независимых источника энтропии: никакой полиномиальной связи
    // между s1 и s2, только биективное перемешивание — алгебраического
    // крючка для атаки не остаётся.
    void seed(std::uint64_t s1, std::uint64_t s2) {
        constexpr std::uint64_t K0 = 0x9E3779B97F4A7C15ULL;
        constexpr std::uint64_t K1 = 0xC2B2AE3D27D4EB4FULL;
        constexpr std::uint64_t K2 = 0x165667B19E3779F9ULL;
        seed_words(detail::mix64(s1 ^ K0),
                   detail::mix64(s2 ^ K1),
                   detail::mix64((s1 ^ s2) ^ K2));
    }

    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    void seed(const SeedSeq& seq) {
        std::uint32_t buf[6];
        seq.generate(buf, buf + 6);
        std::uint64_t w[3];
        detail::split_seed_seq(buf, buf + 6, w);
        seed_words(w[0], w[1], w[2]);
    }

    // ── генерация ─────────────────────────────────────────────
    // double в [0, 1)
    double generate()
    {
        step();
        return static_cast<double>(current_value()) / static_cast<double>(M);
    }

    // сырое значение uint32_t в [0, M-1]
    std::uint32_t generate_raw() {
        step();
        return current_value();
    }

    // Интерфейс C++ UniformRandomBitGenerator
    result_type operator()() { return generate_raw(); }

    // Блочная генерация: заполнить dst n сырыми значениями (состояние последовательное)
    void generate_raw_block(std::uint32_t* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) dst[i] = generate_raw();
    }

    // Пропуск z значений без их материализации
    void discard(std::uint64_t z) { while (z--) step(); }

    // ── доступ к состоянию ────────────────────────────────────
    std::uint64_t get_a() const { return a; }
    std::uint64_t get_b() const { return b; }
    std::uint64_t get_c() const { return c; }
    std::uint64_t get_prev_out() const { return prev_out; }
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }
    std::size_t get_N() const { return N; }

    // ── равенство / сериализация ──────────────────────────────
    // Формат версии 2: a, b, c, prev_out, counter, out_idx, N.
    // Состояния версии 1 несовместимы (см. шапку файла).
    friend bool operator==(const cascade_prng& x, const cascade_prng& y) {
        return x.a == y.a && x.b == y.b && x.c == y.c &&
               x.prev_out == y.prev_out &&
               x.counter == y.counter && x.out_idx == y.out_idx && x.N == y.N;
    }
    friend bool operator!=(const cascade_prng& x, const cascade_prng& y) {
        return !(x == y);
    }

    friend std::ostream& operator<<(std::ostream& os, const cascade_prng& r) {
        return os << r.a << ' ' << r.b << ' ' << r.c << ' '
                  << r.prev_out << ' '
                  << r.counter << ' ' << r.out_idx << ' ' << r.N;
    }
    friend std::istream& operator>>(std::istream& is, cascade_prng& r) {
        return is >> r.a >> r.b >> r.c >> r.prev_out
                  >> r.counter >> r.out_idx >> r.N;
    }
};

// ═══════════════════════════════════════════════════════════════
//  cascade_prng64 — 61-битный каскадный ГПСЧ на простом Мерсенна M_61
//  Для Монте-Карло высокой точности. 183 бита состояния,
//  разрешение хвоста ~1e-18. Вывод честно в [0, M-1] — без расширения.
// ═══════════════════════════════════════════════════════════════

class cascade_prng64
{
public:
    // Интерфейс C++ UniformRandomBitGenerator
    using result_type = std::uint64_t;
    static constexpr std::uint64_t M = 2305843009213693951ULL;  // 2^61 - 1

    // Честная урезка: значения выше M-1 недостижимы, max() = M-1
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return static_cast<result_type>(M - 1); }

private:
    static constexpr std::uint64_t MUL_A = 0x0E38E38E38E38E39ULL;  // 5/9 mod M
    static constexpr std::uint64_t MUL_B = 0x0AAAAAAAAAAAAAACULL;  // 5/3 mod M
    static constexpr int FB_BITS = 16;   // бит обратной связи (теряется за шаг)

    std::uint64_t a, b, c;
    std::uint64_t prev_out;      // выход предыдущего шага — часть состояния
    int      counter;
    int      out_idx;
    std::size_t N;

    static inline std::uint64_t mulmod(std::uint64_t x, std::uint64_t y) {
#if PR_HAS_INT128
        return static_cast<std::uint64_t>((static_cast<__uint128_t>(x) * y) % M);
#else
        // Резерв для MSVC: умножение через разложение по 32-битным половинам
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
        // Редукция по Мерсенну: 2^64 ≡ 8 (mod M). r_hi*8 < 2^62,
        // r_lo % M < 2^61 — сумма не переполняется (исправлено в v2:
        // прямой r_hi*8 + r_lo мог переполнить 64-битный аккумулятор).
        return (r_hi * 8 + r_lo % M) % M;
#endif
    }

    void step() {
        const std::uint64_t fb = detail::mix64(prev_out) >> (64 - FB_BITS);
        a = mulmod(a, MUL_A);
        b = mulmod(b, MUL_B);
        c = (mulmod(c, c) + a + b + fb) % M;
        if (++counter >= static_cast<int>(N)) {
            std::uint64_t tmp = a;
            a = c;
            c = b;
            b = tmp;
            counter = 0;
            out_idx = (out_idx + 1) % 3;
        }
        prev_out = (mulmod(a, b) + c) % M;
    }

    std::uint64_t current_value_mod() const
    {
        return prev_out;
    }

    // Прямое заполнение состояния: три слова по 61 бит = 183 бита.
    // Состояние наконец заполняется целиком — сид перестаёт быть узким местом.
    void seed_words(std::uint64_t w0, std::uint64_t w1, std::uint64_t w2) {
        a = w0 % M;  if (a == 0) a = 1;
        b = w1 % M;  if (b == 0) b = 1;
        c = w2 % M;  if (c == 0) c = 1;
        counter = 0;
        out_idx  = 0;
        prev_out = (mulmod(a, b) + c) % M;
        discard(static_cast<std::uint64_t>(N));   // прогрев: размазать линейность
    }

public:
    explicit cascade_prng64(std::uint64_t s = 1, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    // Два сида: s1 и s2 должны приходить из независимых источников энтропии
    explicit cascade_prng64(std::uint64_t s1, std::uint64_t s2, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s1, s2);
    }

    // SeedSequence (std::seed_seq и любой совместимый тип)
    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    explicit cascade_prng64(const SeedSeq& seq, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(seq);
    }

    void seed(std::uint64_t s) {
        constexpr std::uint64_t K0 = 0x9E3779B97F4A7C15ULL;
        constexpr std::uint64_t K1 = 0xC2B2AE3D27D4EB4FULL;
        constexpr std::uint64_t K2 = 0x165667B19E3779F9ULL;
        seed_words(detail::mix64(s ^ K0),
                   detail::mix64(s ^ K1),
                   detail::mix64(s ^ K2));
    }

    void seed(std::uint64_t s1, std::uint64_t s2) {
        constexpr std::uint64_t K0 = 0x9E3779B97F4A7C15ULL;
        constexpr std::uint64_t K1 = 0xC2B2AE3D27D4EB4FULL;
        constexpr std::uint64_t K2 = 0x165667B19E3779F9ULL;
        seed_words(detail::mix64(s1 ^ K0),
                   detail::mix64(s2 ^ K1),
                   detail::mix64((s1 ^ s2) ^ K2));
    }

    template <class SeedSeq,
              class = std::enable_if_t<detail::is_seed_sequence_v<SeedSeq>>>
    void seed(const SeedSeq& seq) {
        std::uint32_t buf[6];
        seq.generate(buf, buf + 6);
        std::uint64_t w[3];
        detail::split_seed_seq(buf, buf + 6, w);
        seed_words(w[0], w[1], w[2]);
    }

    // double [0,1) — 53 бита (стандартный Монте-Карло)
    double generate() {
        step();
        return static_cast<double>(current_value_mod()) / static_cast<double>(M);
    }

    // Сырой 61-битный вывод в [0, M-1]
    std::uint64_t generate_raw() {
        step();
        return current_value_mod();
    }

    // Интерфейс C++ UniformRandomBitGenerator
    result_type operator()() { return generate_raw(); }

    // Блочная генерация: заполнить dst n сырыми значениями
    void generate_raw_block(std::uint64_t* dst, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) dst[i] = generate_raw();
    }

    // Пропуск z значений без их материализации
    void discard(std::uint64_t z) { while (z--) step(); }

    std::uint64_t get_a() const { return a; }
    std::uint64_t get_b() const { return b; }
    std::uint64_t get_c() const { return c; }
    std::uint64_t get_prev_out() const { return prev_out; }
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }
    std::size_t get_N() const { return N; }

    friend bool operator==(const cascade_prng64& x, const cascade_prng64& y) {
        return x.a == y.a && x.b == y.b && x.c == y.c &&
               x.prev_out == y.prev_out &&
               x.counter == y.counter && x.out_idx == y.out_idx && x.N == y.N;
    }
    friend bool operator!=(const cascade_prng64& x, const cascade_prng64& y) {
        return !(x == y);
    }

    friend std::ostream& operator<<(std::ostream& os, const cascade_prng64& r) {
        return os << r.a << ' ' << r.b << ' ' << r.c << ' '
                  << r.prev_out << ' '
                  << r.counter << ' ' << r.out_idx << ' ' << r.N;
    }
    friend std::istream& operator>>(std::istream& is, cascade_prng64& r) {
        return is >> r.a >> r.b >> r.c >> r.prev_out
                  >> r.counter >> r.out_idx >> r.N;
    }
};

// ─── Проверки соответствия интерфейсу ───────────────────────────
static_assert(cascade_prng::min() < cascade_prng::max(),
              "cascade_prng: min() должен быть меньше max()");
static_assert(cascade_prng64::min() < cascade_prng64::max(),
              "cascade_prng64: min() должен быть меньше max()");

#if defined(__cpp_lib_concepts) || (__cplusplus >= 202002L)
#include <random>
#include <concepts>
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng>,
              "cascade_prng должен удовлетворять std::uniform_random_bit_generator");
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng64>,
              "cascade_prng64 должен удовлетворять std::uniform_random_bit_generator");
#endif

} // namespace perfect_random

#endif // PERFECT_RANDOM_HPP
