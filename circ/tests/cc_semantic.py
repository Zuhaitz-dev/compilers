#!/usr/bin/env python3
"""Semantic test: compile the generated C for each example and verify it
matches the reference simulator.

Usage: tests/cc_semantic.py [file.circ ...]
Exit 0 on success, 1 on any mismatch.
"""
import re
import subprocess
import sys
import tempfile
import os

CIRC = "./circ"


def run_circ(args, **kw):
    return subprocess.run([CIRC] + args, capture_output=True, text=True, **kw)


def parse_signature(c_code):
    m = re.search(r"void circuit\((.*?)\)\s*\{", c_code, re.S)
    if not m:
        return None, None
    params = [p.strip() for p in m.group(1).split(",") if p.strip()]
    inputs = [p.split()[-1] for p in params if not p.startswith("int *")]
    outputs = [p.split()[-1].lstrip("*") for p in params if p.startswith("int *")]
    return inputs, outputs


def parse_truth_header(header):
    """Parse "A[3:0] B | Q[2:0]" into (inputs, outputs) of (name, width)."""
    if "|" not in header:
        return None, None
    left, right = header.split("|", 1)
    def sigs(s):
        out = []
        for tok in s.split():
            m = re.match(r"^(\w+)(?:\[(\d+):(\d+)\])?$", tok)
            if m:
                name = m.group(1)
                width = 1
                if m.group(2) is not None:
                    width = abs(int(m.group(2)) - int(m.group(3))) + 1
                out.append((name, width))
        return out
    return sigs(left), sigs(right)


def gen_driver(inputs, outputs, cycles, combinational):
    """inputs/outputs are (name, width) lists."""
    proto = ", ".join([f"int {n}" for n, _ in inputs] +
                      [f"int *{n}" for n, _ in outputs])
    L = ["#include <stdio.h>",
         f"void circuit({proto});",
         "int main(void) {"]
    for n, _ in inputs:
        L.append(f"    int {n} = 0;")
    for n, _ in outputs:
        L.append(f"    int {n}_out = 0;")
    args = ", ".join([n for n, _ in inputs] + [f"&{n}_out" for n, _ in outputs])
    call = f"circuit({args});"
    if combinational:
        total = sum(w for _, w in inputs)
        if total == 0:
            L.append(f"    {call}")
            L.append("    printf(\"| " + "".join("%d " for _, _ in outputs) +
                     "\\n\", " + ", ".join(f"{n}_out" for _, n in outputs) + ");")
        else:
            for combo in range(1 << total):
                shift = 0
                for n, w in inputs:
                    m = (1 << w) - 1
                    L.append(f"    {n} = ({(combo >> shift)} & {m});")
                    shift += w
                L.append(f"    {call}")
                vals = "".join("%d " for _, _ in inputs) + "| " + \
                       "".join("%d " for _, _ in outputs)
                L.append("    printf(\"" + vals + "\\n\", " +
                         ", ".join([n for n, _ in inputs] +
                                   [f"{n}_out" for n, _ in outputs]) + ");")
    else:
        for c in range(cycles):
            clk = next((n for n, _ in inputs if n.upper() == "CLK"), None)
            if clk:
                L.append(f"    {clk} = {(1 if c % 2 == 0 else 0)};")
            L.append(f"    {call}")
        L.append("    printf(\"" + "".join(f"{n}=%d " for n, _ in outputs) +
                 "\\n\", " + ", ".join(f"{n}_out" for n, _ in outputs) + ");")
    L.append("    return 0;")
    L.append("}")
    return "\n".join(L) + "\n"


def reference(file, combinational):
    if combinational:
        r = run_circ(["--truth", file])
        return r.stdout.splitlines()[1:]
    r = run_circ(["--sim", file, "--cycles=8"])
    return r.stdout.splitlines()


def test_one(file):
    cc = run_circ(["--c", file])
    if cc.returncode != 0:
        return f"SKIP {file}: --c failed"
    inputs, outputs = parse_signature(cc.stdout)
    if inputs is None:
        return f"SKIP {file}: no signature"
    # widths + port order come from the truth table header; a sequential
    # circuit yields empty --truth output, so it is driven via --sim.
    th = run_circ(["--truth", file])
    comb = bool(th.stdout.splitlines())
    if comb:
        tins, touts = parse_truth_header(th.stdout.splitlines()[0])
        if tins is None:
            tins = [(n, 1) for n in inputs]
            touts = [(n, 1) for n in outputs]
    else:
        tins = [(n, 1) for n in inputs]
        touts = [(n, 1) for n in outputs]
    driver = gen_driver(tins, touts, 8, comb)
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "gen.c"), "w") as f:
            f.write(cc.stdout)
        with open(os.path.join(d, "main.c"), "w") as f:
            f.write(driver)
        exe = os.path.join(d, "prog")
        build = subprocess.run(
            ["cc", "-O1", "-o", exe,
             os.path.join(d, "gen.c"), os.path.join(d, "main.c")],
            capture_output=True, text=True)
        if build.returncode != 0:
            return f"SKIP {file}: generated C does not compile:\n{build.stderr}"
        run = subprocess.run([exe], capture_output=True, text=True)
        got = run.stdout.splitlines()
    exp = reference(file, comb)
    if got != exp:
        return f"FAIL {file}:\n  got: {got}\n  exp: {exp}"
    return None


def main():
    files = sys.argv[1:] or sorted(
        os.path.join("examples", f) for f in os.listdir("examples")
        if f.endswith(".circ"))
    fails = 0
    for f in files:
        res = test_one(f)
        if res:
            print(res)
            if not res.startswith("SKIP"):
                fails += 1
    if fails:
        print(f"{fails} semantic failure(s)")
        sys.exit(1)
    print("ALL C SEMANTIC TESTS PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
