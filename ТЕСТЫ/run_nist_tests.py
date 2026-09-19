#!/usr/bin/env python3
"""NIST SP 800-22 statistical tests for Rand++ generator — multi-sequence version.

Follows NIST SP 800-22 methodology:
  - Split bit stream into N sequences
  - Run each test on each sequence independently
  - Proportion test: at least (1 - alpha - 3*sqrt(alpha*(1-alpha)/N)) * N sequences must pass
  - For alpha=0.01, N=10: at least 9/10 must pass each test
"""
import sp80022suite
import sys
import os
import math
import numpy as np
from scipy.stats import chi2
from scipy.special import gammaincc


# ═══════════════════════════════════════════════════════════════
#  Manual test implementations
# ═══════════════════════════════════════════════════════════════
#  Convention: fn(params..., bits) — bits is ALWAYS the last argument,
#  matching sp80022suite API.

def _build_cycles(bits):
    """Build cumulative-sum cycles (zero crossings) for Random Excursions tests."""
    x = [(b * 2) - 1 for b in bits]
    s = []
    pos = 0
    for e in x:
        pos += e
        s.append(pos)
    sprime = [0] + s + [0]
    cycles = []
    pos = 1
    while pos < len(sprime):
        cycle = [0]
        while pos < len(sprime) and sprime[pos] != 0:
            cycle.append(sprime[pos])
            pos += 1
        if pos < len(sprime):
            cycle.append(0)
            cycles.append(cycle)
        pos += 1
    return cycles


def random_excursions_test(bits):
    """NIST SP 800-22 Random Excursions Test."""
    cycles = _build_cycles(bits)
    J = len(cycles)
    if J < 500:
        raise ValueError(f"Insufficient cycles (J={J}, need >= 500)")

    pixk = [
        [0.5,    0.25,   0.125,  0.0625, 0.0312, 0.0312],
        [0.75,   0.0625, 0.0469, 0.0352, 0.0264, 0.0791],
        [0.8333, 0.0278, 0.0231, 0.0193, 0.0161, 0.0804],
        [0.875,  0.0156, 0.0137, 0.012,  0.0105, 0.0733],
    ]
    states = [-4, -3, -2, -1, 1, 2, 3, 4]
    p_values = []
    for x in states:
        abs_x = abs(x) - 1
        vxk = [0] * 6
        for cycle in cycles:
            occ = sum(1 for v in cycle if v == x)
            if occ < 5:
                vxk[occ] += 1
            else:
                vxk[5] += 1
        chisq = 0.0
        for k in range(6):
            top = float(vxk[k]) - (float(J) * pixk[abs_x][k])
            top = top * top
            bottom = J * pixk[abs_x][k]
            if bottom > 0:
                chisq += top / bottom
        p = gammaincc(5.0 / 2.0, chisq / 2.0)
        p_values.append(p)
    return min(p_values)


def random_excursions_variant_test(bits):
    """NIST SP 800-22 Random Excursions Variant Test."""
    cycles = _build_cycles(bits)
    J = len(cycles)
    if J < 500:
        raise ValueError(f"Insufficient cycles (J={J}, need >= 500)")

    states = list(range(-9, 0)) + list(range(1, 10))
    p_values = []
    for x in states:
        xi = sum(1 for cycle in cycles for v in cycle if v == x)
        mean = J
        variance = J * (4 * abs(x) - 2)
        if variance <= 0:
            p_values.append(1.0)
            continue
        z = (xi - mean) / math.sqrt(variance)
        p = math.erfc(abs(z) / math.sqrt(2.0))
        p_values.append(p)
    return min(p_values)


def non_overlapping_template_test(m, bits):
    """NIST SP 800-22 Non-Overlapping Template Matching Test.
    NOTE: signature is (m, bits) to match sp80022suite convention.
    """
    n = len(bits)
    N = 8
    M = n // N

    templates = [
        [0,0,0,0,0,0,0,0,1], [0,0,0,0,0,0,1,0,1], [0,0,0,0,0,1,0,0,1],
        [0,0,0,0,1,0,0,0,1], [0,0,0,1,0,0,0,0,1], [0,0,1,0,0,0,0,0,1],
        [0,1,0,0,0,0,0,0,1], [1,0,0,0,0,0,0,0,1], [0,0,0,0,0,0,1,1,1],
        [0,0,0,0,0,1,1,0,1], [0,0,0,0,1,1,0,0,1], [0,0,0,1,1,0,0,0,1],
        [0,0,1,1,0,0,0,0,1], [0,1,1,0,0,0,0,0,1], [1,1,0,0,0,0,0,0,1],
        [0,0,0,0,0,1,1,1,1], [0,0,0,1,1,1,1,0,0], [0,0,1,1,1,1,0,0,0],
        [0,1,1,1,1,0,0,0,0], [1,1,1,1,0,0,0,0,0], [1,1,1,0,1,0,0,0,0],
        [1,1,0,1,1,0,0,0,0], [1,0,1,1,1,0,0,0,0], [0,1,1,1,1,0,0,0,0],
        [1,1,1,0,0,1,0,0,0], [1,1,0,0,1,1,0,0,0], [1,0,0,1,1,1,0,0,0],
        [0,0,1,1,1,1,0,0,0], [1,1,1,1,1,0,0,0,0], [1,1,1,0,0,0,1,0,0],
    ]

    mu = (M - m + 1) / (2 ** m)
    sigma2 = M * (1.0 / (2 ** m) - (2 * m - 1) / (2 ** (2 * m)))
    if sigma2 <= 0:
        raise ValueError("Variance is non-positive")

    p_values = []
    for template in templates:
        W = []
        for block_idx in range(N):
            block_start = block_idx * M
            block = bits[block_start:block_start + M]
            count = 0
            i = 0
            while i <= len(block) - m:
                if list(block[i:i + m]) == template:
                    count += 1
                    i += m
                else:
                    i += 1
            W.append(count)
        chi_squared = sum((wj - mu) ** 2 for wj in W) / sigma2
        p = 1.0 - chi2.cdf(chi_squared, N - 1)
        p_values.append(p)
    return sum(p_values) / len(p_values)


# ═══════════════════════════════════════════════════════════════
#  Main — multi-sequence NIST testing
# ═══════════════════════════════════════════════════════════════

def run_on_sequences(fn, sequences, args=()):
    """Run fn(*args, bits) on each sequence. Returns list of p-values (-1 on error)."""
    p_values = []
    for seq in sequences:
        try:
            p = fn(*args, seq)
            if isinstance(p, (tuple, list)):
                p = p[0]
            p = float(p)
        except Exception:
            p = -1.0
        p_values.append(p)
    return p_values


def main():
    test_file = sys.argv[1] if len(sys.argv) > 1 else "test_data.bin"

    if not os.path.exists(test_file):
        print(f"ERROR: File not found: {test_file}")
        sys.exit(2)

    with open(test_file, "rb") as f:
        data = f.read()

    print(f"Read {len(data)} bytes from {test_file}")

    # Convert to bit sequence (MSB first)
    all_bits = bytes([(byte >> (7 - bit_pos)) & 1
                      for byte in data for bit_pos in range(8)])

    # ── Split into N sequences ──
    N_SEQ = 10
    SEQ_LEN = 1000000  # 1,000,000 bits per sequence
    ALPHA = 0.01

    available = min(len(all_bits), N_SEQ * SEQ_LEN)
    n_avail = available // SEQ_LEN
    if n_avail < N_SEQ:
        print(f"WARNING: Only {n_avail} full sequences available (need {N_SEQ})")
        N_SEQ = n_avail

    if N_SEQ < 3:
        print("ERROR: Not enough data for multi-sequence testing")
        sys.exit(2)

    sequences = [all_bits[i * SEQ_LEN:(i + 1) * SEQ_LEN] for i in range(N_SEQ)]
    print(f"Using {N_SEQ} sequences x {SEQ_LEN:,} bits = {N_SEQ * SEQ_LEN:,} bits")

    # Proportion threshold (NIST SP 800-22 Section 4.2.1)
    threshold = 1.0 - ALPHA - 3.0 * math.sqrt(ALPHA * (1.0 - ALPHA) / N_SEQ)
    min_passes = int(threshold * N_SEQ) + 1
    print(f"Proportion threshold: {min_passes}/{N_SEQ} must pass each test")
    print(f"(threshold = {threshold:.4f})\n")

    # ── All tests: (name, function, extra_args) ──
    # Convention: fn(*args, bits) — bits is always the last argument
    all_tests = [
        # 1-arg tests (just bits)
        ("Frequency",              sp80022suite.frequency,                  ()),
        ("Runs",                   sp80022suite.runs,                       ()),
        ("Longest Run of Ones",     sp80022suite.longest_run_of_ones,        ()),
        ("Rank",                   sp80022suite.rank,                        ()),
        ("Universal",              sp80022suite.universal,                   ()),
        ("Cumulative Sums",        sp80022suite.cumulative_sums,            ()),
        ("Discrete Fourier",       sp80022suite.discrete_fourier_transform, ()),
        # 2-arg tests (param, bits)
        ("Block Frequency",         sp80022suite.block_frequency,            (128,)),
        ("Overlapping Template",    sp80022suite.overlapping_template_matchings, (9,)),
        ("Linear Complexity",      sp80022suite.linear_complexity,          (500,)),
        ("Serial",                 sp80022suite.serial,                     (16,)),
        ("Approximate Entropy",    sp80022suite.approximate_entropy,        (10,)),
        # Manual tests
        ("Non-Overlapping Template", non_overlapping_template_test,           (9,)),
        ("Random Excursions",       random_excursions_test,                  ()),
        ("Random Excursions Variant", random_excursions_variant_test,         ()),
    ]

    results = []

    for name, fn, args in all_tests:
        p_vals = run_on_sequences(fn, sequences, args)

        # Count passes (p >= alpha, excluding errors)
        passes = sum(1 for p in p_vals if p >= ALPHA)
        errors = sum(1 for p in p_vals if p < 0)
        status = "PASS" if passes >= min_passes else "FAIL"

        # Format p-values
        p_strs = []
        for p in p_vals:
            if p < 0:
                p_strs.append(" ERR ")
            else:
                p_strs.append(f"{p:.4f}")

        err_note = f" ({errors} errors)" if errors > 0 else ""
        print(f"  {name:30s}  {passes}/{N_SEQ}  {status}  [{' '.join(p_strs)}]{err_note}")
        results.append((name, passes, N_SEQ, status))

    # ── Summary ──
    print()
    passed = [r for r in results if r[3] == "PASS"]
    failed = [r for r in results if r[3] == "FAIL"]

    print(f"Passed: {len(passed)}, Failed: {len(failed)}")

    if failed:
        for name, passes, total, _ in failed:
            print(f"  FAILED: {name} ({passes}/{total} sequences passed)")
    else:
        print(f"  All {len(passed)} tests passed proportion test")

    if len(failed) == 0:
        print("\nResult: PASSED")
    else:
        print(f"\nResult: FAILED ({len(failed)} tests failed)")
        sys.exit(1)


if __name__ == "__main__":
    main()
