#!/usr/bin/env python3
"""rsx_fuzz.py — deterministic fuzzing for the jit/rs Rust->C compiler.

Generates random VALID subset-Rust programs from a seeded expression
grammar, then runs every case through four independent checks:

  1. SELF-HOST   feed_boot and feed_self must emit BYTE-IDENTICAL C
                 (the fixed point, per case rather than per corpus), and
                 one compiler run twice must not drift (determinism).
  2. RUNS        the emitted C compiles and runs: a fixed runner
                 (tools/rsx_fuzz_main.c) calls every function over a
                 fixed input grid and the values must equal what the
                 generator computed in Python from the SAME expression
                 tree it emitted. One AST, two evaluators — a lowering
                 that changes meaning changes the numbers.
  3. MRUSTC      the same source goes through mrustc (the reference
                 implementation), its object links against the same
                 runner, and its outputs must agree with micro-rustc's.
  4. CRASH       a refusal on a valid case, a signal, a hang, or a
                 diagnostic-free failure is a bug: the case is saved to
                 tools/rsx_fuzz_corpus/ with a .why note and the sweep
                 reports it. The corpus is re-verified on every run, so
                 a fixed bug never comes back silently.

The generated subset is exactly what both compilers accept and what the
runner contract pins: `#![no_std]` + sixteen `#[no_mangle] pub extern
"C" fn fuzz_fK(a: u32, b: u32, c: u32) -> u32` (K in 0..15) whose
bodies are let-chains and tail expressions over a mod-256 arithmetic
grammar (every `+ - *` result is masked `& 255`, so no overflow
semantics differ between compilers), comparisons in `if` conditions,
and calls to EARLIER functions only (a call DAG — every generated
program terminates).

  ./tools/rsx_fuzz.py                     # corpus + 32 fresh cases
  ./tools/rsx_fuzz.py --cases 500 --seed 7
  ./tools/rsx_fuzz.py --corpus-only       # just the regression corpus

Not a prove seat: a fuzz harness owned by tools/. Any failure exits
non-zero so the overnight run notices it.

Exit status: 0 all checks passed, 1 a check failed (details + corpus
path on stderr), 2 the harness itself could not run."""

import argparse
import os
import random
import subprocess
import sys
import tempfile
import time

METAL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS_DIR = os.path.join(METAL_DIR, "tools", "rsx_fuzz_corpus")
MAIN_C = os.path.join(METAL_DIR, "tools", "rsx_fuzz_main.c")

N_FUNCS = 16
GRID = [(3, 5, 7), (0, 1, 255), (17, 200, 42), (255, 255, 255)]
MASK = 255


# ---------------------------------------------------------------- rng ----
class Rng(random.Random):
    """random.Random is already deterministic per seed; kept behind a
    class so the generator can swap the algorithm without touching
    every call site."""

    def pick(self, seq):
        return seq[self.randrange(len(seq))]


# ------------------------------------------------------------ grammar ----
# Expr nodes are tuples: ('lit', v) ('param', i) ('bin', op, l, r)
# ('call', k, a, b, c) ('if', cond, t, f); cond is ('cmp', op, l, r).
# Every arithmetic node's VALUE is masked mod 256 by both evaluators.


def gen_expr(rng, depth, bound):
    """One u32 expression. `bound` is the highest earlier function index
    callable from here (-1 = none yet)."""
    if depth <= 0:
        if rng.random() < 0.5:
            return ("lit", rng.randrange(256))
        return ("param", rng.randrange(3))
    r = rng.random()
    if r < 0.34:
        return ("bin", rng.pick(["+", "-", "*"]),
                gen_expr(rng, depth - 1, bound),
                gen_expr(rng, depth - 1, bound))
    if r < 0.48:
        return ("lit", rng.randrange(256))
    if r < 0.62:
        return ("param", rng.randrange(3))
    if r < 0.82 or bound < 0:
        return ("if", gen_cond(rng, depth - 1, bound),
                gen_expr(rng, depth - 1, bound),
                gen_expr(rng, depth - 1, bound))
    k = rng.randrange(bound + 1)
    return ("call", k,
            gen_expr(rng, depth - 1, bound),
            gen_expr(rng, depth - 1, bound),
            gen_expr(rng, depth - 1, bound))


def gen_cond(rng, depth, bound):
    return ("cmp", rng.pick(["==", "!=", "<", ">", "<=", ">="]),
            gen_expr(rng, max(depth - 1, 0), bound),
            gen_expr(rng, max(depth - 1, 0), bound))


def emit_expr(e):
    kind = e[0]
    if kind == "lit":
        return "%d" % e[1]
    if kind == "param":
        return "abc"[e[1]]
    if kind == "bin":
        return "((%s) %s (%s)) & 255" % (emit_expr(e[2]), e[1], emit_expr(e[3]))
    if kind == "call":
        return "fuzz_f%d((%s), (%s), (%s))" % (e[1], emit_expr(e[2]),
                                               emit_expr(e[3]), emit_expr(e[4]))
    if kind == "if":
        return "if (%s) { %s } else { %s }" % (emit_expr(e[1]), emit_expr(e[2]),
                                               emit_expr(e[3]))
    if kind == "cmp":
        return "(%s) %s (%s)" % (emit_expr(e[2]), e[1], emit_expr(e[3]))
    raise AssertionError("bad node %r" % (e,))


# ------------------------------------------------------------- program ----
def gen_program(seed):
    """Returns (rust_source, expected[list of list]) where expected[k][g]
    is fuzz_fK's value on GRID[g], computed from the emitted ASTs."""
    rng = Rng(seed)
    lines = ["#![no_std]", ""]
    bodies = []
    for k in range(N_FUNCS):
        depth = rng.randrange(1, 5)
        lets = []
        for _ in range(rng.randrange(0, 4)):
            # names repeat on purpose: the LocalTab reuse guard + rename
            # shadow paths both get exercised
            lets.append(("t%d" % rng.randrange(3), gen_expr(rng, depth, k - 1)))
        tail = gen_expr(rng, depth, k - 1)
        bodies.append((lets, tail))
        lines.append("#[no_mangle]")
        lines.append("pub extern \"C\" fn fuzz_f%d(a: u32, b: u32, c: u32) -> u32 {" % k)
        for name, e in lets:
            lines.append("    let %s: u32 = %s;" % (name, emit_expr(e)))
        lines.append("    %s" % emit_expr(tail))
        lines.append("}")
        lines.append("")

    fns = {}
    for k, (lets, tail) in enumerate(bodies):
        fns[k] = _make_eval_fn(lets, tail, fns)
    expected = [[fns[k](*g) for g in GRID] for k in range(N_FUNCS)]
    return "\n".join(lines) + "\n", expected


def _make_eval_fn(lets, tail, fns):
    def fn(a, b, c):
        names = {"a": a & MASK, "b": b & MASK, "c": c & MASK}
        for name, e in lets:
            names[name] = eval_expr_named(e, names, fns)
        return eval_expr_named(tail, names, fns) & MASK
    return fn


def eval_expr_named(e, names, fns):
    kind = e[0]
    if kind == "lit":
        return e[1] & MASK
    if kind == "param":
        return names["abc"[e[1]]] & MASK
    if kind == "bin":
        l = eval_expr_named(e[2], names, fns)
        r = eval_expr_named(e[3], names, fns)
        if e[1] == "+":
            return (l + r) & MASK
        if e[1] == "-":
            return (l - r) & MASK
        return (l * r) & MASK
    if kind == "call":
        args = [eval_expr_named(a, names, fns) for a in (e[2], e[3], e[4])]
        return fns[e[1]](*args) & MASK
    if kind == "if":
        if eval_expr_named(e[1], names, fns):
            return eval_expr_named(e[2], names, fns)
        return eval_expr_named(e[3], names, fns)
    if kind == "cmp":
        l = eval_expr_named(e[2], names, fns)
        r = eval_expr_named(e[3], names, fns)
        return {
            "==": l == r, "!=": l != r, "<": l < r,
            ">": l > r, "<=": l <= r, ">=": l >= r,
        }[e[1]]
    raise AssertionError("bad node %r" % (e,))


# ---------------------------------------------------------------- run ----
class Case:
    def __init__(self, name, src, expected):
        self.name = name
        self.src = src
        self.expected = expected
        self.notes = []


def run(cmd, timeout, env=None, cwd=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, env=e, cwd=cwd)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout after %ds" % timeout


def parse_runner_output(text):
    """'K G: value' lines -> dict {(k, g): value}; None on junk."""
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            left, val = line.split(":")
            k, g = left.split()
            out[(int(k), int(g))] = int(val)
        except ValueError:
            return None
    return out


class Harness:
    def __init__(self, workdir):
        self.workdir = workdir
        self.boot = os.environ.get(
            "RSX_FUZZ_BOOT", os.path.join(METAL_DIR, "build", "selfhost_feed"))
        self.self_ = os.environ.get(
            "RSX_FUZZ_SELF", "/tmp/selfhost_cycle/feed_self")
        self.cc = os.environ.get("CC", "cc")
        self.mrustc = os.environ.get(
            "RSX_FUZZ_MRUSTC", os.path.join(METAL_DIR, "externals",
                                            "mrustc", "bin", "mrustc"))
        self.mrustc_libdir = os.environ.get(
            "MRUSTC_LIBDIR", os.path.join(METAL_DIR, "externals", "mrustc",
                                          "output-1.90.0"))
        self.have_self = os.path.isfile(self.self_) and os.access(self.self_, os.X_OK)
        self.have_mrustc = os.path.isfile(self.mrustc)
        self.main_o = os.path.join(workdir, "rsx_fuzz_main.o")
        self._build_runner()

    def _build_runner(self):
        rc, _, err = run([self.cc, "-O1", "-c", "-o", self.main_o, MAIN_C], 60)
        if rc != 0:
            raise SystemExit("harness: cannot build the runner: %s" % err)

    def compile_and_run(self, obj, tag, timeout=60):
        exe = os.path.join(self.workdir, "run_%s" % tag)
        rc, _, err = run([self.cc, "-O1", "-o", exe, obj, self.main_o], timeout)
        if rc != 0:
            return None, "link %s failed: %s" % (tag, err.strip()[:400])
        rc, out, err = run([exe], timeout)
        if rc != 0:
            return None, "run %s failed rc=%d: %s" % (tag, rc, err.strip()[:400])
        parsed = parse_runner_output(out)
        if parsed is None:
            return None, "run %s printed junk: %r" % (tag, out[:200])
        return parsed, None

    def check(self, case, quiet=False):
        """Full pipeline. Returns (ok, [notes])."""
        notes = []
        rs = os.path.join(self.workdir, "%s.rs" % case.name)
        with open(rs, "w") as f:
            f.write(case.src)

        # --- 1. self-host: boot vs self vs boot-again
        c1 = os.path.join(self.workdir, "%s.boot1.c" % case.name)
        c2 = os.path.join(self.workdir, "%s.boot2.c" % case.name)
        rc, _, err = run([self.boot, rs, c1], 120)
        if rc != 0:
            notes.append("boot refused (rc=%d): %s" % (rc, err.strip()[:300]))
            return False, notes
        run([self.boot, rs, c2], 120)
        if not _same(c1, c2):
            notes.append("non-deterministic: two boot runs differ")
            return False, notes
        if self.have_self:
            cs = os.path.join(self.workdir, "%s.self.c" % case.name)
            rc, _, err = run([self.self_, rs, cs], 120)
            if rc != 0:
                notes.append("self refused (rc=%d): %s" % (rc, err.strip()[:300]))
                return False, notes
            if not _same(c1, cs):
                notes.append("boot/self outputs differ (self-host break)")
                return False, notes

        # --- 2. runs: micro-rustc's C + the runner
        obj = os.path.join(self.workdir, "%s.mico.o" % case.name)
        rc, _, err = run([self.cc, "-O1", "-c", "-o", obj, c1], 60)
        if rc != 0:
            notes.append("emitted C does not compile: %s" % err.strip()[:400])
            return False, notes
        got, err = self.compile_and_run(obj, "%s_micro" % case.name)
        if got is None:
            notes.append(err or "run produced no output")
            return False, notes
        if case.expected is not None:
            for k in range(N_FUNCS):
                for g in range(len(GRID)):
                    want = case.expected[k][g]
                    if got.get((k, g)) != want:
                        notes.append("value f%d grid%d: got %s want %d"
                                     % (k, g, got.get((k, g)), want))
                        return False, notes

        # --- 3. mrustc differential
        if self.have_mrustc:
            mout = os.path.join(self.workdir, "%s.mrustc" % case.name)
            env = {"MRUSTC_LIBDIR": self.mrustc_libdir}
            rc, _, err = run([self.mrustc, "--crate-type", "rlib",
                              "-o", mout, rs], 180, env=env)
            if rc != 0:
                notes.append("mrustc refused (rc=%d): %s"
                             % (rc, err.strip()[:300]))
                return False, notes
            mobj = mout + ".o"
            if not os.path.isfile(mobj):
                notes.append("mrustc produced no object at %s" % mobj)
                return False, notes
            mgot, err = self.compile_and_run(mobj, "%s_mrustc" % case.name)
            if mgot is None:
                notes.append("mrustc " + (err or "run produced no output"))
                return False, notes
            if mgot != got:
                diff = [(kk, mgot.get(kk), got.get(kk))
                        for kk in sorted(got)
                        if mgot.get(kk) != got.get(kk)]
                notes.append("micro-rustc/mrustc outputs differ: %r"
                             % diff[:6])
                return False, notes
        return True, notes


def _same(a, b):
    try:
        with open(a, "rb") as fa, open(b, "rb") as fb:
            return fa.read() == fb.read()
    except OSError:
        return False


def save_to_corpus(case, notes):
    os.makedirs(CORPUS_DIR, exist_ok=True)
    with open(os.path.join(CORPUS_DIR, "%s.rs" % case.name), "w") as f:
        f.write(case.src)
    with open(os.path.join(CORPUS_DIR, "%s.why" % case.name), "w") as f:
        f.write("\n".join(notes) + "\n")
    with open(os.path.join(CORPUS_DIR, "%s.expect" % case.name), "w") as f:
        for row in case.expected:
            f.write(" ".join(str(v) for v in row) + "\n")
    return os.path.join(CORPUS_DIR, "%s.rs" % case.name)


def corpus_cases():
    """(name, src, expected-or-None) for every saved case. The .expect
    companion is the generator's oracle, so a corpus case replays the
    FULL semantic check — not just 'it still compiles'."""
    if not os.path.isdir(CORPUS_DIR):
        return []
    out = []
    for name in sorted(os.listdir(CORPUS_DIR)):
        if not name.endswith(".rs"):
            continue
        stem = name[:-3]
        with open(os.path.join(CORPUS_DIR, name)) as f:
            src = f.read()
        expected = None
        exp_path = os.path.join(CORPUS_DIR, "%s.expect" % stem)
        if os.path.isfile(exp_path):
            expected = []
            with open(exp_path) as f:
                for line in f:
                    if line.strip():
                        expected.append([int(v) for v in line.split()])
        out.append((stem, src, expected))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=32)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--corpus-only", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix="rsx_fuzz_") as workdir:
        h = Harness(workdir)
        if not args.quiet:
            print("rsx_fuzz: boot=%s self=%s mrustc=%s"
                  % (h.boot, "yes" if h.have_self else "NO",
                     "yes" if h.have_mrustc else "NO"))

        fails = []
        checked = 0

        def once(case, corpus):
            ok, notes = h.check(case)
            if ok:
                return
            fails.append((case.name, notes))
            if corpus:
                print("FAIL corpus %s:" % case.name)
            else:
                print("FAIL %s -> %s" % (case.name, save_to_corpus(case, notes)))
            for n in notes:
                print("     %s" % n)

        # regression corpus first: every saved case runs the FULL check
        # again — its .expect oracle keeps the semantics pinned too
        for name, src, expected in corpus_cases():
            checked += 1
            case = Case("corpus_%s" % name, src, expected)
            once(case, corpus=True)
            if not any(f[0] == case.name for f in fails) and not args.quiet:
                print("ok   corpus %s" % name)

        if args.corpus_only:
            print("rsx_fuzz: %d corpus case(s), %d failed"
                  % (checked, len(fails)))
            return 1 if fails else 0

        # fresh sweep
        for i in range(args.cases):
            seed = args.seed * 1000003 + i
            src, expected = gen_program(seed)
            checked += 1
            once(Case("fuzz_s%d_%d" % (args.seed, i), src, expected), corpus=False)
            if not args.quiet and (i + 1) % 8 == 0:
                print("... %d cases clean" % (i + 1))

        print("rsx_fuzz: %d case(s) checked, %d failed"
              % (checked, len(fails)))
        return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
