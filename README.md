# perfect_random

![Логотип](perfect_random_logo.jpeg)

![perf: 30M nums/sec](https://img.shields.io/badge/perf-14.1M%20nums%2Fsec-orange)
![C/C++](https://img.shields.io/badge/C%20%7C%20C%2B%2B-99%20%7C%2017-blue.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Header-only](https://img.shields.io/badge/Header--only-yes-success.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)
[![Release](https://img.shields.io/github/v/release/MSHUNKO101/perfect_random)](https://github.com/MSHUNKO101/perfect_random/releases)
[![License](https://img.shields.io/github/license/MSHUNKO101/perfect_random)](LICENSE)

![NIST SP 800-22](https://img.shields.io/badge/NIST%20SP%20800--22-188%2F188%20PASS-brightgreen.svg)
![BigCrush](https://img.shields.io/badge/BigCrush-160%2F160%20PASS-brightgreen.svg)
![DieHarder](https://img.shields.io/badge/DieHarder-0%20FAIL-brightgreen.svg)
![Nonlinear](https://img.shields.io/badge/architecture-nonlinear-orange.svg)

![Standard](https://img.shields.io/badge/Standard-31--bit-blue.svg)
![Crypto](https://img.shields.io/badge/Crypto-31--bit%20SplitMix64-blue.svg)
![MonteCarlo](https://img.shields.io/badge/MonteCarlo-61--bit%20__int128-blue.svg)

---

## Что это

**CascadePRNG** — каскадный генератор псевдослучайных чисел с нелинейной архитектурой и пермутацией ролей компонент. Три режима в одном header-only файле, без внешних зависимостей.

| Режим | Разрядность | Seed | Вывод | Назначение |
|:---|:---:|:---|:---|:---|
| **Standard** | 31 бит | линейный (`b = 3a mod M`) | `a*b + c` | Симуляции, игры, образование |
| **Crypto** | 31 бит | SplitMix64 (независимый) | 3 формулы с ротацией | Ключи, nonce, генерация данных |
| **MonteCarlo** | 61 бит | SplitMix64 (независимый) | 3 формулы, `__int128` | Монте-Карло, высокая точность |

Все три режима проходят NIST SP 800-22 (188/188), DieHarder (0 FAIL) и BigCrush (Standard 160/160, Crypto и MonteCarlo 159/160 с одним маргиналом каждый).

---

## Как работает

### Каскадная динамика

Три внутренних переменных `(a, b, c)` — целые числа в диапазоне `[1, M-1]`, где `M = 2^31 - 1` — простое число Мерсенна. На каждом шаге:

- **`a`** — затухание: `a = (a * 5/9) mod M`. Множитель `5/9` — обратный элемент по модулю M.
- **`b`** — рост: `b = (b * 5/3) mod M`. Аналогично, `5/3` через обратный элемент.
- **`c`** — нелинейность: `c = (c^2 + a + b) mod M`. Квадратичный член делает последовательность нелинейной — её нельзя раскрыть матричным анализом, в отличие от LCG, MRG или Mersenne Twister.

Множители `5/9` и `5/3` заданы математической моделью каскада, а не подобраны эмпирически.

### Пермутация ролей

Каждые `N` шагов происходит циклический сдвиг ролей: `(a, b, c) -> (c, a, b)`. Переменная, которая была затуханием, становится нелинейностью; та, что была нелинейностью — ростом; та, что росла — затуханием. После трёх пермутаций роли возвращаются к исходным.

Это **гарантированный цикл по построению**, независимый от модуля. Пермутация меняет **роли операций**, а не данные — ключевое отличие от генераторов, где переставляются биты или слова.

Значение `N` по умолчанию — **49**. Внутри главного класса `RNG` каждое ядро использует своё значение: `48`, `49` и `50` — это снижает корреляцию между ядрами.

### Вывод

**Standard:**
```
output = (a * b + c) mod M
```

**Crypto (ролевая ротация):**
```
output = (a*b + c) mod M   →   (b*c + a) mod M   →   (c*a + b) mod M   →   ...
```

Три структурно идентичные формулы циклически меняются — внешний наблюдатель не знает, какая комбинация используется.

**MonteCarlo:**
```
output = (a * b + c) mod M64,   где M64 = 2^61 - 1, арифметика __int128
```

### Инициализация

**Standard** — линейный seed:
```cpp
a = (s * 5/9) mod M;
b = (s * 5/3) mod M;   // b = 3a mod M — линейная связь
c = (s^2 + 1) mod M;
```

**Crypto и MonteCarlo** — независимый seed через SplitMix64:
```cpp
a = splitmix64(state) mod M;   // независимый хеш 1
b = splitmix64(state) mod M;   // независимый хеш 2
c = splitmix64(state) mod M;   // независимый хеш 3
```

Зная `a`, нельзя восстановить `b` или `c`.

---

## Архитектура RNG

Поверх базового `CascadePRNG` построен многоуровневый генератор `RNG` с тремя независимыми ядрами:

- **AssociativityCore** (`N = 48`) — первичная генерация. Хранит состояние для автоматического ризида.
- **MeanCore** (`N = 49`) — нормализация к целевому среднему с добавлением случайного шума.
- **FantasyCore** (`N = 50`) — адаптивная коррекция: отслеживает коллизии по 32-битным raw-значениям и корректирует выход на основе битовых различий.

Дополнительно:

- **Детекция коллизий** — буфер на 49 значений, перегенерация при совпадении (до 49 попыток).
- **Каскадное порождение зёрен** — под-зёрна порождаются через временный CascadePRNG (64 бита из двух 32-битных выходов).
- **Автоматический ризид** — срабатывает по периоду ОПЖ или при исчерпании попыток.

---

## Результаты тестов

### Сводная таблица

| Батарея | Standard | Crypto | MonteCarlo | Параметры |
|:---|:---:|:---:|:---:|:---|
| **NIST SP 800-22** | **188/188** ✅ | **188/188** ✅ | **188/188** ✅ | 1000 потоков × 1М бит, α = 0.01 |
| **DieHarder** | **0 FAIL** ✅ | **0 FAIL** ✅ | **0 FAIL** ✅ | ~25 тестов, psamples = 100 |
| **BigCrush** | **160/160** ✅ | **159/160** ✅ | **159/160** ✅ | TestU01 1.2.3, seed = 40 |

### NIST SP 800-22

Все три режима — 188/188, ноль провалов. Crypto подтверждён на двух seed (40 и 12345).

| Параметр | Значение |
|:---|:---|
| Батарея | NIST SP 800-22 (SP800-22rev1a) |
| Версия sts | 2.1.2 |
| Длина последовательности | 1 000 000 бит |
| Количество потоков | 1000 |
| Общий объём | 1 000 000 000 бит (~125 МБ) |
| Уровень значимости (α) | 0.01 |
| Порог пропорции | >= 980/1000 |

| Тест | Standard | Crypto | MonteCarlo |
|:---|:---:|:---:|:---:|
| Frequency (Monobit) | 997/1000 | 989/1000 | 990/1000 |
| Block Frequency | 991/1000 | 992/1000 | 991/1000 |
| Cumulative Sums | 994, 992/1000 | 987, 986/1000 | 990, 988/1000 |
| Runs | 994/1000 | 990/1000 | 989/1000 |
| Longest Run of Ones | 995/1000 | 988/1000 | 989/1000 |
| Rank | 995/1000 | 991/1000 | 987/1000 |
| FFT | 989/1000 | 990/1000 | 987/1000 |
| NonOverlappingTemplate (148) | >= 980 | >= 980 | >= 980 |
| OverlappingTemplate | PASS | PASS | PASS |
| Universal Statistical | PASS | PASS | PASS |
| Approximate Entropy | PASS | PASS | PASS |
| Random Excursions (8) | PASS | PASS | PASS |
| Random Excursions Variant (18) | PASS | PASS | PASS |
| Serial (x2) | PASS | PASS | PASS |
| Linear Complexity | PASS | PASS | PASS |

### DieHarder

| Тест | Standard | Crypto | MonteCarlo |
|:---|:---:|:---:|:---:|
| OPERM5 (перестановки) | 0.675 PASS | 0.430 PASS | 0.552 PASS |
| 32x32 Binary Rank | 0.617 PASS | 0.120 PASS | PASS |
| Parking Lot (2D) | 0.517 PASS | 0.969 PASS | 0.518 PASS |
| Squeeze | 0.484 PASS | PASS | 0.280 PASS |
| Craps | 0.673 PASS | PASS | 0.147 PASS |
| Lagged Sums (lag=49) | 0.984 PASS | 0.669 PASS | 0.837 PASS |
| STS Monobit | PASS | PASS | PASS |
| STS Runs | PASS | PASS | PASS |
| STS Serial | PASS | PASS (2 WEAK) | PASS |
| RGB Permutations | PASS (1 WEAK) | PASS | PASS |
| diehard_sums | WEAK* | PASS | PASS |
| Остальные | PASS | PASS | PASS |

*`diehard_sums` помечен разработчиком DieHarder как "Do Not Use" — проваливают все генераторы, включая AES.

### BigCrush (TestU01 1.2.3)

| Версия | Тестов | PASS | Маргиналы | CPU time |
|:---|:---:|:---:|:---:|:---|
| Standard | 160 | **160** | 0 | 1ч 39мин |
| Crypto | 160 | **159** | 1 (ClosePairs mNP2S, p=5.5e-4) | 1ч 41мин |
| MonteCarlo | 160 | **159** | 1 (ClosePairs mNP, p=0.9994) | 2ч 01мин |

Оба маргинала — в тесте `snpair_ClosePairs`, на самом краю порога (0.001 и 0.999). Это не FAIL — статистический шум на границе.

### Сравнение с известными генераторами

| Генератор | NIST SP 800-22 | DieHarder | BigCrush | Линейность |
|:---|:---:|:---:|:---:|:---:|
| LCG (MINSTD) | 3-8 провалов | FAIL | ~96/106 | Линейный |
| xorshift128+ | 0-2 провала | FAIL | ~102/106 | Линейный |
| Mersenne Twister | 0 провалов | 2 FAIL | ~158/160 | Линейный |
| PCG32 | 0 провалов | PASS | 160/160 | Линейный |
| MRG32k3a | 0 провалов | PASS | 160/160 | Линейный |
| Philox4_32_10 | 0 провалов | PASS | 160/160 | Линейный |
| **CascadePRNG Standard** | **188/188** | **0 FAIL** | **160/160** | **Нелинейный** |
| **CascadePRNG Crypto** | **188/188** | **0 FAIL** | **159/160** | **Нелинейный** |
| **CascadePRNG MonteCarlo** | **188/188** | **0 FAIL** | **159/160** | **Нелинейный** |

---

## Использование

### Standard — симуляции, игры

```cpp
#include "perfect_random.hpp"

using namespace perfect_random;

int main() {
    CascadePRNG rng(40, 49, CascadePRNG::Mode::Standard);

    for (int i = 0; i < 100; i++) {
        double val = rng.generate();   // [0.0, 1.0)
        std::cout << val << "\n";
    }
}
```

### Crypto — ключи, nonce

```cpp
CascadePRNG rng(40, 49, CascadePRNG::Mode::Crypto);

uint32_t key = rng.generate_raw();     // 31-битное raw-значение
double val = rng.generate();           // [0.0, 1.0)
```

### MonteCarlo — высокая точность

```cpp
CascadePRNG64 rng(40, 49);

double val = rng.generate();          // [0.0, 1.0), 61 бит точности
uint64_t raw = rng.generate_u64();    // 61-битное raw-значение
long double ld = rng.generate_ld();   // long double
```

### UniformRandomBitGenerator (C++17)

```cpp
CascadePRNG rng(40, 49, CascadePRNG::Mode::Crypto);

std::uniform_int_distribution<uint32_t> dist(0, 100);
uint32_t x = dist(rng);                // работает через operator()
```

### Многоуровневый RNG

```cpp
RNG rng(40, 73.8, RNG::Mode::Crypto);  // seed, ОПЖ, режим

double val = rng.generate();           // [0.0, 1.0)
rng.reseed(12345);                     // каскадный ризид
```

### C API

```c
#include "perfect_random_c.h"

cascade_prng rng;
cascade_prng_init(&rng, 40, 49, CASCADE_MODE_CRYPTO);

double val = cascade_prng_generate(&rng);   /* [0.0, 1.0) */
uint32_t raw = cascade_prng_generate_raw(&rng);
```

### CLI — Rand++

```bash
# Standard, 1 млрд чисел, binary
Rand++.exe -n 1000000000 -e cpp-std -s 40 -f bin -o std.bin

# Crypto, seed 12345
Rand++.exe -n 1000000000 -e cpp-crypto -s 12345 -f bin -o crypto.bin

# MonteCarlo, 61-бит
Rand++.exe -n 500000000 -e cpp-mc -s 40 -f bin -o mc.bin
```

---

## Параметры

| Параметр | Тип | По умолчанию | Описание |
|:---|:---|:---|:---|
| `seed` | `uint64_t` | 1 | Начальное зерно (0 -> 1) |
| `N` | `size_t` | 49 | Период пермутации |
| `mode` | `Mode` | Standard | Standard / Crypto / MonteCarlo |
| `period` | `double` | 73.8 | ОПЖ в годах (для ризида в RNG) |

---

## Привязка к ОПЖ

Период ризида генератора определяется геометрической моделью, связанной с **ОПЖ — ожидаемой продолжительностью жизни**. Три точки трёхмерного пространства задают геометрию вращения, из которых вычисляется угловая скорость и период:

$$
\omega = \frac{\angle AOB}{t_{AB}}, \qquad T = \frac{2\pi}{\omega}
$$

Период `T` (в годах) пересчитывается в количество шагов до ризида:

$$
inc_{max} = \frac{T \times 365.25 \times 24 \times 3600}{8}
$$

При `period = 73.8` лет это даёт ~5 x 10^13 шагов — генератор работает на одном каскаде практически всё время.

Геометрические точки жёстко зафиксированы в коде:

```cpp
A      = (-3,     -0.5,  -2.5)
B      = (-5/3,   -5/6,   25/4)
O_okr  = (-2.64,  -7.91,  1.65)
```

Смена ОПЖ меняет период ризида — все ранее полученные расчёты с тем же seed будут давать иную последовательность. Это не баг, а свойство конструкции: ОПЖ задаёт «частоту обновления» генератора.

---

## К чему готов

| Область | Standard | Crypto | MonteCarlo | Почему |
|:---|:---:|:---:|:---:|:---|
| Монте-Карло, численное интегрирование | ✅ | ✅ | ✅ | Нет решёток, нет хвостовых артефактов |
| Стохастическое моделирование | ✅ | ✅ | ✅ | Нелинейность исключает корреляции LCG |
| Игры, процедурная генерация | ✅ | ✅ | ✅ | Статистическая чистота на больших объёмах |
| Бутстрэп, перестановочные тесты | ✅ | ✅ | ✅ | p-values не искажаются |
| Демографические симуляции | ✅ | ✅ | ✅ | Период ризида привязан к ОПЖ |
| Генерация ключей, nonce | ⚠️ | ✅ | ✅ | SplitMix64 + ролевая ротация |
| Монте-Карло с редкими событиями (< 10^-3) | ⚠️ | ⚠️ | ✅ | 61 бит точнее в хвостах |
| Высокопроизводительные потоки (млрд/сек) | ⚠️ | ⚠️ | ⚠️ | 5-11x медленнее mt19937 |
| Квази-Монте-Карло (Соболь, Холтон) | ❌ | ❌ | ❌ | Не квазислучайный |
| Сертифицированная криптография | ❌ | ❌ | ❌ | Требует независимого криптоанализа |

---

## Производительность

Бенчмарк: 1 000 000 000 сэмплов, MSVC `/O2`, x64.

| Генератор | нс/вызов | млн/сек | Относительно mt19937 |
|:---|:---:|:---:|:---:|
| perfect_random (C++) | 70.67 | 14.1 | 0.09x |
| perfect_random (C) | 31.14 | 32.1 | 0.20x |
| mt19937_64 + dist | 6.31 | 158.5 | 1.00x |

`perfect_random` уступает `mt19937_64` по скорости в 5-11 раз. Это ожидаемо: многоступенчатая каскадная архитектура — ассоциативные ядра, калькуляторы вращения, управление состоянием через хеш-множество. Компромисс: скорость сознательно принесена в жертву статистической надёжности.

---

## Отличия от известных генераторов

| Генератор | Линейность | Компонент | Пермутация | Множители |
|:---|:---:|:---:|:---|:---|
| LCG (MINSTD) | Линейный | 1 | нет | подобраны |
| MRG | Линейный | 3-5 | нет | подобраны |
| Mersenne Twister | Линейный | 624 | битовая | подобраны |
| xorshift | Линейный | 3 | битовая | подобраны |
| PCG32 | Линейный | 1 | нет | подобраны |
| AES-CTR | Нелинейный | блок | байтовая | стандартиз. |
| **CascadePRNG** | **Нелинейный** | **3** | **ролей** | **из модели** |

Ключевое отличие — пермутация меняет **роли операций**, а не данные.

---

## ζ-потолок и каскад

49 — не рекомендация, а **структурный предел окна памяти каскада**. При числе связей <= 49 каскад полностью их отслеживает и гасит возмущения. При превышении — избыточные связи создают некомпенсируемый шум.

ОПЖ определяет, сколько связей уже выстроено:

$$
k(t) = \min(\alpha \cdot t, \; 49), \qquad \alpha = \frac{49}{\text{ОПЖ}}
$$

| Режим связи | β | Что происходит |
|:---|:---:|:---|
| Правильные (регламент, опыт) | < 1 | Сбой гаснет на следующем узле |
| Паника (переключения, хаос) | > 1 | Один сбой -> остановка линии |
| Беспечность (игнорирование) | ~ 0 | Сбой изолирован, но брак копится |

Больше != лучше. Больше = опаснее, если превышен ζ-потолок.

---

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

[9. Каскадная геометрия и акустика Горькова, в точке "Солнце"](https://engee.com/community/ru/catalogs/projects/kaskadnaia-geometriia-i-akustika-gorkova-v-tochke-solntse)

</details>
