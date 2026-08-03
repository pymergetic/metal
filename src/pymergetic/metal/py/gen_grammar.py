#!/usr/bin/env python3
"""Generate `upy/py/grammar.rs` from upstream `external/micropython/py/grammar.h`.

Regenerate:

    python3 src/pymergetic/metal/py/gen_grammar.py

(paths are resolved from this script's own location, so it can be run from
anywhere). Do not hand-edit `grammar.rs` -- edit this script and rerun it.

Feature flags baked into the generated tables (Metal's "core features" combo,
matching MicroPython's MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_CORE_FEATURES
default for every flag `grammar.h` conditions on):
  - MICROPY_PY_ASYNC_AWAIT   = on  (Metal lexer's TokenKind has KwAsync/KwAwait)
  - MICROPY_PY_ASSIGN_EXPR   = on  (walrus `:=`)
  - MICROPY_PY_BUILTINS_SLICE = on (slice subscripts)
  - MICROPY_PY_BUILTINS_SET   = on (set/dict-vs-set disambiguation in dictorsetmaker)
`grammar.h` has no f-string/t-string rules at all in this MicroPython
revision (f-strings are handled entirely in the lexer upstream, which
Metal's lexer.rs already omits) -- so there is nothing to strip for that.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
GRAMMAR_H = ROOT / "external/micropython/py/grammar.h"
OUT = ROOT / "src/pymergetic/metal/py/upy/py/grammar.rs"

FLAGS = {
    "MICROPY_PY_ASYNC_AWAIT": True,
    "MICROPY_PY_ASSIGN_EXPR": True,
    "MICROPY_PY_BUILTINS_SLICE": True,
    "MICROPY_PY_BUILTINS_SET": True,
}

DEF_RULE_RE = re.compile(r"^DEF_RULE\((.*)\)$")
DEF_RULE_NC_RE = re.compile(r"^DEF_RULE_NC\((.*)\)$")


def camel(name: str) -> str:
    """`some_rule_name` / `KW_FALSE` -> `SomeRuleName` / `KwFalse`."""
    return "".join(w.capitalize() for w in name.split("_"))


def split_args(s: str):
    """Split a macro argument list on top-level commas (parens-aware)."""
    args = []
    depth = 0
    cur = ""
    for ch in s:
        if ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def parse_kind(kind_str):
    m = re.match(r"^(\w+)(?:\((\d+)\))?$", kind_str)
    assert m, f"bad rule kind: {kind_str!r}"
    n = int(m.group(2)) if m.group(2) else None
    return m.group(1), n


def parse_arg(arg_str):
    m = re.match(r"^(tok|rule|opt_rule)\((\w+)\)$", arg_str)
    assert m, f"bad rule arg: {arg_str!r}"
    return m.group(1), m.group(2)


class Rule:
    def __init__(self, name, kind, n, args, has_compile_fn):
        self.name = name
        self.kind = kind  # "or" | "and" | "and_ident" | "and_blank" | "one_or_more" | "list" | "list_with_end"
        self.n = n
        self.args = args  # list of ("tok"|"rule"|"opt_rule", NAME)
        self.has_compile_fn = has_compile_fn


def act_byte(kind: str, n: int) -> int:
    if kind == "or":
        return 0x10 | n
    if kind == "and":
        return 0x20 | n
    if kind == "and_ident":
        return 0x20 | n | 0x40
    if kind == "and_blank":
        return 0x20 | n | 0x80
    if kind == "one_or_more":
        return 0x30 | 2
    if kind == "list":
        return 0x30 | 1
    if kind == "list_with_end":
        return 0x30 | 3
    raise AssertionError(f"unknown rule kind {kind!r}")


def parse_grammar():
    text = GRAMMAR_H.read_text()
    rules = []
    in_block_comment = False
    # ctx_stack entries: (parent_active, if_condition_value)
    ctx_stack = []
    active = True

    for raw in text.splitlines():
        line = raw.strip()

        if in_block_comment:
            if "*/" in line:
                in_block_comment = False
            continue
        if line.startswith("/*"):
            if "*/" not in line:
                in_block_comment = True
            continue
        if not line or line.startswith("//"):
            continue

        if line.startswith("#if"):
            cond = line[len("#if"):].strip()
            val = bool(FLAGS.get(cond, False))
            ctx_stack.append((active, val))
            active = active and val
            continue
        if line.startswith("#else"):
            parent_active, val = ctx_stack[-1]
            active = parent_active and (not val)
            continue
        if line.startswith("#endif"):
            parent_active, _val = ctx_stack.pop()
            active = parent_active
            continue
        if not active:
            continue

        nc = False
        m = DEF_RULE_RE.match(line)
        if not m:
            m = DEF_RULE_NC_RE.match(line)
            nc = True
        if not m:
            continue

        parts = split_args(m.group(1))
        name = parts[0]
        if nc:
            kind_str, rest = parts[1], parts[2:]
        else:
            kind_str, rest = parts[2], parts[3:]

        kind, n = parse_kind(kind_str)
        args = [parse_arg(a) for a in rest]
        if n is None:
            n = len(args)
        rules.append(Rule(name, kind, n, args, has_compile_fn=not nc))

    assert not ctx_stack, "unbalanced #if/#endif in grammar.h"
    return rules


def emit(rules):
    with_fn = [r for r in rules if r.has_compile_fn]
    without_fn = [r for r in rules if not r.has_compile_fn]
    ordered = with_fn + [None] + without_fn  # None == RULE_const_object slot
    names = [r.name if r is not None else "const_object" for r in ordered]
    seen = set()
    for n in names:
        assert n not in seen, f"duplicate rule name {n!r}"
        seen.add(n)

    lines = []
    lines.append("//! grammar -- generated rule tables for the Metal parse-tree builder.")
    lines.append("//!")
    lines.append("//! GENERATED by `gen_grammar.py` from `external/micropython/py/grammar.h`.")
    lines.append("//! Regenerate: `python3 src/pymergetic/metal/py/gen_grammar.py`.")
    lines.append("//! Flags baked in: ASYNC_AWAIT/ASSIGN_EXPR/BUILTINS_SLICE/BUILTINS_SET on,")
    lines.append("//! no f-strings/t-strings (see gen_grammar.py FLAGS + module comment).")
    lines.append("//! Do not hand-edit -- edit the generator and rerun it instead.")
    lines.append("")
    lines.append("use crate::upy::py::lexer::TokenKind;")
    lines.append("")
    lines.append("#[repr(u8)]")
    lines.append("#[derive(Debug, Clone, Copy, PartialEq, Eq)]")
    lines.append("pub enum RuleId {")
    for n in names:
        lines.append(f"    {camel(n)},")
    lines.append("}")
    lines.append("")
    lines.append(f"pub const RULE_COUNT: usize = {len(ordered)};")
    lines.append("")
    lines.append("pub const RULE_ACT_ARG_MASK: u8 = 0x0f;")
    lines.append("pub const RULE_ACT_KIND_MASK: u8 = 0x30;")
    lines.append("pub const RULE_ACT_ALLOW_IDENT: u8 = 0x40;")
    lines.append("pub const RULE_ACT_ADD_BLANK: u8 = 0x80;")
    lines.append("pub const RULE_ACT_OR: u8 = 0x10;")
    lines.append("pub const RULE_ACT_AND: u8 = 0x20;")
    lines.append("pub const RULE_ACT_LIST: u8 = 0x30;")
    lines.append("")
    lines.append("#[derive(Debug, Clone, Copy, PartialEq, Eq)]")
    lines.append("pub enum RuleArg {")
    lines.append("    Tok(TokenKind),")
    lines.append("    Rule(RuleId),")
    lines.append("    OptRule(RuleId),")
    lines.append("}")
    lines.append("")

    act_entries = []
    for r in ordered:
        if r is None:
            act_entries.append("0, // RuleId::ConstObject (never dispatched directly)")
        else:
            act_entries.append(f"{hex(act_byte(r.kind, r.n))}, // {r.name}")
    lines.append("pub static RULE_ACT_TABLE: [u8; RULE_COUNT] = [")
    for e in act_entries:
        lines.append(f"    {e}")
    lines.append("];")
    lines.append("")

    lines.append("pub static RULE_ARGS: [&[RuleArg]; RULE_COUNT] = [")
    for r in ordered:
        if r is None:
            lines.append("    &[], // RuleId::ConstObject")
            continue
        parts = []
        for kind, val in r.args:
            if kind == "tok":
                parts.append(f"RuleArg::Tok(TokenKind::{camel(val)})")
            elif kind == "rule":
                parts.append(f"RuleArg::Rule(RuleId::{camel(val)})")
            else:
                parts.append(f"RuleArg::OptRule(RuleId::{camel(val)})")
        lines.append(f"    &[{', '.join(parts)}], // {r.name}")
    lines.append("];")
    lines.append("")

    lines.append("/// Debug-only rule names (upstream `rule_name_table`, always on here --")
    lines.append("/// small and const, useful for `ParseError` messages).")
    lines.append("pub static RULE_NAMES: [&str; RULE_COUNT] = [")
    for n in names:
        lines.append(f'    "{n}",')
    lines.append("];")
    lines.append("")

    lines.append("/// Safe decode of a rule-id byte stored in a struct-node header (the")
    lines.append("/// alternative to an `unsafe transmute::<u8, RuleId>` -- every byte parse.rs")
    lines.append("/// stores there came from `as u8` on a real `RuleId`, so this is only ever")
    lines.append("/// hit with an in-range value; `unreachable!()` documents that invariant")
    lines.append("/// instead of asserting it away with `unsafe`.")
    lines.append("pub fn rule_id_from_u8(v: u8) -> RuleId {")
    lines.append("    match v {")
    for idx, n in enumerate(names):
        lines.append(f"        {idx} => RuleId::{camel(n)},")
    lines.append('        _ => unreachable!("rule id byte out of range"),')
    lines.append("    }")
    lines.append("}")
    lines.append("")

    single = camel("single_input")
    file_ = camel("file_input")
    eval_ = camel("eval_input")
    lines.append("pub const RULE_SINGLE_INPUT: RuleId = RuleId::" + single + ";")
    lines.append("pub const RULE_FILE_INPUT: RuleId = RuleId::" + file_ + ";")
    lines.append("pub const RULE_EVAL_INPUT: RuleId = RuleId::" + eval_ + ";")
    lines.append("")

    OUT.write_text("\n".join(lines) + "\n")


def main():
    rules = parse_grammar()
    emit(rules)
    print(f"wrote {OUT} ({len(rules) + 1} rules incl. RuleId::ConstObject)")


if __name__ == "__main__":
    main()
