#!/usr/bin/env python3
"""
Run each scenario N times and print a copy-paste-ready markdown table of
median latency and run-to-run variation.

Usage:
    python3 bench_latency.py ./build/market_maker sim/fixtures/*.scn
    python3 bench_latency.py ./build/market_maker sim/fixtures/*.scn -n 50
"""
import argparse
import random
import re
import statistics as st
import subprocess
import sys

MSG_RE = re.compile(r"messages:\s*(\d+)")
LAT_RE = re.compile(r"average latency:\s*([0-9.]+)\s*ns")


def run_once(binary, scenario):
    p = subprocess.run([binary, scenario], capture_output=True, text=True, timeout=60)
    m, l = MSG_RE.search(p.stdout), LAT_RE.search(p.stdout)
    if p.returncode != 0 or not m or not l:
        sys.exit(f"error running {binary} {scenario}:\n{p.stdout}{p.stderr}")
    return int(m.group(1)), float(l.group(1))


def ci_half_width_pct(vals, iters=5000):
    """Half-width of the 95% bootstrap CI of the median, as % of the median."""
    rng, n = random.Random(0), len(vals)
    boots = sorted(st.median([vals[rng.randrange(n)] for _ in range(n)]) for _ in range(iters))
    lo, hi = boots[int(0.025 * iters)], boots[int(0.975 * iters)]
    return 100 * (hi - lo) / 2 / st.median(vals)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("scenarios", nargs="+")
    ap.add_argument("-n", "--runs", type=int, default=30, help="measured runs per scenario (default 30)")
    ap.add_argument("-w", "--warmup", type=int, default=3, help="discarded warm-up runs (default 3)")
    args = ap.parse_args()

    rows = []
    for scn in args.scenarios:
        for _ in range(args.warmup):
            run_once(args.binary, scn)
        msgs, lats = None, []
        for _ in range(args.runs):
            m, lat = run_once(args.binary, scn)
            if msgs not in (None, m):
                print(f"warning: message count changed for {scn}", file=sys.stderr)
            msgs = m
            lats.append(lat)
        rows.append((scn.split("/")[-1], msgs, st.median(lats), ci_half_width_pct(lats)))
        print(f"done: {scn}", file=sys.stderr)

    print("\n| Scenario | Messages | Median latency (ns) | Run-to-run variation |")
    print("|---|---|---|---|")
    for name, msgs, med, var in rows:
        print(f"| {name} | {msgs} | {med:,.0f} | ±{var:.1f}% |")
    print(f"\n*{args.runs} runs per scenario, median reported. Variation is the 95% "
          f"confidence interval half-width on the median.*")


if __name__ == "__main__":
    main()