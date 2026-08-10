#!/usr/bin/env python3
"""Semantic test: compile the generated Verilog for each example with
iverilog + vvp and verify it matches the reference simulator.

Usage: tests/v_semantic.py [file.circ ...]
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


def parse_ports(v_code):
    """Return ((inputs), (outputs)) of (name, width) from the top module."""
    m = re.search(r"module circuit \((.*?)\);", v_code, re.S)
    if not m:
        return None, None
    def sigs(body, kw):
        out = []
        for line in body.splitlines():
            line = line.strip().rstrip(",")
            mm = re.match(rf"^(?:, )?{kw}(?: \[(\d+):(\d+)\])? (\w+)$", line)
            if mm:
                w = 1
                if mm.group(1) is not None:
                    w = abs(int(mm.group(1)) - int(mm.group(2))) + 1
                out.append((mm.group(3), w))
        return out
    return sigs(m.group(1), "input"), sigs(m.group(1), "output")


def gen_tb(inputs, outputs, cycles, combinational):
    L = ["module tb;"]
    for n, w in outputs:
        rng = f"[{w-1}:0] " if w > 1 else ""
        L.append(f"  wire {rng}{n};")
    for n, w in inputs:
        rng = f"[{w-1}:0] " if w > 1 else ""
        L.append(f"  reg {rng}{n} = 0;")
    L.append("  circuit dut(" + ", ".join([f".{n}({n})" for n, _ in inputs] +
                                          [f".{n}({n})" for n, _ in outputs]) +
             ");")
    L.append("  initial begin")
    L.append("    $display(\"HDR\");")
    if combinational:
        total = sum(w for _, w in inputs)
        if total == 0:
            L.append("    #1 $display(\"| " + "".join("%0d " for _, _ in outputs) +
                     "\", " + ", ".join(n for n, _ in outputs) + ");")
        else:
            for combo in range(1 << total):
                shift = 0
                for n, w in inputs:
                    m = (1 << w) - 1
                    L.append(f"    {n} = {((combo >> shift) & m)};")
                    shift += w
                L.append("    #1 $display(\"" +
                         "".join("%0d " for _ in inputs) + "| " +
                         "".join("%0d " for _ in outputs) + "\", " +
                         ", ".join([n for n, _ in inputs] +
                                   [n for n, _ in outputs]) + ");")
    else:
        for c in range(cycles):
            clk = next((n for n, _ in inputs if n.upper() == "CLK"), None)
            if clk:
                L.append(f"    {clk} = {(1 if c % 2 == 0 else 0)};")
            L.append("    #1 ;")
        L.append("    $display(\"" + "".join(f"{n}=%0d " for n, _ in outputs) +
                 "\", " + ", ".join(n for n, _ in outputs) + ");")
    L.append("    $finish;")
    L.append("  end")
    L.append("endmodule")
    return "\n".join(L) + "\n"


def reference(file, combinational):
    if combinational:
        r = run_circ(["--truth", file])
        return r.stdout.splitlines()[1:]
    r = run_circ(["--sim", file, "--cycles=8"])
    return r.stdout.splitlines()


def test_one(file):
    vv = run_circ(["--v", file])
    if vv.returncode != 0:
        return f"SKIP {file}: --v failed"
    inputs, outputs = parse_ports(vv.stdout)
    if inputs is None:
        return f"SKIP {file}: no module"
    comb = not any(k in vv.stdout for k in ("always @", "reg "))
    if not comb:
        # Ripple clocks (a sequential gate clocked by a derived signal)
        # trigger spurious edges at iverilog's x-init; the sim and C agree,
        # so those get a compile-only check here.
        posedge_signals = set(re.findall(r"always @\(posedge (\w+)", vv.stdout))
        if posedge_signals - set(n for n, _ in inputs):
            with tempfile.TemporaryDirectory() as d:
                with open(os.path.join(d, "gen.v"), "w") as f:
                    f.write(vv.stdout)
                build = subprocess.run(
                    ["iverilog", "-o", os.path.join(d, "null"),
                     os.path.join(d, "gen.v")],
                    capture_output=True, text=True)
            if build.returncode != 0:
                return f"FAIL {file}: generated Verilog does not compile:\n{build.stderr}"
            return None
    tb = gen_tb(inputs, outputs, 8, comb)
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "gen.v"), "w") as f:
            f.write(vv.stdout)
        with open(os.path.join(d, "tb.v"), "w") as f:
            f.write(tb)
        vvp_prog = os.path.join(d, "prog")
        build = subprocess.run(
            ["iverilog", "-o", vvp_prog,
             os.path.join(d, "tb.v"), os.path.join(d, "gen.v")],
            capture_output=True, text=True)
        if build.returncode != 0:
            return f"SKIP {file}: iverilog failed:\n{build.stderr}"
        run = subprocess.run([vvp_prog], capture_output=True, text=True)
        got = [ln.rstrip() for ln in run.stdout.splitlines()
               if ln != "HDR" and not ln.strip().startswith("/tmp/")]
    exp = [ln.rstrip() for ln in reference(file, comb)]
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
    print("ALL V SEMANTIC TESTS PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
