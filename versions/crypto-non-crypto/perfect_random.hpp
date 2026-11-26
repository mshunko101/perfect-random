// perfect_random.hpp
// MSHUNKO 2026 © Шунько Михаил Геннадьевич.
// Каскадный ГПСЧ на простых Мерсенна: cascade_prng (M_31) и cascade_prng64 (M_61).
// Честная урезка: вывод укладывается в ячейку ровно той ширины, которую
// даёт генератор — 31 бит у cascade_prng и 61 бит у cascade_prng64.
// Никакого финалайзера, миксера и расширения вывода — что сгенерировано,
// то и выдано. Границы min()/max() отражают фактический диапазон.
//
// Поддерживаемые компиляторы: MSVC 2013 (1800) ... MSVC 2026 (1950), GCC, Clang.
// Что подставляется на старых MSVC:
//   VS2013: нет constexpr            -> PR_CONSTEXPR / PR_CONSTVAR
//           нет enable_if_t/void_t   -> std::enable_if<...>::type, свой детект
//           нет expression SFINAE    -> детект SeedSequence через sizeof
//           нет __int128             -> переносимый резерв 128-битного умножения
// Все флаги PR_* можно переопределить извне (-DPR_HAS_CONSTEXPR=0 и т.д.).
//
// Современный C++ интерфейс:
//   - UniformRandomBitGenerator / std::uniform_random_bit_generator (C++20)
//   - поддержка SeedSequence (std::seed_seq и любая совместимая последовательность)
//   - потоковая сериализация состояния (operator<< / operator>>)
//   - discard(n), блочная генерация, сравнение состояний
// Полностью автономен — внешних зависимостей нет.
//
// Использование:
//   perfect_random::cascade_prng  g(42);          // 31-битный движок
//   perfect_random::cascade_prng64 g64(42);       // 61-битный движок (Монте-Карло)
//
//   std::uniform_int_distribution<int> d(0, 9);   // работает сразу
//   std::shuffle(v.begin(), v.end(), g);          // работает сразу
//
//   std::seed_seq seq{1, 2, 3, 4, 5};
//   perfect_random::cascade_prng gs(seq);         // сидирование последовательностью
//
//   g.discard(1000000);                           // пропуск значений
//   std::uint32_t buf[64]; g.generate_raw_block(buf, 64);
//   std::cout << g;                               // сохранение состояния
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
#include <memory>
#undef min
#undef max

// ─── Поколение компилятора / фичи ──────────────────────────────
// Все флаги переопределяемы снаружи: -DPR_HAS_CONSTEXPR=0 и т.п.
#ifndef PR_MSVC_VER
#  if defined(_MSC_VER)
#    define PR_MSVC_VER _MSC_VER
#  else
#    define PR_MSVC_VER 0
#  endif
#endif

#ifndef PR_LANG   // старые MSVC всегда пишут в __cplusplus значение 199711L
#  if defined(_MSVC_LANG)
#    define PR_LANG _MSVC_LANG
#  else
#    define PR_LANG __cplusplus
#  endif
#endif

// 1800=VS2013, 1900=VS2015, 1910=VS2017, 1920=VS2019, 1930=VS2022, 1950=VS2026
#ifndef PR_HAS_CONSTEXPR   // constexpr в VS2013 был только в ноябрьском CTP
#  if PR_MSVC_VER == 0 || PR_MSVC_VER >= 1900   // не-MSVC: по полноценному C++11
#    define PR_HAS_CONSTEXPR 1
#  else
#    define PR_HAS_CONSTEXPR 0
#  endif
#endif

#ifndef PR_HAS_INT128
#  if defined(__SIZEOF_INT128__)
#    define PR_HAS_INT128 1
#  elif PR_MSVC_VER >= 3210   //   (консервативный порог)
#    define PR_HAS_INT128 1
#  else
#    define PR_HAS_INT128 0
#  endif
#endif

#if PR_HAS_CONSTEXPR
#  define PR_CONSTEXPR constexpr
#  define PR_CONSTVAR  constexpr
#else
#  define PR_CONSTEXPR
#  define PR_CONSTVAR  const
#endif

namespace perfect_random {

// ─── Распознавание SeedSequence (совместимо с VS2013) ──────────
// VS2013 не поддерживает expression SFINAE: decltype(std::declval<T&>().foo)
// даёт жёсткую ошибку C2228 вместо тихого отсева кандидата. Поэтому детект
// построен на sizeof и границе массива — технике, работающей на всех
// поколениях MSVC (её же используют Boost.TypeTraits старых версий).
namespace detail {

#if PR_MSVC_VER && PR_MSVC_VER < 1900

// VS2013: грубый, но безопасный фильтр — SeedSequence может быть только
// классовым типом (арифметика отсекается; сам cascade_prng отсекается
// копирующим конструктором). Вызов generate() происходит в момент
// инстанцирования: передача неподходящего класса даст внятную ошибку
// компиляции, а не тихое неверное поведение.
template <class T>
struct is_seed_sequence
    : std::integral_constant<bool,
          std::is_class<typename std::remove_reference<T>::type>::value> {};

#else

template <class T>
struct has_generate {
private:
    typedef char yes;
    struct no { char pad[2]; };

    template <class U>
    static yes test(char (*)[sizeof(
        ((((U*)0)->generate((std::uint32_t*)0, (std::uint32_t*)0)),
         char(0)))]);
    template <class>
    static no& test(...);

public:
    // cv-квалификатор снимаем: у std::seed_seq::generate по стандарту
    // нет const, но логически он константен — const-ссылку обрабатывает
    // call_generate (см. ниже), а детект не должен отсеивать const-тип.
    static const bool value = sizeof(test<typename std::remove_cv<
        typename std::remove_reference<T>::type>::type>(0)) == sizeof(yes);
};

template <class T>
struct is_seed_sequence
    : std::integral_constant<bool,
          has_generate<T>::value && !std::is_arithmetic<T>::value> {};

#endif

// std::seed_seq::generate по стандарту не const, но состояния не меняет;
// const_cast (со снятием cv самого типа) позволяет передавать и const
// seed_seq&, как это делали интерфейсы исходной версии файла.
template <class SeedSeq, class It>
inline void call_generate(SeedSeq& seq, It b, It e) {
    const_cast<typename std::remove_const<SeedSeq>::type&>(seq).generate(b, e);
}

// Сворачивание 32-битной SeedSequence в 64-битное состояние.
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
//  cascade_prng — 31-битный каскадный ГПСЧ на простом Мерсенна M_31
// ═══════════════════════════════════════════════════════════════

class cascade_prng
{
public:
    // Интерфейс C++ UniformRandomBitGenerator
    typedef std::uint32_t result_type;
    static PR_CONSTVAR std::uint64_t M = 2147483647ULL;   // 2^31 - 1

    // Честная урезка: 0 достижим, значит min() = 0
    static PR_CONSTEXPR result_type min() { return 0; }
    static PR_CONSTEXPR result_type max() { return static_cast<result_type>(M - 1); }

private:
    static PR_CONSTVAR std::uint64_t MUL_A = 954437177ULL;   // 5/9 mod M
    static PR_CONSTVAR std::uint64_t MUL_B = 715827884ULL;   // 5/3 mod M

    std::uint64_t a, b, c;
    int      counter;
    int      out_idx;
    std::size_t N;
public:
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
    // ── конструирование / сидирование ─────────────────────────
    explicit cascade_prng(std::uint64_t s = 1, std::size_t n = 49)
        : counter(0), out_idx(0), N(n < 1 ? 1 : n)
    {
        seed(s);
    }

    // SeedSequence (std::seed_seq и любой совместимый тип).
    // Принимается и lvalue, и rvalue: std::seed_seq некопируем.
    template <class SeedSeq,
              class = typename std::enable_if<detail::is_seed_sequence<
                  typename std::remove_reference<SeedSeq>::type>::value>::type>
    explicit cascade_prng(SeedSeq&& seq, std::size_t n = 49)
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
              class = typename std::enable_if<detail::is_seed_sequence<
                  typename std::remove_reference<SeedSeq>::type>::value>::type>
    void seed(SeedSeq&& seq) {
        std::uint32_t buf[6];
        detail::call_generate(seq, buf, buf + 6);
        seed_standard(detail::fold_seed_seq(buf, buf + 6));
        counter = 0;
        out_idx = 0;
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
    int      get_counter() const { return counter; }
    int      get_out_idx() const { return out_idx; }
    std::size_t get_N() const { return N; }

    // ── равенство / сериализация ──────────────────────────────
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


class crypto
{
    std::unique_ptr<cascade_prng> m_a;
    std::unique_ptr<cascade_prng> m_b;
    std::unique_ptr<cascade_prng> m_c;

public:
    crypto(std::uint64_t seed1, std::uint64_t seed2, std::uint64_t seed3)
        : m_a(new cascade_prng(seed1, 48)),
          m_b(new cascade_prng(seed2, 49)),
          m_c(new cascade_prng(seed3, 50))
    {}

    std::uint32_t generate_raw() {
        std::uint32_t a  = m_a->generate_raw();
        std::uint32_t b  = m_b->generate_raw();
        std::uint32_t c  = m_c->generate_raw();
        std::uint32_t ab = (a + b) / 2;
        std::uint32_t ac = (a + c) / 2;
        std::uint32_t ca = ~((a + c) / 2);
        return ab - ac + ca;
    }

    crypto(const crypto&) = delete;
    crypto& operator=(const crypto&) = delete;

    virtual ~crypto()
    {}
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
    typedef std::uint64_t result_type;
    static PR_CONSTVAR std::uint64_t M = 2305843009213693951ULL;  // 2^61 - 1

    // Честная урезка: значения выше M-1 недостижимы, max() = M-1
    static PR_CONSTEXPR result_type min() { return 0; }
    static PR_CONSTEXPR result_type max() { return static_cast<result_type>(M - 1); }

private:
    static PR_CONSTVAR std::uint64_t MUL_A = 0x0E38E38E38E38E39ULL;  // 5/9 mod M
    static PR_CONSTVAR std::uint64_t MUL_B = 0x0AAAAAAAAAAAAAACULL;  // 5/3 mod M

    std::uint64_t a, b, c;
    int      counter;
    int      out_idx;
    std::size_t N;

    static inline std::uint64_t mulmod(std::uint64_t x, std::uint64_t y) {
#if PR_HAS_INT128
        // GCC / Clang: нативный 128-битный тип
        return static_cast<std::uint64_t>((static_cast<__uint128_t>(x) * y) % M);
#elif defined(_MSC_VER) && defined(_M_X64)
        // MSVC x64: аппаратные интринсики.
        // Требование _udiv128 (hi < делитель) выполнено: x, y < 2^61,
        // значит hi = старшая половина произведения < 2^58 < M.
        x %= M;
        y %= M;
        std::uint64_t hi;
        const std::uint64_t lo = _umul128(x, y, &hi);
        std::uint64_t rem;
        (void)_udiv128(hi, lo, M, &rem);
        return rem;
#else
        // Переносимый резерв (MSVC x86 и всё прочее без __int128):
        // 128-битное произведение через 32-битные половинки.
        // Редукция операндов до < 2^61 гарантирует отсутствие переполнений:
        // lo < 2^64, mid < 2^62, r_hi < 2^59.
        x %= M;
        y %= M;
        const std::uint64_t x_lo = x & 0xFFFFFFFFULL, x_hi = x >> 32;
        const std::uint64_t y_lo = y & 0xFFFFFFFFULL, y_hi = y >> 32;
        const std::uint64_t lo = x_lo * y_lo;               // < 2^64
        const std::uint64_t mid = x_lo * y_hi + x_hi * y_lo; // < 2^62
        const std::uint64_t hi = x_hi * y_hi;               // < 2^58
        const std::uint64_t r_lo = lo + (mid << 32);
        const std::uint64_t carry = (r_lo < lo) ? 1ULL : 0ULL;
        const std::uint64_t r_hi = hi + (mid >> 32) + carry; // < 2^59
        // p = r_hi * 2^64 + r_lo; по модулю M: 2^64 ≡ 8, 2^61 ≡ 1
        const std::uint64_t l1 = r_lo >> 61;                  // < 8
        const std::uint64_t l0 = r_lo & M;                    // < 2^61
        return ((r_hi * 8) % M + l1 + l0) % M;                // сумма < 2^62.01
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

    // SeedSequence (std::seed_seq и любой совместимый тип).
    template <class SeedSeq,
              class = typename std::enable_if<detail::is_seed_sequence<
                  typename std::remove_reference<SeedSeq>::type>::value>::type>
    explicit cascade_prng64(SeedSeq&& seq, std::size_t n = 49)
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
              class = typename std::enable_if<detail::is_seed_sequence<
                  typename std::remove_reference<SeedSeq>::type>::value>::type>
    void seed(SeedSeq&& seq) {
        std::uint32_t buf[6];
        detail::call_generate(seq, buf, buf + 6);
        seed_standard(detail::fold_seed_seq(buf, buf + 6));
        counter = 0;
        out_idx = 0;
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



class crypto64
{
    std::unique_ptr<cascade_prng64> m_a;
    std::unique_ptr<cascade_prng64> m_b;
    std::unique_ptr<cascade_prng64> m_c;

public:
    crypto64(std::uint64_t seed1, std::uint64_t seed2, std::uint64_t seed3)
        : m_a(new cascade_prng64(seed1, 48)),
          m_b(new cascade_prng64(seed2, 49)),
          m_c(new cascade_prng64(seed3, 50))
    {}

    std::uint64_t generate_raw() {
        std::uint64_t a  = m_a->generate_raw();
        std::uint64_t b  = m_b->generate_raw();
        std::uint64_t c  = m_c->generate_raw();
        std::uint64_t ab = (a + b) / 2;
        std::uint64_t ac = (a + c) / 2;
        std::uint64_t ca = ~((a + c) / 2);
        return ab - ac + ca;
    }

    crypto64(const crypto64&) = delete;
    crypto64& operator=(const crypto64&) = delete;

    virtual ~crypto64()
    {}
};

// ─── Проверки соответствия интерфейсу ───────────────────────────
#if PR_HAS_CONSTEXPR
static_assert(cascade_prng::min() < cascade_prng::max(),
              "cascade_prng: min() должен быть меньше max()");
static_assert(cascade_prng64::min() < cascade_prng64::max(),
              "cascade_prng64: min() должен быть меньше max()");
#endif

#if defined(__cpp_lib_concepts) || PR_LANG >= 202002L
#include <concepts>
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng>,
              "cascade_prng должен удовлетворять std::uniform_random_bit_generator");
static_assert(std::uniform_random_bit_generator<perfect_random::cascade_prng64>,
              "cascade_prng64 должен удовлетворять std::uniform_random_bit_generator");
#endif

} // namespace perfect_random

#endif // PERFECT_RANDOM_HPP
