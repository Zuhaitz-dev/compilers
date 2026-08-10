#!/usr/bin/env python3
"""Random fuzzer for the `circ` tool.

Generates random circuits two ways:
  * structured: a coherent DAG of gates over scalar/bus signals with
    scalar-broadcast and junction fan-in, rendered as valid layouts;
  * mutation: random edits to the example circuits (stray chars, stacked
    labels, mangled markers, truncation, ...).

For every circuit it checks that:
  1. --check never crashes or hangs (rejection is a valid outcome),
  2. an ACCEPTED circuit evaluates identically across --sim, the compiled
     --c --driver program, and iverilog (ripple-clock / DLATCH cases are
     excluded from the iverilog comparison, as in tests/v_semantic.py).

Usage: tests/fuzz.py [--iters N] [--seed S] [--only structured|mutate]
Any failure is saved to tests/fuzz/regressions/ and the script exits 1.
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

CIRC = "./circ"
REGRESS = os.path.join("tests", "fuzz", "regressions")
MAX_LINES = 80
MAX_CHARS = 4000
NO_V = False  # set by --no-v: skip the iverilog comparison
KEEP_GOING = False  # set by --keep-going: collect all failures

HAVE_GCC = shutil.which("gcc") is not None
HAVE_IV = shutil.which("iverilog") is not None and shutil.which("vvp") is not None


def run(args, timeout=10, **kw):
    return subprocess.run(args, capture_output=True, text=True,
                          timeout=timeout, **kw)


def save_failure(circ, tag):
    os.makedirs(REGRESS, exist_ok=True)
    ts = int(__import__("time").time() * 1000)
    path = os.path.join(REGRESS, f"{tag}_{ts}.circ")
    with open(path, "w") as f:
        f.write(circ)
    return path


# ---------------------------------------------------------------
# label / layout helpers
# ---------------------------------------------------------------

def label(name, w, kind):
    rng = f"[{w-1}:0]" if w > 1 else ""
    if kind == "input":
        return f"{name}{rng}>"
    if kind == "output":
        return f"{name}{rng}<"
    return f"{name}{rng}"


def not_block(l1, gate, out):
    return [f"{l1}---[{gate}]---{out}"]


def bin_block(l1, l2, gate, out):
    jc = max(len(l1), len(l2)) + 3
    return [
        l1 + "-" * (jc - len(l1)) + "+--" + gate + "--" + out,
        " " * jc + "|",
        l2 + "-" * (jc - len(l2)) + "+",
    ]


def seq_block(d, clk, gate, out):
    gx = len(d) + 3
    return [
        d + "---[" + gate + "]---" + out,
        " " * gx + "|",
        clk + "-" * (gx - len(clk)) + "+",
    ]


def seq3_block(j, k, clk, gate, out):
    """3-pin gate (JKFF): J on pin0, K on pin1, clock on pin2."""
    gx = len(j) + 3
    return [
        j + "---[" + gate + "]---" + out,
        k + "-" * (gx - len(k)) + "-",
        " " * (gx + 1) + "|",
        clk + "-" * (gx + 1 - len(clk)) + "+",
    ]


def const_block(gate, out):
    """GND/VCC: no inputs, one output label."""
    return [f"[{gate}]---{out}"]


def gen_structured(rng):
    pool = []
    used = set()

    def newname(prefix):
        i = 0
        while f"{prefix}{i}" in used:
            i += 1
        used.add(f"{prefix}{i}")
        return f"{prefix}{i}"

    rows = []
    for _ in range(rng.randint(1, 4)):
        pool.append((newname("I"), rng.choice([1, 1, 1, 1, 2, 4, 8, 16]),
                     "input"))

    for _ in range(rng.randint(1, 8)):
        gtype = rng.choice(["NOT", "AND", "OR", "XOR", "NAND", "NOR", "XNOR",
                            "DFF", "DLATCH", "JKFF", "GND", "VCC"])
        out_name = newname("T")
        out_kind = rng.choice(["internal", "internal", "internal", "output"])

        if gtype == "NOT":
            s = rng.choice(pool)
            w = s[1]
            block = not_block(label(*s), gtype, label(out_name, w, out_kind))
            pool.append((out_name, w, out_kind))
        elif gtype in ("DFF", "DLATCH"):
            s = rng.choice(pool)
            w = s[1]
            if not any(p[0] == "CLK" for p in pool):
                pool.append(("CLK", 1, "input"))
            block = seq_block(label(*s), label("CLK", 1, "input"), gtype,
                              label(out_name, w, out_kind))
            pool.append((out_name, w, out_kind))
        elif gtype == "JKFF":
            j = rng.choice(pool)
            k = rng.choice(pool)
            w = max(j[1], k[1]) if (j[1] == k[1] or j[1] == 1 or k[1] == 1) else 1
            if not any(p[0] == "CLK" for p in pool):
                pool.append(("CLK", 1, "input"))
            block = seq3_block(label(*j), label(*k), label("CLK", 1, "input"),
                               gtype, label(out_name, w, out_kind))
            pool.append((out_name, w, out_kind))
        elif gtype in ("GND", "VCC"):
            w = rng.choice([1, 1, 1, 4])
            block = const_block(gtype, label(out_name, w, out_kind))
            pool.append((out_name, w, out_kind))
        else:
            s1 = rng.choice(pool)
            s2 = rng.choice(pool)
            if not (s1[1] == s2[1] or s1[1] == 1 or s2[1] == 1):
                continue  # incompatible widths, skip this gate
            w = max(s1[1], s2[1])
            block = bin_block(label(*s1), label(*s2), gtype,
                              label(out_name, w, out_kind))
            pool.append((out_name, w, out_kind))

        if rows:
            rows.append("")
        rows += block

    return "\n".join(rows) + "\n"


def load_examples():
    base = []
    if os.path.isdir("examples"):
        for f in sorted(os.listdir("examples")):
            if f.endswith(".circ"):
                base.append(open(os.path.join("examples", f)).read())
    return base


def gen_mutate(rng, examples):
    circ = rng.choice(examples)
    lines = circ.rstrip("\n").split("\n")
    chars = list(" -|+[]<>=/:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_")
    for _ in range(rng.randint(1, 8)):
        op = rng.random()
        if op < 0.30 and lines:
            r = rng.randrange(len(lines))
            c = rng.randrange(len(lines[r]) + 1)
            lines[r] = lines[r][:c] + rng.choice(chars) + lines[r][c:]
        elif op < 0.50 and lines:
            r = rng.randrange(len(lines))
            if lines[r]:
                c = rng.randrange(len(lines[r]))
                lines[r] = lines[r][:c] + lines[r][c + 1:]
        elif op < 0.70 and lines:
            r = rng.randrange(len(lines))
            if lines[r]:
                c = rng.randrange(len(lines[r]))
                lines[r] = lines[r][:c] + rng.choice(chars) + lines[r][c + 1:]
        elif op < 0.80:
            lines.append("".join(rng.choice(chars)
                                 for _ in range(rng.randint(0, 30))))
        elif op < 0.90 and len(lines) > 1:
            lines.pop(rng.randrange(len(lines)))
        else:
            lines.insert(rng.randrange(len(lines)),
                         lines[rng.randrange(len(lines))])

    if len(lines) > MAX_LINES:
        lines = lines[:MAX_LINES]
    out = "\n".join(lines) + "\n"
    if len(out) > MAX_CHARS:
        return gen_mutate(rng, examples)
    return out


# ---------------------------------------------------------------
# equivalence checking
# ---------------------------------------------------------------

def parse_ports_v(v_code):
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


def exclude_iverilog(v_code, inputs):
    """Ripple clocks (derived posedge) and DLATCH trigger iverilog x-init
    quirks; exclude those from the Verilog comparison."""
    if "always @(*)" in v_code:
        return True
    pos = set(re.findall(r"always @\(posedge (\w+)", v_code))
    return bool(pos - set(n for n, _ in inputs))


def sim_vec(file, inputs, vec, cycles):
    args = [CIRC, "--sim", file]
    for n, _ in inputs:
        if n in vec and n.upper() != "CLK":
            args.append(f"{n}={vec[n]}")
    args.append(f"--cycles={cycles}")
    r = run(args)
    if r.returncode < 0:
        return ("crash", f"--sim segfault (signal {-r.returncode})")
    return ("ok", "\n".join(r.stdout.splitlines()))


def c_vec(file, inputs, vec, cycles, d):
    if not HAVE_GCC:
        return ("skip", None)
    gen = run([CIRC, "--c", "--driver", file, "-o", os.path.join(d, "g.c")])
    if gen.returncode < 0:
        return ("crash", "--c --driver segfault")
    if gen.returncode != 0:
        return ("crash", "--c --driver produced no output")
    exe = os.path.join(d, "g")
    cc = run(["gcc", "-O1", "-o", exe, os.path.join(d, "g.c")])
    if cc.returncode != 0:
        return ("crash", f"generated C does not compile:\n{cc.stderr}")
    args = [exe]
    for n, _ in inputs:
        if n in vec and n.upper() != "CLK":
            args.append(f"{n}={vec[n]}")
    args.append(f"--cycles={cycles}")
    r = run(args)
    if r.returncode < 0:
        return ("crash", f"generated C driver segfault")
    return ("ok", "\n".join(r.stdout.splitlines()))


def v_vec(file, inputs, outputs, vec, cycles, d):
    if not HAVE_IV:
        return ("skip", None)
    gv = os.path.join(d, "g.v")
    vv = run([CIRC, "--v", file, "-o", gv])
    if vv.returncode != 0:
        return ("crash", "--v failed on accepted circuit")
    vcode = open(gv).read()
    comb = "always @" not in vcode

    L = ["module tb;"]
    for n, w in outputs:
        rng = f"[{w-1}:0] " if w > 1 else ""
        L.append(f"  wire {rng}{n};")
    for n, w in inputs:
        rng = f"[{w-1}:0] " if w > 1 else ""
        L.append(f"  reg {rng}{n} = {vec.get(n, 0)};")
    L.append("  circuit dut(" + ", ".join(
        [f".{n}({n})" for n, _ in inputs] + [f".{n}({n})" for n, _ in outputs]
    ) + ");")
    L.append("  initial begin")
    L.append("    #1;")
    if comb:
        L.append("    #1 $display(\"" + "".join(f"{n}=%0d " for n, _ in outputs)
                 + "\", " + ", ".join(n for n, _ in outputs) + ");")
    else:
        clk = next((n for n, _ in inputs if n.upper() == "CLK"), None)
        for c in range(cycles):
            if clk:
                L.append(f"    {clk} = {1 if c % 2 == 0 else 0};")
            L.append("    #1;")
        L.append("    $display(\"" + "".join(f"{n}=%0d " for n, _ in outputs)
                 + "\", " + ", ".join(n for n, _ in outputs) + ");")
    L.append("    $finish;")
    L.append("  end")
    L.append("endmodule")
    tb = os.path.join(d, "tb.v")
    open(tb, "w").write("\n".join(L) + "\n")

    out = os.path.join(d, "p")
    iv = run(["iverilog", "-o", out, tb, os.path.join(d, "g.v")])
    if iv.returncode != 0:
        return ("crash", f"iverilog failed:\n{iv.stderr}")
    r = run([out])
    if r.returncode < 0:
        return ("crash", "vvp segfault")
    lines = [ln for ln in r.stdout.splitlines()
             if ln.strip() and not ln.strip().startswith("/tmp/")]
    return ("ok", "\n".join(lines))


def check_circuit(circ, rng, examples, tag):
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "fz.circ")
        open(f, "w").write(circ)
        try:
            chk = run([CIRC, "--check", f])
        except subprocess.TimeoutExpired:
            return f"{tag}: hang in --check\n{circ}"
        if chk.returncode < 0:
            return f"{tag}: crash in --check (signal {-chk.returncode})\n{circ}"
        if chk.returncode != 0:
            return None  # rejected cleanly — valid outcome

        vv = run([CIRC, "--v", f])
        if vv.returncode != 0 or "module circuit" not in vv.stdout:
            return f"{tag}: accepted but --v failed\n{circ}"
        inputs, outputs = parse_ports_v(vv.stdout)
        if inputs is None:
            return f"{tag}: accepted but no ports parsed\n{circ}"

        input_names = {n for n, _ in inputs}
        seq_clks = set(re.findall(r"always @\(posedge (\w+)", vv.stdout))
        has_latch = "always @(*)" in vv.stdout
        internal_clks = seq_clks - input_names
        nonclk_clks = seq_clks - {n for n, _ in inputs if n.upper() == "CLK"}
        skip_v = has_latch or bool(internal_clks)

        for _ in range(3):
            vec = {}
            for n, w in inputs:
                if n.upper() == "CLK":
                    continue
                if n in nonclk_clks:
                    vec[n] = 0  # constant clock: no edges, deterministic
                else:
                    vec[n] = rng.randint(0, (1 << w) - 1)
            cycles = rng.randint(1, 4)

            st, sr = sim_vec(f, inputs, vec, cycles)
            if st == "crash":
                return f"{tag}: {sr}\n{circ}"
            if internal_clks:
                continue  # can't control the clock: crash-check only

            ct, cr = c_vec(f, inputs, vec, cycles, d)
            if ct == "crash":
                return f"{tag}: {cr}\n{circ}"
            if cr is not None and cr != sr:
                return (f"{tag}: sim vs C mismatch\n  vec={vec} cycles={cycles}\n"
                        f"  sim: {sr}\n  c:   {cr}\n{circ}")
            if not skip_v and not NO_V:
                vt, vr = v_vec(f, inputs, outputs, vec, cycles, d)
                if vt == "crash":
                    return f"{tag}: {vr}\n{circ}"
                if vr is not None and vr != sr:
                    return (f"{tag}: sim vs iverilog mismatch\n"
                            f"  vec={vec} cycles={cycles}\n"
                            f"  sim: {sr}\n  v:   {vr}\n{circ}")
        return None


def main():
    global NO_V, KEEP_GOING
    iters = 2000
    seed = 20260809
    only = None
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--iters" or a.startswith("--iters="):
            iters = int(a.split("=", 1)[1] if "=" in a else sys.argv[i + 1])
            i += 2 if "=" not in a else 1
        elif a == "--seed" or a.startswith("--seed="):
            seed = int(a.split("=", 1)[1] if "=" in a else sys.argv[i + 1])
            i += 2 if "=" not in a else 1
        elif a == "--only" or a.startswith("--only="):
            only = a.split("=", 1)[1] if "=" in a else sys.argv[i + 1]
            i += 2 if "=" not in a else 1
        elif a == "--no-v":
            NO_V = True
            i += 1
        elif a == "--keep-going":
            KEEP_GOING = True
            i += 1
        else:
            i += 1

    rng = random.Random(seed)
    corpus = load_examples()
    if os.path.isdir(REGRESS):
        for f in sorted(os.listdir(REGRESS)):
            if f.endswith(".circ"):
                corpus.append(open(os.path.join(REGRESS, f)).read())
    if not corpus:
        print("no example circuits found; run from the repo root")
        sys.exit(1)

    checked = 0
    failures = 0
    t0 = __import__("time").time()
    for n in range(iters):
        mode = only or rng.choice(["structured", "mutate", "mutate"])
        if mode == "structured":
            circ = gen_structured(rng)
            if len(corpus) < 400 and rng.random() < 0.1:
                corpus.append(circ)  # seed the mutation pool
        else:
            circ = gen_mutate(rng, corpus)
        checked += 1
        tag = f"{mode}"
        fail = check_circuit(circ, rng, corpus, tag)
        if fail:
            failures += 1
            path = save_failure(circ, tag)
            print(f"FAIL iteration {n}:\n{fail}\nsaved: {path}")
            if not KEEP_GOING:
                sys.exit(1)
        elif (n + 1) % 500 == 0:
            print(f"  {n+1}/{iters} iterations OK")

    dt = __import__("time").time() - t0
    if failures:
        print(f"DONE: {checked} circuits, {failures} failure(s) (seed={seed}, "
              f"{dt:.0f}s)")
        sys.exit(1)
    print(f"OK: {checked} circuits fuzzed (seed={seed}, {dt:.0f}s) — no failures")
    sys.exit(0)


if __name__ == "__main__":
    main()
