"""metal mod sync|check|build|test — lang-pool codegen + crate facade.

Pipeline:

  human impl  --export-->  in-memory catalog  --emit-->  other pool faces

Pool slots: c (covers cpp), rs, py, toml (output-only, not default).
Never regenerate the human source. Prefer ``metal mod build|test`` over raw cargo.
"""
from __future__ import annotations

import asyncio
import json
import re
import sys
from pathlib import Path

from metal_cli.mod.catalog import (
    emit_c_header,
    emit_catalog_toml,
    emit_pyi,
    emit_rs_bindings,
)
from metal_cli.mod.c_export import catalog_from_c
from metal_cli.mod.lang_pool import (
    IMPL_EXT,
    POOL,
    all_face_rels,
    emit_slots,
    face_rel,
    pool_slot,
)
from metal_cli.mod.module_meta import (
    PM_DIR,
    PM_MODULE_FILE,
    PM_SMOKE_STEM,
    ModuleType,
    cargo_toml_path,
    discover_hidden,
    discover_modules,
    is_hidden_dir,
    legacy_markers_under,
    load_module_meta,
    module_json_path,
    src_roots,
)
from metal_cli.mod.rust_export import catalog_from_rust
from metal_cli.paths import exp2_root, metal_root


def _host_environ() -> dict[str, str]:
    """Host CPython env. ``typings/os.pyi`` is Metal guest os (no environ)."""
    env = getattr(__import__("os"), "environ", None)
    if env is None:
        return {}
    return dict(env)


def _rmtree(path: Path) -> None:
    """Remove a file or directory tree (stdlib shutil-free)."""
    if path.is_symlink() or path.is_file():
        path.unlink()
        return
    if not path.is_dir():
        return
    for child in path.iterdir():
        _rmtree(child)
    path.rmdir()


# ASCII table signs for pool-face status (clean / sync)
_STATUS_SIGN = {
    "original": "§",
    "cleaned": "X",
    "emitted": "*",
    "marker": "~",  # empty __init__.pyi package marker (no symbols)
    "pruned": "x",
    "noop": "-",
}


# Package entry is always __init__.{ext} — same for every language. No stem-name alias.
ENTRY_STEM = "__init__"

# Fixed v2 module schema under ``.pm/``:
#   .pm/module       — JSON metadata (type + name/impl/…)
#   .pm/Cargo.toml   — Rust crate (when impl=rs)
#   .pm/build.{ext}  — native/tooling build hook
#   .pm/smoke.{ext}  — host smoketest (metal mod test)


def _mod_dir_rel(mod_dir: Path) -> str:
    """Module directory id under a src root (no stem)."""
    for root in src_roots():
        try:
            return mod_dir.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
    return mod_dir.name


def _stem_id(mod_dir: Path, stem: str) -> str:
    """Table id: dir for package entry, ``dir/stem`` for sibling sources."""
    rel = _mod_dir_rel(mod_dir)
    if stem == ENTRY_STEM:
        return rel
    return f"{rel}/{stem}"


def _module_id(mod_dir: Path, meta: dict[str, str] | None = None) -> str:
    """Module directory id (package entry is always ``__init__``)."""
    del meta  # kept for call-site compatibility
    return _mod_dir_rel(mod_dir)


def _human_source(mod_dir: Path, meta: dict[str, str] | None, impl: str) -> Path:
    """Package entry: ``__init__.{ext}``. For ``impl=c``, prefer ``.h`` then ``.c``."""
    del meta
    if impl in ("c", "cpp"):
        for ext in (("h", "c") if impl == "c" else ("hpp", "h", "cpp")):
            p = mod_dir / f"{ENTRY_STEM}.{ext}"
            if p.is_file():
                return p
        return mod_dir / f"{ENTRY_STEM}.{'h' if impl == 'c' else 'cpp'}"
    return mod_dir / f"{ENTRY_STEM}.{IMPL_EXT[impl]}"

# Extra skip list beyond ``_*.{ext}`` (schema lives under ``.pm/``, not as stems).
_SKIP_STEMS = frozenset()
_SKIP_STEM_PREFIXES = ()
_SKIP_STEM_SUFFIXES = ("_bin",)


def _impl_source_exts(impl: str) -> list[str]:
    """Filename extensions scanned as human impl sources."""
    if impl == "c":
        return ["h", "c"]
    if impl == "cpp":
        return ["hpp", "h", "cpp"]
    return [IMPL_EXT[impl]]


def _impl_sources(mod_dir: Path, impl: str) -> list[Path]:
    """Every human stem in the module dir (not only package entry).

    Skips ``_*.{ext}`` and ``*_bin``. Schema files live under ``.pm/``.
    For ``impl=c``, header-first (``.h``) then ``.c`` bodies at module root.
    """
    seen: set[str] = set()
    out: list[Path] = []
    for ext in _impl_source_exts(impl):
        for p in sorted(mod_dir.glob(f"*.{ext}")):
            if not p.is_file():
                continue
            stem = p.stem
            if stem in seen:
                continue
            if stem == ENTRY_STEM:
                seen.add(stem)
                out.append(p)
                continue
            if stem.startswith("_"):
                continue
            if stem in _SKIP_STEMS:
                continue
            if any(stem.startswith(x) for x in _SKIP_STEM_PREFIXES):
                continue
            if any(stem.endswith(x) for x in _SKIP_STEM_SUFFIXES):
                continue
            seen.add(stem)
            out.append(p)
    # Package entry first for stable sync order
    out.sort(key=lambda p: (0 if p.stem == ENTRY_STEM else 1, p.name.lower()))
    return out

# In-file ownership hint (must stay in generated_banner() output).
_GENERATED_HINT = "DO NOT HAND-EDIT THIS FILE."


def _normalize_rel(rel: str) -> str:
    norm = rel.replace("\\", "/")
    while norm.startswith("./"):
        norm = norm[2:]
    if not norm or norm.startswith("/") or ".." in norm.split("/"):
        raise PermissionError(f"codegen refuses unsafe path: {rel!r}")
    return norm


def _under_hidden(mod_dir: Path, rel: str) -> Path | None:
    """Return a ``type=hidden`` ancestor of REL (under mod_dir)."""
    parts = Path(rel).parts
    for i in range(1, len(parts)):
        d = mod_dir.joinpath(*parts[:i])
        if is_hidden_dir(d):
            return d
    return None


def _file_has_generated_hint(path: Path) -> bool:
    """True if PATH carries the metal mod sync ownership banner."""
    if not path.is_file():
        return False
    try:
        head = path.read_text(encoding="utf-8", errors="replace")[:4096]
    except OSError:
        return False
    return _GENERATED_HINT in head


def _drop_legacy_generated_dir(mod_dir: Path) -> None:
    """Remove obsolete ``.generated/`` shadow-marker trees if present."""
    gen = mod_dir / ".generated"
    if gen.is_dir():
        _rmtree(gen)


_GITIGNORE_BEGIN = "# BEGIN metal-generated"
_GITIGNORE_END = "# END metal-generated"


def list_generated_rels(mod_dir: Path) -> list[str]:
    """Return top-level files in MOD_DIR that carry the generated banner."""
    if not mod_dir.is_dir():
        return []
    out: list[str] = []
    for p in sorted(mod_dir.iterdir(), key=lambda x: x.name.lower()):
        if p.is_file() and _file_has_generated_hint(p):
            out.append(p.name)
    return out


def update_module_gitignore(mod_dir: Path, generated_rels: list[str] | None = None) -> None:
    """Rewrite the managed ignore block from banner-owned faces on disk."""
    if generated_rels is None:
        generated_rels = list_generated_rels(mod_dir)
    # Cargo out dir; keep legacy .generated/ ignored while trees drain.
    patterns = [".target/", "target/", ".generated/"]
    for rel in generated_rels:
        norm = rel.replace("\\", "/").lstrip("./")
        if norm and norm not in patterns:
            patterns.append(norm)
    block = "\n".join([_GITIGNORE_BEGIN, *patterns, _GITIGNORE_END]) + "\n"

    gi = mod_dir / ".gitignore"
    if gi.is_file():
        text = gi.read_text(encoding="utf-8")
        if _GITIGNORE_BEGIN in text and _GITIGNORE_END in text:
            pre = text.split(_GITIGNORE_BEGIN, 1)[0]
            post = text.split(_GITIGNORE_END, 1)[1]
            if post.startswith("\n"):
                post = post[1:]
            text = pre + block + post
        else:
            if text and not text.endswith("\n"):
                text += "\n"
            text = text + ("\n" if text else "") + block
    else:
        text = block
    gi.write_text(text, encoding="utf-8")


def clear_module_gitignore_block(mod_dir: Path) -> None:
    """Remove the managed metal-generated block from module .gitignore."""
    gi = mod_dir / ".gitignore"
    if not gi.is_file():
        return
    text = gi.read_text(encoding="utf-8")
    if _GITIGNORE_BEGIN not in text or _GITIGNORE_END not in text:
        return
    pre = text.split(_GITIGNORE_BEGIN, 1)[0]
    post = text.split(_GITIGNORE_END, 1)[1]
    if post.startswith("\n"):
        post = post[1:]
    text = pre + post
    if text.strip():
        gi.write_text(text, encoding="utf-8")
    else:
        gi.unlink()


def write_generated(mod_dir: Path, rel: str, content: str, *, claim: bool = False) -> None:
    """Write REL under mod_dir if the in-file banner gate allows it.

    Ownership = file contains ``DO NOT HAND-EDIT THIS FILE.`` (or missing).
    claim=True: allow taking over a human file once.
    Always refreshes module .gitignore from banner-owned faces.
    """
    norm = _normalize_rel(rel)
    if norm == ".generated" or norm.startswith(".generated/"):
        raise PermissionError(f"codegen refuses path under .generated/: {norm}")
    blocked = _under_hidden(mod_dir, norm)
    if blocked is not None:
        raise PermissionError(
            f"codegen refuses path under hidden module ({blocked.relative_to(mod_dir)}): {norm}"
        )
    out = mod_dir / norm
    if out.exists() and not _file_has_generated_hint(out) and not claim:
        raise PermissionError(
            f"refusing to overwrite human file (no generated banner): {out}"
        )
    if _GENERATED_HINT not in content:
        raise PermissionError(
            f"codegen refuses content without ownership banner for {norm}"
        )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(content, encoding="utf-8")
    _drop_legacy_generated_dir(mod_dir)
    update_module_gitignore(mod_dir)


def _remove_marked(mod_dir: Path, rel: str) -> bool:
    """Delete a banner-owned generated file. Returns True if something went."""
    out = mod_dir / rel
    gone = False
    if out.is_file() and _file_has_generated_hint(out):
        out.unlink()
        gone = True
    # Legacy shadow marker (pre-banner gate)
    legacy = mod_dir / ".generated" / f"{rel}.generated"
    if legacy.is_file():
        legacy.unlink()
        gone = True
    return gone


def _pool_clean_status(
    mod_dir: Path, meta: dict[str, str], stem: str | None = None
) -> dict[str, str]:
    """Per pool slot before clean: original | cleaned | noop."""
    impl = meta.get("impl", "")
    base = stem if stem is not None else ENTRY_STEM
    own = pool_slot(impl) if impl in IMPL_EXT else None
    out: dict[str, str] = {}
    for slot in POOL:
        if slot == own:
            out[slot] = "original"
            continue
        rel = face_rel(slot, base)
        path = mod_dir / rel
        # toml: any on-disk dump counts as cleaned (never human)
        if path.is_file() and (_file_has_generated_hint(path) or slot == "toml"):
            out[slot] = "cleaned"
        else:
            out[slot] = "noop"
    return out

def _format_pool_table(
    rows: list[tuple[str, dict[str, str]]],
    *,
    legend: str,
) -> str:
    """Plain-ASCII table: module id x pool slots (signs only).

    Module id is ``{src-relative-dir}/{stem}`` e.g. ``pymergetic/metal/boot/boot``.
    """
    if not rows:
        return ""
    mod_w = max(len("module"), max(len(r[0]) for r in rows))
    slot_w = max(len(s) for s in POOL)
    head = f"{'module':<{mod_w}}  " + "  ".join(f"{s:^{slot_w}}" for s in POOL)
    lines = [head, "-" * len(head)]
    for mod, st in rows:
        cells = "  ".join(
            f"{_STATUS_SIGN.get(st.get(s, 'noop'), '?'):^{slot_w}}" for s in POOL
        )
        lines.append(f"{mod:<{mod_w}}  {cells}")
    lines.append(legend)
    return "\n".join(lines)


def clean_module_generated(mod_dir: Path, meta: dict[str, str] | None = None) -> list[str]:
    """Delete every banner-owned generated file; clear gitignore block.

    Also drops unmarked ``{base}.toml`` / legacy ``catalog.toml`` (dev dumps)
    and obsolete ``.generated/`` shadow trees.
    """
    removed: list[str] = []
    for rel in list_generated_rels(mod_dir):
        if _remove_marked(mod_dir, rel):
            removed.append(rel)
    # Dev toml dumps: never human source (stem faces + legacy name).
    toml_candidates = ["catalog.toml"]
    if meta is not None:
        impl = meta.get("impl", "")
        stems = {ENTRY_STEM}
        if impl in IMPL_EXT:
            stems |= {p.stem for p in _impl_sources(mod_dir, impl)}
        for stem in sorted(stems):
            toml_candidates.append(face_rel("toml", stem))
    for rel in toml_candidates:
        path = mod_dir / rel
        if path.is_file() and rel not in removed:
            path.unlink()
            removed.append(rel)
    _drop_legacy_generated_dir(mod_dir)
    # Cargo / host smoketest out dirs (metal mod build|test → .target/)
    for name in (".target", "target"):
        d = mod_dir / name
        if d.is_dir():
            _rmtree(d)
            removed.append(name + "/")
    clear_module_gitignore_block(mod_dir)
    return removed


async def cmd_mod_check() -> int:
    mods = discover_modules()
    hidden = discover_hidden()
    errors = 0
    for root in src_roots():
        for legacy in legacy_markers_under(root):
            print(
                f"metal mod check: leftover root marker {legacy} "
                f"(use {PM_DIR}/{PM_MODULE_FILE})",
                file=sys.stderr,
            )
            errors += 1
    for d in hidden:
        try:
            rel = d.relative_to(metal_root())
        except ValueError:
            rel = d
        print(f"  hidden   {rel}")
    if not mods and not hidden:
        print(
            f"metal mod check: no {PM_DIR}/{PM_MODULE_FILE} directories found",
            file=sys.stderr,
        )
        return 1
    for mod_dir in mods:
        try:
            meta = load_module_meta(mod_dir)
        except (OSError, ValueError, json.JSONDecodeError) as e:
            print(f"metal mod check: {mod_dir}: bad {PM_DIR}/{PM_MODULE_FILE}: {e}", file=sys.stderr)
            errors += 1
            continue
        if meta.get("type") != ModuleType.MODULE.value:
            continue
        impl = meta.get("impl", "")
        if impl not in IMPL_EXT:
            print(f"metal mod check: {mod_dir}: bad or missing impl={impl!r}", file=sys.stderr)
            errors += 1
            continue
        sources = _impl_sources(mod_dir, impl)
        if not sources:
            print(
                f"metal mod check: {mod_dir}: no human impl sources "
                f"(__init__ or sibling stems)",
                file=sys.stderr,
            )
            errors += 1
        if (mod_dir / "_impl").is_dir():
            print(
                f"metal mod check: {mod_dir}: leftover _impl/",
                file=sys.stderr,
            )
            errors += 1
        for src in sources:
            if _file_has_generated_hint(src):
                print(
                    f"metal mod check: {mod_dir}: human {src.name} carries generated banner "
                    f"(stem collision)",
                    file=sys.stderr,
                )
                errors += 1
        try:
            rel = mod_dir.relative_to(metal_root())
        except ValueError:
            rel = mod_dir
        stem_bits: list[str] = []
        for src in sources:
            faces = [face_rel(s, src.stem) for s in emit_slots(impl)]
            stem_bits.append(f"{src.name}->[{','.join(faces)}]")
        print(
            f"  ok  {rel}  impl={impl}  pool={pool_slot(impl)}  "
            f"stems={','.join(stem_bits) if stem_bits else '(none)'}"
        )
    if errors:
        print(f"metal mod check: {errors} error(s)", file=sys.stderr)
        return 1
    print(f"metal mod check: {len(mods)} module(s), {len(hidden)} hidden ok")
    return 0


def _export_to_catalog(impl: str, human: Path):
    """Human source -> in-memory Catalog. Never reads faces."""
    slot = pool_slot(impl)
    if slot == "rs":
        return catalog_from_rust(human)
    if slot == "c":
        return catalog_from_c(human)
    raise FileNotFoundError(
        f"{human.parent}: no exporter for impl={impl!r} (pool slot {slot!r})"
    )


def _emit_slot(
    mod_dir: Path,
    cat,
    slot: str,
    *,
    name: str,
    base: str,
    human_name: str,
) -> str:
    """Emit one pool face from the in-memory catalog. Returns REL written."""
    if slot == "c":
        rel = face_rel("c", base)
        write_generated(mod_dir, rel, emit_c_header(name, base, cat, human_name))
        return rel
    if slot == "rs":
        rel = face_rel("rs", base)
        write_generated(mod_dir, rel, emit_rs_bindings(name, base, cat, human_name))
        return rel
    if slot == "py":
        rel = face_rel("py", base)
        write_generated(mod_dir, rel, emit_pyi(name, base, cat, human_name))
        return rel
    if slot == "toml":
        rel = face_rel("toml", base)
        write_generated(mod_dir, rel, emit_catalog_toml(cat, human_name, rel), claim=True)
        return rel
    raise ValueError(f"no emitter for slot {slot!r}")


def _prune_unemitted(mod_dir: Path, base: str, slots: list[str]) -> list[str]:
    """Drop banner-owned pool faces that are not in this sync's emit set."""
    keep = {face_rel(s, base) for s in slots}
    pruned: list[str] = []
    for rel in all_face_rels(base):
        if rel in keep:
            continue
        if _remove_marked(mod_dir, rel):
            pruned.append(rel)
    # Legacy fixed name (pre stem-symmetric toml)
    legacy = "catalog.toml"
    if legacy not in keep and _remove_marked(mod_dir, legacy):
        pruned.append(legacy)
    elif legacy not in keep and (mod_dir / legacy).is_file():
        (mod_dir / legacy).unlink()
        pruned.append(legacy)
    return pruned


def _sync_stem(
    mod_dir: Path,
    meta: dict[str, str],
    human: Path,
    *,
    slots: list[str],
) -> tuple[str, dict[str, str], set[str]]:
    """Export one human source stem; return (id, status, emitted rels).

    No exported fns → skip ``.h`` / consumer ``.rs`` (private helpers stay
    quiet). Exception: package entry always gets ``__init__.pyi`` when the
    py slot is in the emit set (linter/package marker; never ``__init__.py``).
    """
    stem = human.stem
    impl = meta.get("impl", "")
    own = pool_slot(impl)
    pkg = meta.get("name", mod_dir.name)
    if stem == ENTRY_STEM:
        name = pkg
    else:
        # Sibling stem under the package: mem.lock + spin → mem.lock.spin
        name = f"{pkg}.{stem}" if pkg else stem
    cat = _export_to_catalog(impl, human)
    emitted: set[str] = set()
    emit_slots_now = list(slots)
    marker_slots: set[str] = set()
    if not cat.has_border():
        keep: list[str] = []
        for s in slots:
            if s == "toml":
                keep.append(s)
            elif s == "py" and stem == ENTRY_STEM:
                # Empty pyi package marker (typing); never emit __init__.py.
                keep.append(s)
                marker_slots.add(s)
        emit_slots_now = keep
    for slot in emit_slots_now:
        rel = _emit_slot(
            mod_dir, cat, slot, name=name, base=stem, human_name=human.name
        )
        emitted.add(rel)
    pruned_rels = set(_prune_unemitted(mod_dir, stem, emit_slots_now))
    status: dict[str, str] = {}
    for slot in POOL:
        if slot == own:
            status[slot] = "original"
        elif slot in marker_slots:
            status[slot] = "marker"
        elif slot in emit_slots_now:
            status[slot] = "emitted"
        elif face_rel(slot, stem) in pruned_rels:
            status[slot] = "pruned"
        else:
            status[slot] = "noop"
    return _stem_id(mod_dir, stem), status, emitted


def _prune_orphan_faces(mod_dir: Path, keep_stems: set[str]) -> list[str]:
    """Drop banner-owned pool faces whose stem is no longer a human source."""
    pruned: list[str] = []
    for rel in list_generated_rels(mod_dir):
        stem = Path(rel).stem
        if stem in keep_stems:
            continue
        if _remove_marked(mod_dir, rel):
            pruned.append(rel)
    legacy = "catalog.toml"
    p = mod_dir / legacy
    if p.is_file():
        _remove_marked(mod_dir, legacy)
        if p.is_file():
            p.unlink()
        pruned.append(legacy)
    return pruned

def _sync_one(
    mod_dir: Path,
    meta: dict[str, str],
    *,
    extra_emit: frozenset[str] | None = None,
) -> list[tuple[str, dict[str, str]]]:
    """Export + emit every impl-language stem; return table rows.

    Package entry ``__init__.{ext}`` is optional when sibling stems exist
    (e.g. ``boot/platform/{console,mem_map,…}.h``).
    """
    impl = meta.get("impl", "")
    sources = _impl_sources(mod_dir, impl)
    if not sources:
        raise FileNotFoundError(
            f"no human impl sources under {mod_dir} (__init__ or sibling stems)"
        )
    slots = emit_slots(impl, extra_emit)
    rows: list[tuple[str, dict[str, str]]] = []
    for human in sources:
        mid, status, _emitted = _sync_stem(mod_dir, meta, human, slots=slots)
        rows.append((mid, status))
    _prune_orphan_faces(mod_dir, {p.stem for p in sources})

    update_module_gitignore(mod_dir)
    _drop_legacy_generated_dir(mod_dir)
    legacy = mod_dir / ".metal_generated"
    if legacy.is_file():
        legacy.unlink()
    return rows

def _is_generated_rel(mod_dir: Path, rel: str) -> bool:
    """True if REL is legacy .generated/ or a banner-owned face."""
    norm = rel.replace("\\", "/").lstrip("./")
    if norm == ".generated" or norm.startswith(".generated/"):
        return True
    return _file_has_generated_hint(mod_dir / norm)


def _human_entries(mod_dir: Path, rel_dir: str = "") -> list[tuple[str, bool]]:
    """Sorted (name, is_dir) under rel_dir, skipping generated paths."""
    d = mod_dir if not rel_dir else mod_dir / rel_dir
    if not d.is_dir():
        return []
    out: list[tuple[str, bool]] = []
    for p in sorted(d.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
        if p.name in (".", "..", "target", ".target", "Cargo.lock", ".generated"):
            continue
        child_rel = p.name if not rel_dir else f"{rel_dir}/{p.name}"
        if _is_generated_rel(mod_dir, child_rel):
            continue
        out.append((p.name, p.is_dir()))
    return out


def _format_human_tree(mod_dir: Path, meta: dict[str, str]) -> str:
    """ASCII tree of human-owned files (generated faces hidden)."""
    mid = _module_id(mod_dir, meta)
    lines = [mid + "/"]

    def walk(rel_dir: str, prefix: str) -> None:
        entries = _human_entries(mod_dir, rel_dir)
        for i, (name, is_dir) in enumerate(entries):
            last = i == len(entries) - 1
            branch = "`-- " if last else "|-- "
            lines.append(f"{prefix}{branch}{name}{'/' if is_dir else ''}")
            if is_dir:
                child_rel = name if not rel_dir else f"{rel_dir}/{name}"
                walk(child_rel, prefix + ("    " if last else "|   "))

    walk("", "")
    return "\n".join(lines)


async def cmd_mod_ls() -> int:
    """List module trees with generated faces hidden."""
    mods = discover_modules()
    if not mods:
        print(f"metal mod ls: no {PM_DIR}/{PM_MODULE_FILE} modules found", file=sys.stderr)
        return 1
    blocks: list[str] = []
    for mod_dir in mods:
        meta = load_module_meta(mod_dir)
        blocks.append(_format_human_tree(mod_dir, meta))
    print("\n\n".join(blocks))
    print(f"metal mod ls: {len(mods)} module(s) (generated hidden)")
    return 0


def _resolve_modules(spec: str | None) -> list[Path]:
    """Resolve ``mem`` / ``pymergetic/metal/mem`` / path → module dirs."""
    mods = discover_modules()
    if not spec:
        return mods
    spec = spec.strip().rstrip("/")
    p = Path(spec)
    if p.is_dir() and module_json_path(p).is_file():
        if ModuleType(load_module_meta(p.resolve()).get("type", "")) == ModuleType.MODULE:
            return [p.resolve()]
    hits: list[Path] = []
    for mod_dir in mods:
        meta = load_module_meta(mod_dir)
        mid = _module_id(mod_dir, meta)
        if spec in (mid, mod_dir.name, meta.get("name", "")):
            hits.append(mod_dir)
            continue
        if mid.endswith("/" + spec) or mid.endswith(spec):
            hits.append(mod_dir)
    return hits


async def _cargo(mod_dir: Path, args: list[str]) -> int:
    manifest = cargo_toml_path(mod_dir)
    if not manifest.is_file():
        print(f"metal mod: {mod_dir}: no {PM_DIR}/Cargo.toml", file=sys.stderr)
        return 1
    # ``--manifest-path`` is a per-subcommand flag (cargo run --manifest-path …).
    if not args:
        print(f"metal mod: {mod_dir}: empty cargo args", file=sys.stderr)
        return 1
    cmd = ["cargo", args[0], "--manifest-path", str(manifest), *args[1:]]
    mid = _module_id(mod_dir, load_module_meta(mod_dir))
    env = _host_environ()
    # Dot-prefix so module trees stay clean (`target/` is ugly next to sources).
    env["CARGO_TARGET_DIR"] = str((mod_dir / ".target").resolve())
    print(f"  $ {' '.join(cmd)}  ({mid})", flush=True)
    proc = await asyncio.create_subprocess_exec(*cmd, cwd=str(mod_dir), env=env)
    return await proc.wait()


async def cmd_mod_build(
    spec: str | None = None,
    *,
    release: bool = True,
    target: str = "x86_64-unknown-none",
) -> int:
    """Build Rust module crate(s) via cargo (firmware lib by default)."""
    mods = _resolve_modules(spec)
    if not mods:
        print(f"metal mod build: no module matched {spec!r}", file=sys.stderr)
        return 1
    errors = 0
    built = 0
    for mod_dir in mods:
        meta = load_module_meta(mod_dir)
        if meta.get("impl") != "rs":
            print(f"  skip {_module_id(mod_dir, meta)}  (impl!={meta.get('impl')!r}, not rs)")
            continue
        if not cargo_toml_path(mod_dir).is_file():
            print(f"  skip {_module_id(mod_dir, meta)}  (no {PM_DIR}/Cargo.toml)")
            continue
        args = ["build", "--lib"]
        if release:
            args.append("--release")
        if target:
            args.extend(["--target", target])
        rc = await _cargo(mod_dir, args)
        if rc != 0:
            errors += 1
        else:
            built += 1
            print(f"  ok   {_module_id(mod_dir, meta)}")
    if errors:
        print(f"metal mod build: {errors} error(s)", file=sys.stderr)
        return 1
    print(f"metal mod build: {built} crate(s)")
    return 0


_SMOKE_EXTS = ("rs", "c", "cpp", "py")


def _list_pm_smokes(mod_dir: Path) -> list[Path]:
    """All ``.pm/smoke.{rs,c,cpp,py}`` present (ABI proofs may outlive impl lang)."""
    out: list[Path] = []
    pm = mod_dir / PM_DIR
    for ext in _SMOKE_EXTS:
        p = pm / f"{PM_SMOKE_STEM}.{ext}"
        if p.is_file():
            out.append(p)
    return out


def _cargo_package_name(mod_dir: Path) -> str | None:
    cargo = cargo_toml_path(mod_dir)
    if not cargo.is_file():
        return None
    for line in cargo.read_text(encoding="utf-8").splitlines():
        m = re.match(r'^\s*name\s*=\s*"([^"]+)"\s*$', line)
        if m:
            return m.group(1)
    return None


async def _ensure_host_rust_lib(mod_dir: Path, *, release: bool) -> Path | None:
    """``cargo build --lib`` for host; return path to ``lib{name}.a`` or None."""
    name = _cargo_package_name(mod_dir)
    if not name:
        return None
    args = ["build", "--lib"]
    if release:
        args.append("--release")
    rc = await _cargo(mod_dir, args)
    if rc != 0:
        return None
    profile = "release" if release else "debug"
    lib = mod_dir / ".target" / profile / f"lib{name}.a"
    return lib if lib.is_file() else None


async def _run_pm_smoke_file(mod_dir: Path, smoke: Path, *, release: bool) -> int:
    """Run one host ``.pm/smoke.*``. Returns process exit code."""
    ext = smoke.suffix.lstrip(".").lower()
    if ext == "rs":
        args = ["run", "--bin", PM_SMOKE_STEM]
        if release:
            args.append("--release")
        return await _cargo(mod_dir, args)
    if ext == "py":
        cmd = [sys.executable, str(smoke)]
        print(f"  $ {' '.join(cmd)}", flush=True)
        proc = await asyncio.create_subprocess_exec(*cmd, cwd=str(mod_dir))
        return await proc.wait()
    if ext in ("c", "cpp"):
        out_dir = mod_dir / ".target"
        out_dir.mkdir(parents=True, exist_ok=True)
        out = out_dir / f"{PM_SMOKE_STEM}_{ext}"
        host_env = _host_environ()
        cc = host_env.get("CXX" if ext == "cpp" else "CC", "c++" if ext == "cpp" else "cc")
        cmd = [cc, "-O2", "-I", str(mod_dir), "-o", str(out), str(smoke)]
        # Link host Rust staticlib when this module is a Cargo crate (impl=rs ABI).
        lib_a = await _ensure_host_rust_lib(mod_dir, release=release)
        if lib_a is not None:
            cmd.extend([str(lib_a), "-lpthread", "-ldl", "-lm"])
        print(f"  $ {' '.join(cmd)}", flush=True)
        proc = await asyncio.create_subprocess_exec(*cmd, cwd=str(mod_dir), env=host_env)
        rc = await proc.wait()
        if rc != 0:
            return rc
        print(f"  $ {out}", flush=True)
        proc = await asyncio.create_subprocess_exec(str(out), cwd=str(mod_dir))
        return await proc.wait()
    print(f"metal mod test: no runner for {smoke.name}", file=sys.stderr)
    return 1


async def cmd_mod_test(spec: str | None = None, *, release: bool = True) -> int:
    """Run every host ``.pm/smoke.{rs|c|cpp|py}`` present under matched modules."""
    mods = _resolve_modules(spec)
    if not mods:
        print(f"metal mod test: no module matched {spec!r}", file=sys.stderr)
        return 1
    errors = 0
    ran = 0
    for mod_dir in mods:
        meta = load_module_meta(mod_dir)
        mid = _module_id(mod_dir, meta)
        smokes = _list_pm_smokes(mod_dir)
        if not smokes:
            print(f"  skip {mid}  (no {PM_DIR}/{PM_SMOKE_STEM}.*)")
            continue
        # Faces are gitignored; C/cpp smokes need generated ``*.h`` present.
        try:
            _sync_one(mod_dir, meta)
        except (OSError, ValueError, FileNotFoundError) as e:
            print(f"  fail {mid}  (sync before smoke: {e})", file=sys.stderr)
            errors += 1
            continue
        for smoke in smokes:
            rc = await _run_pm_smoke_file(mod_dir, smoke, release=release)
            rel = f"{PM_DIR}/{smoke.name}"
            if rc != 0:
                print(f"  fail {mid}  ({rel})", file=sys.stderr)
                errors += 1
            else:
                ran += 1
                print(f"  ok   {mid}  ({rel})")
    if errors:
        print(f"metal mod test: {errors} error(s)", file=sys.stderr)
        return 1
    if ran == 0:
        print("metal mod test: nothing to run", file=sys.stderr)
        return 1
    print(f"metal mod test: {ran} smoke(s)")
    return 0


async def cmd_mod_clean() -> int:
    """Remove all auto-generated module faces (marker-owned only)."""
    mods = discover_modules()
    if not mods:
        print(f"metal mod clean: no {PM_DIR}/{PM_MODULE_FILE} modules found", file=sys.stderr)
        return 1
    n = 0
    rows: list[tuple[str, dict[str, str]]] = []
    for mod_dir in mods:
        meta = load_module_meta(mod_dir)
        impl = meta.get("impl", "")
        stems: list[str]
        if impl in IMPL_EXT:
            srcs = _impl_sources(mod_dir, impl)
            stems = [p.stem for p in srcs] if srcs else [ENTRY_STEM]
        else:
            stems = [ENTRY_STEM]
        for stem in stems:
            rows.append((_stem_id(mod_dir, stem), _pool_clean_status(mod_dir, meta, stem)))
        removed = clean_module_generated(mod_dir, meta)
        n += len(removed)
    print(
        _format_pool_table(
            rows,
            legend="§=original  X=cleaned  -=noop",
        )
    )
    print(
        f"metal mod clean: removed {n} generated file(s) in "
        f"{len(mods)} module(s), {len(rows)} stem(s)"
    )
    return 0


async def cmd_mod_sync(*, extra_emit: frozenset[str] | None = None) -> int:
    mods = discover_modules()
    if not mods:
        print(f"metal mod sync: no {PM_DIR}/{PM_MODULE_FILE} modules found", file=sys.stderr)
        return 1
    errors = 0
    rows: list[tuple[str, dict[str, str]]] = []
    synced_mods = 0
    for mod_dir in mods:
        try:
            meta = load_module_meta(mod_dir)
        except (OSError, ValueError, json.JSONDecodeError) as e:
            print(f"metal mod sync: {mod_dir}: bad meta: {e}", file=sys.stderr)
            errors += 1
            continue
        if meta.get("type") != ModuleType.MODULE.value:
            print(
                f"metal mod sync: refusing {mod_dir} (type={meta.get('type')!r})",
                file=sys.stderr,
            )
            return 1
        impl = meta.get("impl", "")
        if impl not in IMPL_EXT:
            print(f"metal mod sync: skip {mod_dir}: bad impl", file=sys.stderr)
            continue
        try:
            rows.extend(_sync_one(mod_dir, meta, extra_emit=extra_emit))
            synced_mods += 1
        except PermissionError as e:
            print(f"metal mod sync: {e}", file=sys.stderr)
            errors += 1
        except Exception as e:
            print(f"metal mod sync: {mod_dir}: {e}", file=sys.stderr)
            errors += 1
    if rows:
        print(
            _format_pool_table(
                rows,
                legend="§=original  *=emitted  ~=marker  x=pruned  -=noop",
            )
        )
    if errors:
        print(f"metal mod sync: {errors} error(s)", file=sys.stderr)
        return 1
    print(f"metal mod sync: {synced_mods} module(s), {len(rows)} stem(s)")
    return 0