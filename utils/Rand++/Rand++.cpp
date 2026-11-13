// Rand++.cpp — WinAPI version (no MFC) + CLI mode
// MSHUNKO 2026
// Единый C++-интерфейс ядра perfect_random (без C-версии):
//   standard (31 бит) — cascade_prng
//   standard (61 бит) — cascade_prng64
//   crypto (31 бит)   — perfect_random::crypto (3 каскада, N = 48/49/50)
//   crypto (61 бит)   — perfect_random::crypto64 (3 каскада, N = 48/49/50)
// Модель вывода: «случайных бит на число» -> контейнер.
// Честная урезка: старшие биты сырого вывода движка, масштаб в [min, max].
#include "pch.h"
#include "resource.h"

#include "../../versions/crypto-non-crypto/perfect_random.hpp"

#include <chrono>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <iomanip>
#include <thread>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <functional>
#include <memory>
#include <commdlg.h>
#include <sstream>
#include <type_traits>
#include <cmath>
#include <cfloat>
#pragma comment(lib, "comctl32.lib")

// ── Новые ID контролов (если ещё не заведены в resource.h) ─────
#ifndef IDC_CONT_U8
#define IDC_CONT_U8         2020
#endif
#ifndef IDC_CONT_U16
#define IDC_CONT_U16        2021
#endif
#ifndef IDC_CONT_U32
#define IDC_CONT_U32        2022
#endif
#ifndef IDC_CONT_U64
#define IDC_CONT_U64        2023
#endif
#ifndef IDC_CONT_F32
#define IDC_CONT_F32        2024
#endif
#ifndef IDC_CONT_F64
#define IDC_CONT_F64        2025
#endif
#ifndef IDC_CONT_F80
#define IDC_CONT_F80        2026
#endif
#ifndef IDC_BITS_EDIT
#define IDC_BITS_EDIT       2027
#endif
#ifndef IDC_BITS_SPIN
#define IDC_BITS_SPIN       2028
#endif
#ifndef IDS_BITS_RANGE
#define IDS_BITS_RANGE      1052
#endif
#ifndef IDS_CONTAINER_BITS
#define IDS_CONTAINER_BITS  1053
#endif
#ifndef IDS_RANGE_INVALID
#define IDS_RANGE_INVALID   1054
#endif

// ── Custom messages ─────────────────────────────────────────────
#define WM_UPDATE_PROGRESS   (WM_APP + 1)
#define WM_TASK_COMPLETE     (WM_APP + 2)
#define WM_TASK_ERROR        (WM_APP + 3)
#define WM_UPDATE_SPEED     (WM_APP + 4)

// ── Globals ─────────────────────────────────────────────────────
static HINSTANCE g_hInstance = nullptr;

// ── Модель вывода: биты -> контейнер ────────────────────────────
enum class Container { U8 = 0, U16, U32, U64, F32, F64, F80 };

enum class EngineType {
    Std31,      // cascade_prng,  31 бит
    Std61,      // cascade_prng64, 61 бит
    Crypto31,   // perfect_random::crypto   (3 каскада)
    Crypto61    // perfect_random::crypto64 (3 каскада)
};

// Ширина сырого вывода crypto64 определяется компилятором по сигнатуре
// generate_raw(): сейчас в ядре она uint32_t (32 бита) — после замены
// на uint64_t приложение автоматически поднимет движок до 61 бита.
using crypto64_raw_t = decltype(std::declval<perfect_random::crypto64&>().generate_raw());
static constexpr uint32_t CRYPTO64_BITS =
    std::is_same<crypto64_raw_t, std::uint64_t>::value ? 61u : 32u;

static uint32_t EngineBits(EngineType e) {
    switch (e) {
    case EngineType::Std31:    return 31;
    case EngineType::Std61:    return 61;
    case EngineType::Crypto31: return 31;
    case EngineType::Crypto61: return CRYPTO64_BITS;
    }
    return 31;
}

static std::wstring EngineToStr(EngineType e) {
    switch (e) {
    case EngineType::Std31:    return L"standard (31 бит)";
    case EngineType::Std61:    return L"standard (61 бит)";
    case EngineType::Crypto31: return L"crypto (31 бит)";
    case EngineType::Crypto61:
        return (CRYPTO64_BITS >= 61) ? L"crypto (61 бит)" : L"crypto (32 бита)";
    }
    return L"?";
}

static const wchar_t* ContainerToStr(Container c) {
    switch (c) {
    case Container::U8:  return L"byte";
    case Container::U16: return L"word";
    case Container::U32: return L"dword";
    case Container::U64: return L"qword";
    case Container::F32: return L"float";
    case Container::F64: return L"double";
    case Container::F80: return L"long double";
    }
    return L"dword";
}

// Ширина мантиссы/диапазона контейнера (для валидации бит)
static uint32_t ContainerBits(Container c) {
    switch (c) {
    case Container::U8:  return 8;
    case Container::U16: return 16;
    case Container::U32: return 32;
    case Container::U64: return 64;
    case Container::F32: return 24;   // мантисса float
    case Container::F64: return 53;   // мантисса double
    case Container::F80: return 53;   // MSVC: long double == double
    }
    return 32;
}

static uint32_t ContainerCapBits(Container c) {
#if defined(__MINGW32__) || defined(__GNUC__)
    // GCC/MinGW: long double — 80-битный x87, мантисса 64 бита
    if (c == Container::F80) return 64;
#endif
    return ContainerBits(c);
}

static size_t ContainerSize(Container c) {
    switch (c) {
    case Container::U8:  return 1;
    case Container::U16: return 2;
    case Container::U32: return 4;
    case Container::U64: return 8;
    case Container::F32: return 4;
    case Container::F64: return 8;
    case Container::F80: return sizeof(long double);
    }
    return 4;
}

// ── Thread parameters ───────────────────────────────────────────
struct GenParams {
    HWND hDlg = nullptr;
    unsigned long long serie_count = 10000000;
    long double serie_min = 0.0L;
    long double serie_max = 4294967295.0L;
    uint32_t out_bits = 31;          // 1..EngineBits(engine)
    Container container = Container::U32;
    bool binary_format = true;
    std::wstring filename = L"output.bin";
    double period = 42.0;
    EngineType engine = EngineType::Std31;
    unsigned long long seed = 0;     // 0 = по времени
};

// ── Helpers ─────────────────────────────────────────────────────
static std::wstring GetExecutableDirectory() {
    wchar_t szPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, szPath, MAX_PATH);
    std::wstring path(szPath);
    size_t pos = path.find_last_of(L'\\');
    return (pos != std::wstring::npos) ? path.substr(0, pos) : path;
}

static std::wstring LoadStr(UINT uID) {
    wchar_t buf[1024];
    int len = LoadStringW(g_hInstance, uID, buf, 1024);
    return std::wstring(buf, len);
}

// SplitMix64: расщепление сида на три независимых для crypto-классов
static std::uint64_t SplitMix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// ── Generator wrapper — все 4 движка в одном ────────────────────
struct GenWrapper {
    EngineType engine;
    std::unique_ptr<perfect_random::cascade_prng>    g31;
    std::unique_ptr<perfect_random::cascade_prng64>  g61;
    std::unique_ptr<perfect_random::crypto>          cr31;
    std::unique_ptr<perfect_random::crypto64>        cr61;
    uint32_t ebits = 31;

    GenWrapper(EngineType eng, uint64_t seed, double period) : engine(eng) {
        size_t N = 49;
        // period -> N: период вращения -> количество шагов в цикле
        if (period >= 1.0) N = (size_t)period;

        std::uint64_t st = seed | 1;
        std::uint64_t s1 = SplitMix64(st);
        std::uint64_t s2 = SplitMix64(st);
        std::uint64_t s3 = SplitMix64(st);

        switch (eng) {
        case EngineType::Std31:
            g31 = std::make_unique<perfect_random::cascade_prng>(seed, N);
            break;
        case EngineType::Std61:
            g61 = std::make_unique<perfect_random::cascade_prng64>(seed, N);
            break;
        case EngineType::Crypto31:
            cr31 = std::make_unique<perfect_random::crypto>(s1, s2, s3);
            break;
        case EngineType::Crypto61:
            cr61 = std::make_unique<perfect_random::crypto64>(s1, s2, s3);
            break;
        }
        ebits = EngineBits(eng);
    }

    // Сырое значение в [0, 2^ebits). Для crypto-движков приведение по
    // модулю Мерсенна гасит структурный перекос старших бит смешивания.
    uint64_t next_raw() {
        switch (engine) {
        case EngineType::Std31:
            return (uint64_t)g31->generate_raw();
        case EngineType::Std61:
            return g61->generate_raw();
        case EngineType::Crypto31:
            return (uint64_t)(cr31->generate_raw()
                % (uint32_t)perfect_random::cascade_prng::M);
        case EngineType::Crypto61:
            if (CRYPTO64_BITS >= 61)
                return (uint64_t)(cr61->generate_raw()
                    % perfect_random::cascade_prng64::M);
            return (uint64_t)cr61->generate_raw();
        }
        return 0;
    }
};

// ── Core generation ─────────────────────────────────────────────
static long double ClampL(long double v, long double lo, long double hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static int FloatPrecision(Container c) {
    switch (c) {
    case Container::F32: return 9;
    case Container::F64: return 17;
    case Container::F80:
#if defined(__MINGW32__)
        return 21;
#else
        return 17;
#endif
    default: return 17;
    }
}

static bool GenerateCore(const GenParams& p, std::wstring& errMsg,
    std::function<void(size_t, double)> progressFn = nullptr) {

    if (p.serie_count == 0) { errMsg = L"count";     return false; }
    if (!(p.serie_min < p.serie_max)) { errMsg = L"minmax"; return false; }

    // Ширина вывода против ширины движка
    const uint32_t ebits = EngineBits(p.engine);
    if (p.out_bits == 0 || p.out_bits > ebits) { errMsg = L"bits"; return false; }

    // Ширина вывода против контейнера
    const uint32_t ccap = ContainerCapBits(p.container);
    if (p.out_bits > ccap) { errMsg = L"container"; return false; }

    // Диапазон против контейнера
    const bool is_int = (int)p.container <= (int)Container::U64;
    if (is_int) {
        if (p.serie_min < 0.0L) { errMsg = L"range"; return false; }
        static const long double cmax[] = {
            255.0L, 65535.0L, 4294967295.0L,
            18446744073709551615.0L, 0.0L, 0.0L, 0.0L };
        if (p.serie_max > cmax[(int)p.container]) { errMsg = L"range"; return false; }
    } else {
        if (!std::isfinite((double)p.serie_min) ||
            !std::isfinite((double)p.serie_max)) { errMsg = L"range"; return false; }
        if (p.container == Container::F32 &&
            (fabsl(p.serie_min) > (long double)FLT_MAX ||
             fabsl(p.serie_max) > (long double)FLT_MAX)) { errMsg = L"range"; return false; }
    }

    uint64_t seed = p.seed ? (uint64_t)p.seed : (uint64_t)time(nullptr);
    GenWrapper gen(p.engine, seed, p.period);

    const size_t pull_size = ContainerSize(p.container);
    const size_t updateInterval = (p.serie_count >= 100)
        ? (size_t)(p.serie_count / 100) : 1;

    auto t_start = std::chrono::steady_clock::now();
    auto reportProgress = [&](size_t i) {
        if (progressFn && (i + 1) % updateInterval == 0) {
            auto t_now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(t_now - t_start).count();
            double bytes_written = static_cast<double>(i + 1) * pull_size;
            double speed_mbps = (elapsed > 0.0)
                ? (bytes_written / elapsed / (1024.0 * 1024.0)) : 0.0;
            progressFn(i + 1, speed_mbps);
        }
    };

    const long double lo = p.serie_min, hi = p.serie_max;
    const long double denom_int  = (long double)(((unsigned long long)1 << p.out_bits) - 1);
    const long double denom_frac = ldexpl(1.0L, (int)p.out_bits);
    const int shift = (p.out_bits < ebits) ? (int)(ebits - p.out_bits) : 0;

    // --- Binary mode ---
    if (p.binary_format) {
        std::ofstream file(p.filename, std::ios::binary);
        if (!file.is_open()) { errMsg = L"file"; return false; }

        for (size_t i = 0; i < p.serie_count; ++i) {
            uint64_t raw = gen.next_raw();
            uint64_t k = shift ? (raw >> shift) : raw;

            if (is_int) {
                long double v = lo + (long double)k / denom_int * (hi - lo);
                unsigned long long q =
                    (unsigned long long)floorl(ClampL(v, lo, hi) + 0.5L);
                switch (p.container) {
                case Container::U8: {
                    uint8_t x = (uint8_t)q;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                case Container::U16: {
                    uint16_t x = (uint16_t)q;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                case Container::U32: {
                    uint32_t x = (uint32_t)q;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                case Container::U64: {
                    uint64_t x = (uint64_t)q;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                default: break;
                }
            } else {
                long double v = lo + (long double)k / denom_frac * (hi - lo);
                switch (p.container) {
                case Container::F32: {
                    float x = (float)v;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                case Container::F64: {
                    double x = (double)v;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                case Container::F80: {
                    long double x = v;
                    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
                    break;
                }
                default: break;
                }
            }
            reportProgress(i);
        }
    }
    // --- Text mode ---
    else {
        std::ofstream file(p.filename);
        if (!file.is_open()) { errMsg = L"file"; return false; }

        if (!is_int)
            file << std::fixed << std::setprecision(FloatPrecision(p.container));

        for (size_t i = 0; i < p.serie_count; ++i) {
            uint64_t raw = gen.next_raw();
            uint64_t k = shift ? (raw >> shift) : raw;

            if (is_int) {
                long double v = lo + (long double)k / denom_int * (hi - lo);
                unsigned long long q =
                    (unsigned long long)floorl(ClampL(v, lo, hi) + 0.5L);
                switch (p.container) {
                case Container::U8:  file << (unsigned)(uint8_t)q;  break;
                case Container::U16: file << (unsigned)(uint16_t)q; break;
                case Container::U32: file << (uint32_t)q;           break;
                case Container::U64: file << (uint64_t)q;           break;
                default: break;
                }
            } else {
                long double v = lo + (long double)k / denom_frac * (hi - lo);
                switch (p.container) {
                case Container::F32: file << (float)v;          break;
                case Container::F64: file << (double)v;         break;
                case Container::F80: file << v;                 break;
                default: break;
                }
            }
            if (i + 1 < p.serie_count) file << "\n";
            reportProgress(i);
        }
    }

    return true;
}

// ── CLI ─────────────────────────────────────────────────────────
static int RunCLI(int argc, LPWSTR* argv) {
    GenParams p;
    bool help = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        auto val = [&]() -> std::wstring {
            if (i + 1 < argc) return argv[++i];
            return L"";
        };

        if (arg == L"-h" || arg == L"--help") { help = true; }
        else if (arg == L"-n" || arg == L"--count")
            p.serie_count = wcstoull(val().c_str(), nullptr, 10);
        else if (arg == L"--min")
            p.serie_min = wcstold(val().c_str(), nullptr);
        else if (arg == L"--max")
            p.serie_max = wcstold(val().c_str(), nullptr);
        else if (arg == L"-w" || arg == L"--bits")
            p.out_bits = wcstoul(val().c_str(), nullptr, 10);
        else if (arg == L"-c" || arg == L"--container") {
            std::wstring v = val();
            if      (v == L"byte"  || v == L"8")   p.container = Container::U8;
            else if (v == L"word"  || v == L"16")  p.container = Container::U16;
            else if (v == L"dword" || v == L"32")  p.container = Container::U32;
            else if (v == L"qword" || v == L"64")  p.container = Container::U64;
            else if (v == L"float" || v == L"f32") p.container = Container::F32;
            else if (v == L"double"|| v == L"f64") p.container = Container::F64;
            else if (v == L"long-double" || v == L"ld" || v == L"f80")
                p.container = Container::F80;
        }
        else if (arg == L"-e" || arg == L"--engine") {
            std::wstring v = val();
            if      (v == L"std31" || v == L"std" || v == L"cpp")       p.engine = EngineType::Std31;
            else if (v == L"std61" || v == L"mc"  || v == L"std-mc")    p.engine = EngineType::Std61;
            else if (v == L"crypto31" || v == L"crypto")                p.engine = EngineType::Crypto31;
            else if (v == L"crypto61" || v == L"crypto-mc")             p.engine = EngineType::Crypto61;
        }
        else if (arg == L"-p" || arg == L"--period") {
            p.period = wcstod(val().c_str(), nullptr);
            if (p.period < 1.0) p.period = 42.0;
        }
        else if (arg == L"--seed")
            p.seed = wcstoull(val().c_str(), nullptr, 10);
        else if (arg == L"-f" || arg == L"--format")
            p.binary_format = (val() != L"txt");
        else if (arg == L"-o" || arg == L"--out")
            p.filename = val();
    }

    if (help) {
        std::wcout <<
            L"Rand++ - генератор псевдослучайных чисел (perfect_random)\n\n"
            L"  -n, --count N       количество чисел (default: 10000000)\n"
            L"  --min V             нижняя граница (default: 0)\n"
            L"  --max V             верхняя граница (default: 4294967295)\n"
            L"  -w, --bits N        случайных бит на число: 1..31 (std31/crypto31)\n"
            L"                      или 1..61 (std61/crypto61)\n"
            L"  -c, --container T   byte | word | dword | qword | float | double |\n"
            L"                      long-double (default: dword)\n"
            L"  -e, --engine ENG    std31 | std61 | crypto31 | crypto61\n"
            L"                      (алиасы: std, mc = std61, crypto = crypto31)\n"
            L"  -p, --period N      период вращения каскада (default: 42)\n"
            L"      --seed N        сид (default: текущее время)\n"
            L"  -f, --format FMT    bin или txt (default: bin)\n"
            L"  -o, --out FILE      имя файла (default: output.bin)\n"
            L"  -h, --help          эта справка\n";
        return 0;
    }

    // Resolve relative paths
    if (p.filename.find_first_of(L"\\/:") == std::wstring::npos)
        p.filename = GetExecutableDirectory() + L"\\" + p.filename;

    std::wcout << L"Generating " << p.serie_count << L" values"
        << L" (engine=" << EngineToStr(p.engine)
        << L", bits=" << p.out_bits
        << L", container=" << ContainerToStr(p.container)
        << L", format=" << (p.binary_format ? L"bin" : L"txt")
        << L", period=" << p.period
        << L") -> " << p.filename << std::endl;

    std::wstring errMsg;
    auto cliProgress = [](size_t processed, double speed_mbps) {
        fprintf(stderr, "\r  %zu values  |  %.2f MB/s",
            processed, speed_mbps);
        fflush(stderr);
    };

    if (GenerateCore(p, errMsg, cliProgress)) {
        fprintf(stderr, "\n");
        std::wcout << L"Done: " << p.filename << std::endl;
        return 0;
    }

    if (errMsg == L"count")
        std::wcerr << L"Error: count must be > 0" << std::endl;
    else if (errMsg == L"minmax")
        std::wcerr << L"Error: min must be < max" << std::endl;
    else if (errMsg == L"bits")
        std::wcerr << L"Error: bits must be in 1.." << EngineBits(p.engine) << std::endl;
    else if (errMsg == L"container")
        std::wcerr << L"Error: container " << ContainerToStr(p.container)
                   << L" cannot hold " << p.out_bits << L" bits" << std::endl;
    else if (errMsg == L"range")
        std::wcerr << L"Error: [min, max] incompatible with container" << std::endl;
    else if (errMsg == L"file")
        std::wcerr << L"Error: cannot open file" << std::endl;
    else
        std::wcerr << L"Error: " << errMsg << std::endl;
    return 1;
}

// ── Generate thread ─────────────────────────────────────────────
static void GenerateThread(GenParams* params) {
    HWND hDlg = params->hDlg;

    auto progressFn = [hDlg, params](size_t processed, double speed_mbps) {
        if (IsWindow(hDlg)) {
            PostMessageW(hDlg, WM_UPDATE_PROGRESS,
                static_cast<WPARAM>(processed * 100 / params->serie_count), 0);
            PostMessageW(hDlg, WM_UPDATE_SPEED,
                static_cast<WPARAM>(static_cast<int>(speed_mbps * 100.0)), 0);
        }
        fprintf(stderr, "\r  %zu values  |  %.2f MB/s",
            processed, speed_mbps);
        fflush(stderr);
    };

    std::wstring errMsg;
    bool ok = GenerateCore(*params, errMsg, progressFn);

    if (!ok) {
        if (IsWindow(hDlg)) {
            UINT id = (errMsg == L"count")     ? IDS_MIN_SERIE_COUNT :
                      (errMsg == L"minmax")    ? IDS_MIN_MAX_CONDITION :
                      (errMsg == L"bits")      ? IDS_BITS_RANGE :
                      (errMsg == L"container") ? IDS_CONTAINER_BITS :
                      (errMsg == L"range")     ? IDS_RANGE_INVALID :
                                                 IDS_FILE_ERROR;
            std::wstring* pMsg = new std::wstring(LoadStr(id));
            PostMessageW(hDlg, WM_TASK_ERROR, 0,
                reinterpret_cast<LPARAM>(pMsg));
        }
    } else {
        if (IsWindow(hDlg))
            PostMessageW(hDlg, WM_TASK_COMPLETE, 0, 0);
    }
    delete params;
}

// ── About dialog ────────────────────────────────────────────────
static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_ABOUT_TEXT,
            L"Генератор псевдослучайных чисел на основе каскадной модели.\r\n"
            L"Автор: Шунько Михаил Геннадьевич\r\n"
            L"Проект: «Каскадная модель развёртки» (2021–2026)\r\n\r\n");
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ─── Тексты справки по ID контрола ──────────────────────────────
static std::wstring GetHelpText(int controlId) {
    switch (controlId) {

        // ─── Параметры ГПСЧ ─────────────────────────────────────

    case IDC_STATIC:
        return L"Параметры генератора псевдослучайных чисел perfect_random. "
            L"Настройте движок, ширину вывода и контейнер перед запуском.";

    case IDC_SERIES_PERIOD:
        return L"τ — период серии. Количество значений, генерируемых "
            L"за один проход ГПСЧ. Связан с каскадной моделью: "
            L"минимум 57, рекомендованный порог 99% — 62.6, "
            L"оптимальный — равен ОПЖ популяции.";

    case IDC_SERIES_COUNT:
        return L"Количество чисел в серии. Определяет, сколько "
            L"случайных значений будет сгенерировано за один запуск.";

    case IDC_ENGINE_LIST:
        return L"Движок ГПСЧ: standard или crypto, ширина ядра 31 или 61 бит. "
            L"Ширина движка ограничивает максимально доступные "
            L"«биты на число».";

    case IDC_BITS_EDIT:
    case IDC_BITS_SPIN:
        return L"Количество случайных бит на одно число. Определяет "
            L"разрешение генерации: контейнеры, не вмещающие выбранную "
            L"ширину, гасятся автоматически.";

        // ─── Контейнер ──────────────────────────────────────────

    case IDC_CONT_U8:
        return L"byte: целые 0..255 (1 байт).";
    case IDC_CONT_U16:
        return L"word: целые 0..65535 (2 байта).";
    case IDC_CONT_U32:
        return L"dword: целые 0..4294967295 (4 байта).";
    case IDC_CONT_U64:
        return L"qword: целые 0..18446744073709551615 (8 байт).";
    case IDC_CONT_F32:
        return L"float: дробные числа, мантисса 24 бита, диапазон min..max.";
    case IDC_CONT_F64:
        return L"double: дробные числа, мантисса 53 бита, диапазон min..max.";
    case IDC_CONT_F80:
        return L"long double: на MinGW — 80-битный x87 (мантисса 64 бита), "
            L"на MSVC — эквивалент double (53 бита).";

        // ─── Формат, диапазон, файл ─────────────────────────────

    case IDC_TYPE_TEXT:
        return L"Текстовый формат файла. Значения выводятся построчно, "
            L"с точностью, соответствующей контейнеру.";

    case IDC_TYPE_BINARY:
        return L"Бинарный формат файла. Результат сохраняется как "
            L"сырые байты контейнера без текстового представления.";

    case IDC_SERIE_MIN:
        return L"Минимальное значение диапазона генерации. "
            L"Для целочисленных контейнеров — не меньше 0.";

    case IDC_SERIE_MAX:
        return L"Максимальное значение диапазона генерации. "
            L"Для целочисленных контейнеров — не больше ёмкости контейнера.";

    case IDC_SERIE_FILENAME:
        return L"Имя выходного файла. Если не указано — используется "
            L"имя по умолчанию. Расширение зависит от формата: "
            L".txt для текстового, .bin для бинарного.";

    case IDC_SPEED_LABEL:
        return L"Скорость потока генерации — байт в секунду. "
            L"Обновляется в реальном времени во время генерации.";

    case IDC_PROGRESS_BAR:
        return L"Индикатор прогресса генерации. Показывает, какая часть "
            L"серии уже сгенерирована. От 0 до 100%.";

    case ID_SERIE_GENERATION_START:
        return L"СТАРТ — запуск генерации серии случайных чисел "
            L"с выбранными параметрами.";

    case IDC_SHOW_ABOUT:
        return L"Сведения о программе и авторе.";
    }
    return L"";
}

// ─── Обработчик WM_HELP ─────────────────────────────────────────
static BOOL OnHelp(WPARAM, LPARAM lParam) {
    HELPINFO* phi = reinterpret_cast<HELPINFO*>(lParam);
    if (phi->iContextType != HELPINFO_WINDOW || phi->hItemHandle == nullptr)
        return TRUE;

    int controlId = static_cast<int>(phi->iCtrlId);
    std::wstring helpText = GetHelpText(controlId);
    std::wstring title = L"Справка";

    MessageBoxW((HWND)phi->hItemHandle, helpText.c_str(),
        title.c_str(), MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

// ── UI state: спиннер и доступность контейнеров ─────────────────
static void UpdateUIState(HWND hDlg) {
    int sel = (int)SendDlgItemMessageW(hDlg, IDC_ENGINE_LIST, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    uint32_t ebits = EngineBits((EngineType)sel);

    // Спиннер: 1..ebits
    SendDlgItemMessageW(hDlg, IDC_BITS_SPIN, UDM_SETRANGE32, 1, (LPARAM)ebits);
    UINT bits = GetDlgItemInt(hDlg, IDC_BITS_EDIT, nullptr, FALSE);
    if (bits == 0)                bits = (ebits >= 31) ? 31 : ebits;
    if (bits > ebits)             bits = ebits;
    SetDlgItemInt(hDlg, IDC_BITS_EDIT, bits, FALSE);

    // Контейнеры: гасим те, что не вмещают выбранную ширину
    int firstEnabled = -1;
    for (int c = 0; c < 7; ++c) {
        bool en = ContainerCapBits((Container)c) >= bits;
        EnableWindow(GetDlgItem(hDlg, IDC_CONT_U8 + c), en ? TRUE : FALSE);
        if (en && firstEnabled < 0) firstEnabled = c;
    }
    int checked = -1;
    for (int c = 0; c < 7; ++c)
        if (IsDlgButtonChecked(hDlg, IDC_CONT_U8 + c) == BST_CHECKED) { checked = c; break; }
    if (checked < 0 || ContainerCapBits((Container)checked) < bits)
        CheckRadioButton(hDlg, IDC_CONT_U8, IDC_CONT_F80,
            IDC_CONT_U8 + (firstEnabled >= 0 ? firstEnabled : 0));
}

// ── Main dialog ─────────────────────────────────────────────────
static INT_PTR CALLBACK RandDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HICON hIcon = nullptr;
    switch (msg) {

    case WM_INITDIALOG: {
        hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDR_MAINFRAME));
        if (hIcon) {
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }

        HMENU hSysMenu = GetSystemMenu(hDlg, FALSE);
        if (hSysMenu) {
            AppendMenuW(hSysMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hSysMenu, MF_STRING, IDM_ABOUTBOX, LoadStr(IDS_ABOUTBOX).c_str());
        }

        const wchar_t* counts[] = {
            L"10", L"100", L"1000", L"10000",
            L"134217728", L"268435456", L"536870912", L"1668467902"
        };
        for (auto s : counts)
            SendDlgItemMessageW(hDlg, IDC_SERIES_COUNT, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(s));
        SendDlgItemMessageW(hDlg, IDC_SERIES_COUNT, CB_SETCURSEL, 3, 0);

        const wchar_t* periods[] = { L"42", L"49", L"57", L"62.6" };
        for (auto s : periods)
            SendDlgItemMessageW(hDlg, IDC_SERIES_PERIOD, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(s));
        SendDlgItemMessageW(hDlg, IDC_SERIES_PERIOD, CB_SETCURSEL, 0, 0);

        // Движки: порядок совпадает с EngineType
        for (int e = 0; e < 4; ++e) {
            std::wstring name = EngineToStr((EngineType)e);
            SendDlgItemMessageW(hDlg, IDC_ENGINE_LIST, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendDlgItemMessageW(hDlg, IDC_ENGINE_LIST, CB_SETCURSEL, 0, 0);

        const wchar_t* maxes[] = { L"4294967295", L"255", L"65535",
            L"18446744073709551615", L"1", L"10" };
        for (auto s : maxes)
            SendDlgItemMessageW(hDlg, IDC_SERIE_MAX, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(s));
        SendDlgItemMessageW(hDlg, IDC_SERIE_MAX, CB_SETCURSEL, 0, 0);

        SetDlgItemTextW(hDlg, IDC_SERIE_MIN, L"0");
        SetDlgItemTextW(hDlg, IDC_SERIE_FILENAME, L"output.bin");

        CheckRadioButton(hDlg, IDC_TYPE_TEXT, IDC_TYPE_BINARY, IDC_TYPE_BINARY);
        CheckRadioButton(hDlg, IDC_CONT_U8, IDC_CONT_F80, IDC_CONT_U32);
        SetDlgItemInt(hDlg, IDC_BITS_EDIT, 31, FALSE);
        SetDlgItemTextW(hDlg, IDC_SPEED_LABEL, L"");

        UpdateUIState(hDlg);
        return TRUE;
    }

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == IDM_ABOUTBOX) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTBOX), hDlg, AboutDlgProc);
            return TRUE;
        }
        break;

    case WM_HELP:
        return OnHelp(wParam, lParam);

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SHOW_ABOUT:
            DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTBOX), hDlg, AboutDlgProc);
            return TRUE;

        case IDC_ENGINE_LIST:
            if (HIWORD(wParam) == CBN_SELCHANGE)
                UpdateUIState(hDlg);
            return TRUE;

        case IDC_CONT_U8: case IDC_CONT_U16: case IDC_CONT_U32:
        case IDC_CONT_U64: case IDC_CONT_F32: case IDC_CONT_F64: case IDC_CONT_F80:
            return TRUE;

        case ID_SERIE_GENERATION_START: {
            GenParams* p = new GenParams;
            p->hDlg = hDlg;

            wchar_t buf[512];
            GetDlgItemTextW(hDlg, IDC_SERIES_PERIOD, buf, 256);
            p->period = _wtof(buf);
            if (p->period < 1.0) p->period = 42.0;

            GetDlgItemTextW(hDlg, IDC_SERIES_COUNT, buf, 256);
            p->serie_count = wcstoull(buf, nullptr, 10);

            GetDlgItemTextW(hDlg, IDC_SERIE_MIN, buf, 256);
            p->serie_min = wcstold(buf, nullptr);

            GetDlgItemTextW(hDlg, IDC_SERIE_MAX, buf, 256);
            p->serie_max = wcstold(buf, nullptr);

            GetDlgItemTextW(hDlg, IDC_SERIE_FILENAME, buf, 256);
            p->filename = GetExecutableDirectory() + L"\\" + buf;

            p->binary_format = IsDlgButtonChecked(hDlg, IDC_TYPE_BINARY) == BST_CHECKED;

            int sel = (int)SendDlgItemMessageW(hDlg, IDC_ENGINE_LIST, CB_GETCURSEL, 0, 0);
            if (sel < 0) sel = 0;
            p->engine = (EngineType)sel;

            p->out_bits = GetDlgItemInt(hDlg, IDC_BITS_EDIT, nullptr, FALSE);

            int c = 0;
            for (int idc = IDC_CONT_U8; idc <= IDC_CONT_F80; ++idc)
                if (IsDlgButtonChecked(hDlg, idc) == BST_CHECKED) {
                    c = idc - IDC_CONT_U8;
                    break;
                }
            p->container = (Container)c;

            std::thread(GenerateThread, p).detach();
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_UPDATE_PROGRESS:
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, wParam, 0);
        return TRUE;

    case WM_TASK_COMPLETE:
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, 0, 0);
        SetDlgItemTextW(hDlg, IDC_SPEED_LABEL, L"");
        return TRUE;

    case WM_UPDATE_SPEED: {
        double speed_mbps = static_cast<double>(wParam) / 100.0;
        wchar_t speedText[64];
        if (speed_mbps < 1.0)
            swprintf_s(speedText, L"%.2f KB/s", speed_mbps * 1024.0);
        else
            swprintf_s(speedText, L"%.2f MB/s", speed_mbps);
        SetDlgItemTextW(hDlg, IDC_SPEED_LABEL, speedText);
        return TRUE;
    }

    case WM_TASK_ERROR: {
        std::wstring* pErr = reinterpret_cast<std::wstring*>(lParam);
        if (pErr) {
            MessageBoxW(hDlg, pErr->c_str(), L"Error", MB_ICONERROR);
            delete pErr;
        }
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, 0, 0);
        return TRUE;
    }

    case WM_PAINT:
        if (IsIconic(hDlg)) {
            PAINTSTRUCT ps;
            HDC hDC = BeginPaint(hDlg, &ps);
            SendMessageW(hDlg, WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(hDC), 0);
            int cx = GetSystemMetrics(SM_CXICON);
            int cy = GetSystemMetrics(SM_CYICON);
            RECT rc; GetClientRect(hDlg, &rc);
            DrawIcon(hDC, (rc.right - cx + 1) / 2, (rc.bottom - cy + 1) / 2, hIcon);
            EndPaint(hDlg, &ps);
            return TRUE;
        }
        break;

    case WM_QUERYDRAGICON:
        return reinterpret_cast<INT_PTR>(hIcon);

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    }

    return FALSE;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInstance = hInstance;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv && argc > 1) {
        int ret = RunCLI(argc, argv);
        LocalFree(argv);
        return ret;
    }
    if (argv) LocalFree(argv);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_RAND_DIALOG), nullptr, RandDlgProc);
    return 0;
}
