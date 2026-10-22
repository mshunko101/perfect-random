# perfect_random

![Логотип](perfect_random_logo.jpeg)

![C++17](https://img.shields.io/badge/C%2B%2B-17%2B-blue.svg)
![Header-only](https://img.shields.io/badge/Header--only-yes-success.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)

---

## Что это

**CascadePRNG** — каскадный генератор псевдослучайных чисел с нелинейной архитектурой и пермутацией ролей компонент. Два движка в одном header-only файле, без внешних зависимостей:

| Движок | Модуль | Полезных бит | Состояние | Назначение |
|:---|:---|:---:|:---:|:---|
| `cascade_prng` | \(M = 2^{31} - 1\) | 31 | 183 бита | Симуляции, игры, STD-алгоритмы |
| `cascade_prng64` | \(M = 2^{61} - 1\) | 61 | 183 бита | Монте-Карло, высокая точность |

Вывод честно урезан: 31-битный движок выдаёт значения из `[0, M-1]`, 61-битный — из `[0, M-1]`. Старший бит у первого и три старших у второго всегда нулевые — интерфейс (`min()`/`max()`) это отражает и ничего не скрывает.

## Как работает

### Каскадная динамика

Три внутренних переменных `(a, b, c)` — целые числа в диапазоне `[1, M-1]`, где `M` — простое число Мерсенна. На каждом шаге:

- **`a`** — затухание: `a = (a * 5/9) mod M` (обратный элемент по модулю M);
- **`b`** — рост: `b = (b * 5/3) mod M`;
- **`c`** — нелинейность: `c = (c^2 + a + b) mod M` — квадратичный член исключает раскрытие последовательности матричным анализом, в отличие от LCG, MRG и Mersenne Twister.

Множители `5/9` и `5/3` заданы математической моделью каскада, а не подобраны эмпирически.

### Пермутация ролей

Каждые `N` шагов происходит циклический сдвиг ролей: `(a, b, c) -> (c, a, b)`. Переменная, бывшая затуханием, становится нелинейностью; нелинейность — ростом; рост — затуханием. После трёх пермутаций роли возвращаются к исходным. Пермутация меняет **роли операций**, а не данные или биты.

По умолчанию `N = 49`.

### Вывод и инициализация

```
output = (a * b + c) mod M
```

Сид линейный, без хеширования:

```
a = (s * 5/9) mod M;    b = (s * 5/3) mod M;    c = (s^2 + 1) mod M
```

Сид `0` заменяется на `1`; нулевые `a`, `b`, `c` защищены — ни одна из компонент не вырождается.

## Интерфейс (C++17 / C++20)

Оба движка реализуют полный контракт URBG (`std::uniform_random_bit_generator` на C++20 закреплён `static_assert`):

- `min()` / `max()` / `result_type` / `operator()` — работает с `std::uniform_int_distribution`, `std::uniform_real_distribution`, `std::shuffle` из коробки;
- конструкторы и `seed()` для арифметического сида и для любого SeedSequence (`std::seed_seq` и свои);
- `generate()` → `double` в `[0, 1)`, `generate_raw()` → целое, `generate_raw_block(dst, n)` — блочная заливка;
- `discard(n)` — пропуск значений без материализации;
- `operator==` / `operator!=` по полному состоянию, `operator<<` / `operator>>` — сериализация состояния.

## Использование

### C++ — 31 бит, STD-алгоритмы

```cpp
#include "perfect_random.hpp"

using namespace perfect_random;

int main() {
    cascade_prng rng(40);                    // 31-битный движок, N = 49

    std::uniform_int_distribution<int> d(0, 9);
    int x = d(rng);                          // STD из коробки

    std::shuffle(v.begin(), v.end(), rng);   // и shuffle тоже

    double val = rng.generate();             // [0.0, 1.0)
    uint32_t raw = rng.generate_raw();       // [0, M-1]
}
```

### C++ — 61 бит, Монте-Карло

```cpp
cascade_prng64 rng(40);

double val = rng.generate();          // [0.0, 1.0), 61 бит точности
uint64_t raw = rng.generate_raw();
```

### SeedSequence

```cpp
std::seed_seq seq{1, 2, 3, 4, 5};
cascade_prng gs(seq);                 // свёртка последовательности в сид
```

### Сохранение и восстановление

```cpp
cascade_prng a(40);
std::stringstream ss;
ss << a;                              // состояние a, b, c, counter, out_idx, N

cascade_prng b;
ss >> b;                              // b == a — точная воспроизводимость
```

### C API

```c
#include "perfect_random_c.h"

cascade_prng rng;
cascade_prng_init(&rng, 40, 49);

double   val = cascade_prng_generate(&rng);
uint32_t raw = cascade_prng_generate_raw(&rng);

cascade_prng_discard(&rng, 1000);
```

C-версия полностью паритетна C++: `discard`, блочная генерация, `reset`, `equals`, геттеры состояния.

## Сборка и тесты

Header-only — достаточно положить `perfect_random.hpp` (C++) или `perfect_random_c.h` (C) в проект.

Тесты и бенчмарк — в одном файле `cpp_tests.cpp` (30 тестов по 4 движкам + скоростной бенчмарк против `mt19937_64`, вся генерация через STD-интерфейс):

```bash
# GCC / Clang
g++ -std=c++17 -O2 -Wall -Wextra -o cpp_tests cpp_tests.cpp && ./cpp_tests

# MSVC
cl /std:c++17 /O2 cpp_tests.cpp

# серьёзный замер производительности
./cpp_tests 1000000000
```

Код возврата зависит только от тестов — бенчмарк не «роняет» прогон в CI.

## Производительность

Бенчмарк: 1 000 000 000 сэмплов, MSVC `/O2`, x64, best of 3, прогрев.

| Генератор | нс/вызов | млн/сек | ГБ/сек |
|:---|:---:|:---:|:---:|
| perfect_random (C++) | 10.80 | 92.6 | 0.74 |
| perfect_random (C) | 10.95 | 91.3 | 0.73 |
| mt19937_64 + dist | 6.14 | 162.8 | 1.30 |

`mt19937_64` быстрее в ~1.7 раза: его ядро — сдвиги и XOR, тогда как CascadePRNG выполняет 4 операции `% M` на вызов. Компромисс: нелинейная архитектура и пермутация ролей требуют больше вычислений. C++ и C версии идут ноздря в ноздрю — overhead абстракций отсутствует.

## Ограничения

- **Не криптография.** При 183 битах состояния и открытой структуре восстановление состояния по выводам — аналитическая задача. Для симуляций и игр — то, что нужно; для ключей и nonce — нет.
- **Статистические батареи — впереди.** Актуальные результаты прогонов через TestU01, PractRand и DieHarder будут добавлены в этот README по факту. Прошлые цифры относятся к более ранней версии и здесь не приводятся.
- **Не квазислучайный** — Соболь и Холтона не заменяет.
- Для статистических батарей 31/61-битный поток следует упаковывать плотно (бит к биту), иначе «мёртвые» старшие биты дадут фейл на формате файла, а не на генераторе.

## Лицензия

Разработано в Республике Беларусь.
Оригинальная реализация, не использующая сторонние библиотеки.

**MSHUNKO 2026 © Шунько Михаил Геннадьевич.**

<details><summary>Исследования и публикации</summary>

[1. Оценка вероятности покрытия мишеней методом Монте-Карло.](https://engee.com/community/ru/catalogs/projects/otsenka-veroiatnosti-pokrytiia-mishenei-metodom-monte-karlo)

[2. Двумерная структура отображения 2/3: каскад и симметрия](https://engee.com/community/ru/catalogs/projects/dvumernaia-struktura-otobrazheniia-2-3-kaskad-i-simmetriia)

[3. Трёхтактный цикл 4/9: самосогласованность и точный возврат](https://engee.com/community/ru/catalogs/projects/trekhtaktnyi-tsikl-4-9-samosoglasovannost-i-tochnyi-vozvrat)

[4. Каскадная развёртка из неустойчивого истока: фазы и спектр](https://engee.com/community/ru/catalogs/projects/kaskadnaia-razvertka-iz-neustoichivogo-istoka-fazy-i-spektr)

[5. Каскадный каркас: от субстракта к сознанию через один каскад](https://engee.com/community/ru/catalogs/projects/kaskadnyi-karkas-ot-substrakta-k-soznaniiu-cherez-odin-kaskad)

[6. Многочастичная симуляция стабильности кластеров](https://engee.com/community/ru/catalogs/projects/mnogochastichnaia-simuliatsiia-stabilnosti-klasterov)

[7. Шаг 50: орбита Земли, число Авогадро и граница стабильности](https://engee.com/community/ru/catalogs/projects/shag-50-orbita-zemli-chislo-avogadro-i-granitsa-stabilnosti)

[8. Каскад и ОТО: ζ-порог как механизм замедления времени](https://engee.com/community/ru/catalogs/projects/kaskad-i-oto-z-porog-kak-mekhanizm-zamedleniia-vremeni)

[8a. ζ-каскад: торможение и потеря формы](https://engee.com/community/ru/catalogs/projects/z-kaskad-tormozhenie-i-poteria-formy)

[9. Каскадная геометрия и акустика Горькова, в точке «Солнце»](https://engee.com/community/ru/catalogs/projects/kaskadnaia-geometriia-i-akustika-gorkova-v-tochke-solntse)

</details>
