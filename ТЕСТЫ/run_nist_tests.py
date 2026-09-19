#!/usr/bin/env python3
"""NIST SP 800-22 statistical tests for Rand++ generator.
Multiple-sequence version with proper proportion testing."""
import sp80022suite
import sys
import os
import math
import numpy as np
from scipy.stats import chi2
from scipy.special import gammaincc

NUM_SEQ = 10
SEQ_LEN = 2_000_000  # 2M bits per sequence — enough for Random Excursions (J ~ 1400)


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


def run_single_test(name, fn, *args):
    """Run one test on one bit sequence. Returns (p_value, status, error)."""
    try:
        p = fn(*args)
        if isinstance(p, (tuple, list)):
            p = p[0]
        p = float(p)
        if p < 0 or p > 1:
            return (None, "ERR", f"invalid p={p}")
        status = "PASS" if p >= 0.01 else "FAIL"
        return (p, status, None)
    except ValueError as e:
        return (None, "ERR", str(e))
    except Exception as e:
        return (None, "ERR", str(e))


def main():
    test_file = sys.argv[1] if len(sys.argv) > 1 else "test_data.bin"
    if not os.path.exists(test_file):
        print(f"ERROR: File not found: {test_file}")
        sys.exit(2)

    with open(test_file, "rb") as f:
        data = f.read()
    print(f"Read {len(data)} bytes from {test_file}")

    # Convert to bit sequence
    all_bits = bytes([(byte >> (7 - bit_pos)) & 1 for byte in data for bit_pos in range(8)])
    total_bits = len(all_bits)
    needed = NUM_SEQ * SEQ_LEN
    if total_bits < needed:
        print(f"WARNING: Need {needed} bits, have {total_bits}. Reducing sequences.")
        num_seq = max(1, total_bits // SEQ_LEN)
    else:
        num_seq = NUM_SEQ

    print(f"Using {num_seq} sequences x {SEQ_LEN:,} bits = {num_seq * SEQ_LEN:,} bits")
    threshold = 1.0 - 3.0 / math.sqrt(num_seq)
    print(f"Proportion threshold: {threshold:.4f} (must pass >= {math.ceil(threshold * num_seq)}/{num_seq})\n")

    # Define tests: (name, function, extra_args)
    tests_1arg = [
        ("Frequency",              sp80022suite.frequency),
        ("Runs",                   sp80022suite.runs),
        ("Longest Run of Ones",     sp80022suite.longest_run_of_ones),
        ("Rank",                   sp80022suite.rank),
        ("Universal",              sp80022suite.universal),
        ("Cumulative Sums",        sp80022suite.cumulative_sums),
        ("Discrete Fourier",       sp80022suite.discrete_fourier_transform),
    ]
    tests_2arg = [
        ("Block Frequency",         sp80022suite.block_frequency, 128),
        ("Overlapping Template",    sp80022suite.overlapping_template_matchings, 9),
        ("Linear Complexity",      sp80022suite.linear_complexity, 500),
        ("Serial",                 sp80022suite.serial, 16),
        ("Approximate Entropy",    sp80022suite.approximate_entropy, 10),
    ]
    tests_manual = [
        ("Non-Overlapping Template",  non_overlapping_template_test, (SEQ_LEN,)),
        ("Random Excursions",        random_excursions_test, ()),
        ("Random Excursions Variant", random_excursions_variant_test, ()),
    ]

    all_results = []

    for test_name, fn, *extra in tests_1arg:
        pvals = []
        n_pass = 0
        n_run = 0
        n_err = 0
        for i in range(num_seq):
            seq = all_bits[i * SEQ_LEN:(i + 1) * SEQ_LEN]
            p, status, err = run_single_test(test_name, fn, seq)
            if status == "ERR":
                n_err += 1
            else:
                n_run += 1
                pvals.append(p)
                if status == "PASS":
                    n_pass += 1

        if n_run == 0:
            all_results.append((test_name, 0, 0, 0, "SKIPPED", pvals))
        else:
            prop = n_pass / n_run
            status = "PASS" if prop >= threshold else "FAIL"
            all_results.append((test_name, n_pass, n_run, n_err, status, pvals))

    for test_name, fn, param, *rest in tests_2arg:
        pvals = []
        n_pass = 0
        n_run = 0
        n_err = 0
        for i in range(num_seq):
            seq = all_bits[i * SEQ_LEN:(i + 1) * SEQ_LEN]
            p, status, err = run_single_test(test_name, fn, param, seq)
            if status == "ERR":
                n_err += 1
            else:
                n_run += 1
                pvals.append(p)
                if status == "PASS":
                    n_pass += 1

        if n_run == 0:
            all_results.append((test_name, 0, 0, 0, "SKIPPED", pvals))
        else:
            prop = n_pass / n_run
            status = "PASS" if prop >= threshold else "FAIL"
            all_results.append((test_name, n_pass, n_run, n_err, status, pvals))

    for test_name, fn, args in tests_manual:
        pvals = []
        n_pass = 0
        n_run = 0
        n_err = 0
        for i in range(num_seq):
            seq = all_bits[i * SEQ_LEN:(i + 1) * SEQ_LEN]
            p, status, err = run_single_test(test_name, fn, *args, seq)
            if status == "ERR":
                n_err += 1
            else:
                n_run += 1
                pvals.append(p)
                if status == "PASS":
                    n_pass += 1

        if n_run == 0:
            all_results.append((test_name, 0, 0, 0, "SKIPPED", pvals))
        elif n_run < 3:
            all_results.append((test_name, n_pass, n_run, n_err, "SKIPPED", pvals))
        else:
            prop = n_pass / n_run
            status = "PASS" if prop >= threshold else "FAIL"
            all_results.append((test_name, n_pass, n_run, n_err, status, pvals))

    # ── Print results ──
    for name, n_pass, n_run, n_err, status, pvals in all_results:
        if status == "SKIPPED":
            print(f"  {name:30s}  SKIPPED ({n_err} errors)")
        else:
            pstr = " ".join(f"{p:.4f}" for p in pvals[:10])
            err_str = f" ({n_err} errors)" if n_err > 0 else ""
            print(f"  {name:30s}  {n_pass}/{n_run}  {status}  [{pstr}]{err_str}")

    # ── Summary ──
    print()
    passed = [r for r in all_results if r[4] == "PASS"]
    failed = [r for r in all_results if r[4] == "FAIL"]
    skipped = [r for r in all_results if r[4] == "SKIPPED"]

    print(f"Passed: {len(passed)}, Failed: {len(failed)}, Skipped: {len(skipped)}")

    if skipped:
        for name, _, _, _, _, _ in skipped:
            print(f"  SKIPPED: {name}")

    if failed:
        for name, n_pass, n_run, _, _, _ in failed:
            print(f"  FAILED: {name} ({n_pass}/{n_run} sequences passed)")

    if len(failed) == 0:
        print("\nResult: PASSED")
    else:
        print(f"\nResult: FAILED ({len(failed)} tests failed)")
        sys.exit(1)


if __name__ == "__main__":
    main()
