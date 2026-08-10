#!/usr/bin/env python3
"""Run the fuzz campaign across many seeds in parallel.

Usage:
    python3 tests/run_campaign.py [--iters N] [--workers N] [--seeds A..B]
"""
import os
import subprocess
import sys

def main():
    iters = 5000
    workers = os.cpu_count() or 4
    seeds = range(1, 25)
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--iters" or a.startswith("--iters="):
            iters = int(a.split("=", 1)[1] if "=" in a else sys.argv[i + 1])
            i += 2 if "=" not in a else 1
        elif a == "--workers" or a.startswith("--workers="):
            workers = int(a.split("=", 1)[1] if "=" in a else sys.argv[i + 1])
            i += 2 if "=" not in a else 1
        elif a == "--seeds":
            lo, hi = sys.argv[i + 1].split("..")
            seeds = range(int(lo), int(hi) + 1)
            i += 2
        else:
            i += 1

    procs = {}
    for s in seeds:
        cmd = [sys.executable, "tests/fuzz.py",
               "--iters", str(iters), "--seed", str(s)]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
        procs[p] = s

    n_fail = 0
    for p in list(procs):
        out, _ = p.communicate()
        text = out.decode(errors="replace").strip().splitlines()
        last = text[-1] if text else "(no output)"
        print(f"[seed={procs[p]}] {last}")
        if p.returncode != 0:
            n_fail += 1
    print(f"campaign done: {n_fail} seed(s) with failures")
    sys.exit(1 if n_fail else 0)

if __name__ == "__main__":
    main()
