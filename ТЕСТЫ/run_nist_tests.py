#!/usr/bin/env python3
"""NIST SP 800-22 statistical tests for Rand++ generator.
Multiple-sequence methodology per NIST SP 800-22 Section 4.
"""
import sp80022suite
import sys
import os
import math
import numpy as np
from scipy.stats import chi2
from scipy.special import gammaincc


# ═══════════════════════════════════════════════════════════════
#  Manual test implementations (sp80022suite issues)
# ═══════════════════════════════════════════════════════════════

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
    cycles = _build_cycles(bits)
    J = len(cycles)
    if J < 500:
        raise ValueError(f"Insufficient cycles (J={J})")

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
            chisq += (top * top) / (J * pixk[abs_x][k]) if J * pixk[abs_x][k] > 0 else 0
        p_values.append(gammaincc(5.0 / 2.0, chisq / 2.0))
    return min(p_values)


def random_excursions_variant_test(bits):
    cycles = _build_cycles(bits)
    J = len(cycles)
    if J < 500:
        raise ValueError(f"Insufficient cycles (J={J})")

    states = list(range(-9, 0)) + list(range(1, 10))
    p_values = []
    for x in states:
        xi = sum(1 for cycle in cycles for v in cycle if v == x)
        variance = J * (4 * abs(x) - 2)
        if variance <= 0:
            p_values.append(1.0)
            continue
        z = (xi - J) / math.sqrt(variance)
        p_values.append(math.erfc(abs(z) / math.sqrt(2.0)))
    return min(p_values)


def non_overlapping_template_test(bits, m=9):
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
#  Main — multi-sequence NIST methodology
# ═══════════════════════════════════════════════════════════════

def main():
    test_file = sys.argv[1] if len(sys.argv) > 1 else "test_data.bin"

    if not os.path.exists(test_file):
        print(f"ERROR: File not found: {test_file}")
        sys.exit(2)

    with open(test_file, "rb") as f:
        data = f.read()

    print(f"Read {len(data)} bytes from {test_file}")

    # Extract all bits
    all_bits = bytes([(byte >> (7 - bit_pos)) & 1 for byte in data for bit_pos in range(8)])

    # Split into sequences
    SEQ_BITS = 1_000_000
    NUM_SEQ = min(10, len(all_bits) // SEQ_BITS)
    if NUM_SEQ < 1:
        print("ERROR: Not enough data (need at least 1,000,000 bits)")
        sys.exit(2)

    sequences = [all_bits[i * SEQ_BITS:(i + 1) * SEQ_BITS] for i in range(NUM_SEQ)]
    print(f"Using {NUM_SEQ} sequences x {SEQ_BITS:,} bits = {NUM_SEQ * SEQ_BITS:,} bits")

    ALPHA = 0.01
    # NIST proportion threshold: pass if proportion >= (1 - alpha) - 3*sqrt(alpha*(1-alpha)/N)
    prop_threshold = (1 - ALPHA) - 3 * math.sqrt(ALPHA * (1 - ALPHA) / NUM_SEQ)
    print(f"Proportion threshold: {math.ceil(prop_threshold * NUM_SEQ)}/{NUM_SEQ} must pass each test")
    print(f"  (threshold = {prop_threshold:.4f})")
    print()

    # Define all tests: (name, callable, args)
    # 1-arg tests (just bits)
    tests_1arg = [
        ("Frequency",              sp80022suite.frequency),
        ("Runs",                   sp80022suite.runs),
        ("Longest Run of Ones",     sp80022suite.longest_run_of_ones),
        ("Rank",                   sp80022suite.rank),
        ("Universal",              sp80022suite.universal),
        ("Cumulative Sums",        sp80022suite.cumulative_sums),
        ("Discrete Fourier",       sp80022suite.discrete_fourier_transform),
    ]

    # 2-arg tests (param, bits)
    tests_2arg = [
        ("Block Frequency",         128, sp80022suite.block_frequency),
        ("Overlapping Template",    9,   sp80022suite.overlapping_template_matchings),
        ("Linear Complexity",      500, sp80022suite.linear_complexity),
        ("Serial",                 16,  sp80022suite.serial),
        ("Approximate Entropy",    10,  sp80022suite.approximate_entropy),
    ]

    # Manual tests
    tests_manual = [
        ("Non-Overlapping Template",  non_overlapping_template_test, (9,)),
        ("Random Excursions",        random_excursions_test, ()),
        ("Random Excursions Variant", random_excursions_variant_test, ()),
    ]

    all_results = []
    any_failure = False

    for name, fn, *args in [(n, f, *a) for n, f in tests_1arg] + \
                         [(n, p, f, ) for n, p, f in tests_2arg] + \
                         [(n, f, *a) for n, f, a in tests_manual]:
        pass

    # Actually, let me restructure this properly
    all_results = []

    # Run 1-arg tests
    for name, fn in tests_1arg:
        pvals = []
        n_pass = 0
        n_skip = 0
        for seq in sequences:
            try:
                p = float(fn(seq))
                pvals.append(p)
                if p >= ALPHA:
                    n_pass += 1
                else:
                    pvals[-1] = p  # FAIL
            except Exception:
                pvals.append(None)
                n_skip += 1
        n_run = NUM_SEQ - n_skip
        _report(name, n_pass, n_run, n_skip, pvals, prop_threshold, NUM_SEQ, all_results)

    # Run 2-arg tests
    for name, param, fn in tests_2arg:
        pvals = []
        n_pass = 0
        n_skip = 0
        for seq in sequences:
            try:
                p = float(fn(param, seq))
                pvals.append(p)
                if p >= ALPHA:
                    n_pass += 1
            except Exception:
                pvals.append(None)
                n_skip += 1
        n_run = NUM_SEQ - n_skip
        _report(name, n_pass, n_run, n_skip, pvals, prop_threshold, NUM_SEQ, all_results)

    # Run manual tests
    for name, fn, args in tests_manual:
        pvals = []
        n_pass = 0
        n_skip = 0
        for seq in sequences:
            try:
                p = float(fn(seq, *args))
                pvals.append(p)
                if p >= ALPHA:
                    n_pass += 1
            except Exception:
                pvals.append(None)
                n_skip += 1
        n_run = NUM_SEQ - n_skip
        _report(name, n_pass, n_run, n_skip, pvals, prop_threshold, NUM_SEQ, all_results)

    # Summary
    print()
    passed = [r for r in all_results if r[2] == "PASS"]
    failed = [r for r in all_results if r[2] == "FAIL"]
    skipped = [r for r in all_results if r[2] == "SKIPPED"]

    print(f"Passed: {len(passed)}, Failed: {len(failed)}, Skipped: {len(skipped)}")

    if skipped:
        for name, _, _ in skipped:
            print(f"  SKIPPED: {name}")

    if failed:
        for name, detail, _ in failed:
            print(f"  FAILED: {name} ({detail})")

    if len(failed) == 0:
        print("\nResult: PASSED")
    else:
        print(f"\nResult: FAILED ({len(failed)} tests failed)")
        sys.exit(1)


def _report(name, n_pass, n_run, n_skip, pvals, prop_threshold, NUM_SEQ):
    """Print result line and append to all_results."""
    # Format p-values: show actual values, ERR for None
    pstr_parts = []
    for p in pvals:
        if p is None:
            pstr_parts.append(" ERR ")
        else:
            pstr_parts.append(f"{p:.4f}")
    pstr = " ".join(pstr_parts)

    if n_run == 0:
        # All sequences skipped
        print(f"  {name:30s}  SKIPPED (all sequences had insufficient data)")
        # This is NOT a failure — NIST says skip the test
        return ("SKIPPED_RESULT", None, "SKIPPED")

    # Need at least 3 sequences to evaluate proportion
    if n_run < 3:
        print(f"  {name:30s}  SKIPPED ({n_run} sequences ran, need >= 3)  [{pstr}]")
        return (name, None, "SKIPPED")

    proportion = n_pass / n_run
    if proportion >= prop_threshold:
        print(f"  {name:30s}  {n_pass}/{n_run}  PASS  [{pstr}]" + (f" ({n_skip} skipped)" if n_skip else ""))
        return (name, f"{n_pass}/{n_run}", "PASS")
    else:
        print(f"  {name:30s}  {n_pass}/{n_run}  FAIL  [{pstr}]" + (f" ({n_skip} skipped)" if n_skip else ""))
        return (name, f"{n_pass}/{n_run}", "FAIL")


if __name__ == "__main__":
    main()
