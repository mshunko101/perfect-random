# perfect_random.hpp

![Логотип](perfect_random_logo.jpeg)
Каскадный генератор псевдослучайных чисел на простых Мерсенна в одном заголовочном файле.
MSHUNKO 2026 © Шунько Михаил Геннадьевич.

![C/C++](https://img.shields.io/badge/C%20%7C%20C%2B%2B-99%20%7C%2017-blue.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Header-only](https://img.shields.io/badge/Header--only-yes-success.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)
[![Release](https://img.shields.io/github/v/release/MSHUNKO101/perfect_random)](https://github.com/MSHUNKO101/perfect_random/releases)
[![License](https://img.shields.io/github/license/MSHUNKO101/perfect_random)](LICENSE)

| Класс            | Поле            | Состояние | Вывод                       | Назначение                          |
|------------------|-----------------|-----------|-----------------------------|-------------------------------------|
| `cascade_prng`   | \(M_{31}=2^{31}-1\) | ~183 бита | `uint32_t` в [0, 2³¹−2] | общий случай, генерация индексов    |
| `cascade_prng64` | \(M_{61}=2^{61}-1\) | ~183 бита | `uint64_t` в [0, 2⁶¹−2] | Монте-Карло высокой точности        |
| `crypto`         | —               | 3 движка  | `uint32_t`                  | экспериментальная комбинированная схема (см. «Оговорки») |

## Идея

Каскад из трёх мультипликативных рекуррентностей по модулю простого Мерсенна:

```text
a ← a·A mod M          (A = 5/9 mod M)
b ← b·B mod M          (B = 5/3 mod M)
c ← c² + a + b mod M
```

Каждые `N` шагов (по умолчанию `N = 49`) состояния циклически переставляются (`a ← c ← b ← a`), что «перемешивает» три независимые траектории между собой. Вывод — `(a·b + c) mod M`.

**Честная урезка.** Вывод укладывается в ячейку ровно той ширины, которую даёт поле: 31 бит у `cascade_prng` и 61 бит у `cascade_prng64`. Никакого финалайзера, миксера и расширения вывода — что сгенерировано, то и выдано. `min()`/`max()` отражают фактический диапазон: значения выше `M−1` недостижимы, `0` достижим.

## Возможности

- Современный интерфейс C++: удовлетворяет `UniformRandomBitGenerator` / `std::uniform_random_bit_generator` (C++20, проверяется `static_assert`) — работает с `std::uniform_int_distribution`, `std::shuffle` и любой стандартной библиотекой распределений сразу.
- Сидирование как одним числом, так и любой `SeedSequence` (включая `std::seed_seq`) — распознавание через проверку наличия `generate()`.
- Потоковая сериализация состояния: `operator<<` / `operator>>`.
- `discard(n)` — пропуск `n` значений без материализации.
- Блочная генерация: `generate_raw_block(dst, n)`.
- Геттеры состояния (`get_a/b/c/counter/out_idx/N`) и сравнение двух генераторов `==` / `!=`.
- Полностью автономен: внешних зависимостей нет, `__int128` используется при наличии (GCC/Clang), для MSVC есть поразрядный резерв `mulmod`.

## Требования

- C++17 и выше (рекомендуется C++20 — тогда включается проверка концепта).
- Заголовочный файл один: `perfect_random.hpp`.

## Быстрый старт

```cpp
#include "perfect_random.hpp"
#include <random>
#include <algorithm>
#include <vector>

int main() {
    // 31-битный движок
    perfect_random::cascade_prng g(42);

    // 61-битный движок (Монте-Карло)
    perfect_random::cascade_prng64 g64(42);

    // Стандартные распределения работают сразу
    std::uniform_int_distribution<int> d(0, 9);
    int x = d(g);

    std::vector<int> v = {1, 2, 3, 4, 5};
    std::shuffle(v.begin(), v.end(), g);

    // double в [0, 1): 31 бит у cascade_prng, 61 бит у cascade_prng64
    double u = g64.generate();

    // Сырой вывод
    std::uint64_t r = g64.generate_raw();
}
```

## Справочник API

### Конструирование и сидирование

```cpp
cascade_prng(std::uint64_t s = 1, std::size_t n = 49);   // сид числом
template <class SeedSeq>
cascade_prng(const SeedSeq& seq, std::size_t n = 49);    // сид последовательностью
void seed(std::uint64_t s);
template <class SeedSeq> void seed(const SeedSeq& seq);
```

`n` — длина каскадного цикла (минимум 1). Для `SeedSequence` используются первые 6 32-битных слов, которые сворачиваются в 64-битное состояние умножением на золотую константу `0x9E3779B97F4A7C15` с xor-shift.

Нулевой сид заменяется на 1, нулевые компоненты состояния — на 1, поэтому состояние `a = b = c = 0` недостижимо.

### Генерация

| Метод                        | Возвращаемое значение                     |
|------------------------------|-------------------------------------------|
| `operator()`                 | сырой вывод: `uint32_t` / `uint64_t`      |
| `generate_raw()`             | то же, явно                               |
| `generate()`                 | `double` в [0, 1)                         |
| `generate_raw_block(dst, n)` | заполнить массив `n` сырыми значениями    |
| `discard(z)`                 | пропустить `z` значений состояния         |

### Состояние

```cpp
get_a(), get_b(), get_c();      // компоненты состояния
get_counter(), get_out_idx();   // фаза каскада
get_N();                        // длина цикла
operator==(g1, g2), operator!=(g1, g2);
std::ostream& operator<<(std::ostream&, const T&);
std::istream& operator>>(std::istream&, T&);
```

Пример сохранения и восстановления:

```cpp
perfect_random::cascade_prng g(42);
std::ostringstream out; out << g;                 // сохранить
std::istringstream in(out.str());
perfect_random::cascade_prng h(1); in >> h;       // восстановить
assert(g == h);
```

## Оговорки

- **Не криптографический.** Название класса `crypto` не означает криптостойкость: рекуррентности мультипликативные и по структуре обратимы, выходные значения детерминированно восстанавливаются из состояния. Для криптографических задач используйте `std::random_device`, ChaCha20 и стандартные примитивы.
- **`crypto` — экспериментальный класс.** Комбинирует вывод трёх движков с разными `N` (48/49/50); при этом третья компонента (`ca`) дублирует вторую (`ac`), а деление на 2 выполняется в обычной, а не модулярной арифметике. Поведение может измениться в следующих версиях — не закладывайтесь на него в коде, который должен жить долго.
- **Статистика не сертифицирована.** Перед использованием в чувствительных задачах прогоните движок через TestU01 (Crush/BigCrush) или PractRand. Гарантий качества, сопоставимого с `std::mt19937_64` или xoshiro256**, нет — проверяйте под свою нагрузку.
- `double` из `generate()` у `cascade_prng` имеет разрешение 31 бит (~9 знаков после запятой); для Монте-Карло используйте `cascade_prng64`.

## Сборка примеров

Компиляция тривиальна:

```bash
g++ -std=c++20 -O2 -I. main.cpp -o main
clang++ -std=c++20 -O2 -I. main.cpp -o main
cl /std:c++20 /O2 /I. main.cpp
```

## Лицензия

Разработано в Республике Беларусь.
Оригинальная реализация, не использующая сторонние библиотеки.

**MSHUNKO 2026 © Шунько Михаил Геннадьевич.**

<details><summary>Исследования и публикации</summary>

[1. Оценка вероятности покрытия мишеней методом Монте-Карло.](https://engee.com/community/ru/catalogs/projects/otsenka-veroiatnosti-pokrytiia-mishenei-metodom-monte-karlo)

[2. Двумерная структура отображения 2/3: каскад и симметрия](https://engee.com/community/ru/catalogs/projects/dvumernaia-struktura-otobrazheniia-2-3-kaskad-i-simmetriia)

[3. Трёхтактный цикл 4/9: самосогласованность и точный возврат](https://engee.com/community/ru/catalogs/projects/trekhtaktnyi-tsikl-4-9-samosoglasovannost-i-tochnyi-vozvrat)

[4. Каскадная развёртка из неустойчивого истока: фазы и спектр](https://engee.com/community/ru/catalogs/projects/kaskadnaia-razvertka-iz-neustoichivogo-istoka-fazy-i-spektr)

[5. Каскадный каркас: от субстрата к сознанию через один каскад](https://engee.com/community/ru/catalogs/projects/kaskadnyi-karkas-ot-substrata-k-soznaniu-cherez-odin-kaskad)

[6. Многочастичная симуляция стабильности кластеров](https://engee.com/community/ru/catalogs/projects/mnogochastichnaia-simuliatsiia-stabilnosti-klasterov)

[7. Шаг 50: орбита Земли, число Авогадро и граница стабильности](https://engee.com/community/ru/catalogs/projects/shag-50-orbita-zemli-chislo-avogadro-i-granitsa-stabilnosti)

[8. Каскад и ОТО: ζ-порог как механизм замедления времени](https://engee.com/community/ru/catalogs/projects/kaskad-i-oto-z-porog-kak-mekhanizm-zamedleniia-vremeni)

[8a. ζ-каскад: торможение и потеря формы](https://engee.com/community/ru/catalogs/projects/z-kaskad-tormozhenie-i-poteria-formy)

[9. Каскадная геометрия и акустика Горькова, в точке «Солнце»](https://engee.com/community/ru/catalogs/projects/kaskadnaia-geometriia-i-akustika-gorkova-v-tochke-solntse)

</details>
