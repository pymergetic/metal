"""Parse docs/MODULE_MATRIX.md seat table + snapshot."""

from __future__ import annotations

from pathlib import Path

METAL = Path(__file__).resolve().parents[2]
MATRIX_MD = METAL / "docs" / "MODULE_MATRIX.md"
TYPINGS = METAL / "typings" / "pymergetic" / "metal"
GLUE = METAL / "glue" / "pymergetic" / "metal"

# Py-muscle seats: frozen CORE (no glue nest face).
FROZEN_SEATS = frozenset(
    {
        "arch",
        "arch.wasm",
        "arch.x86",
        "arch.x86_64",
        "unix.x86",
        "unix.x86_64",
        "inspect",
        "net.microdot",
    }
)


def parse_rows(text: str | None = None) -> list[dict]:
    if text is None:
        text = MATRIX_MD.read_text(encoding="utf-8")
    rows: list[dict] = []
    for line in text.splitlines():
        if not line.startswith("| `"):
            continue
        parts = [p.strip() for p in line.strip().strip("|").split("|")]
        if len(parts) < 12:
            continue
        path = parts[0].strip("`")
        try:
            api, c, rs, py = (int(parts[i]) for i in (2, 3, 4, 5))
        except ValueError:
            continue
        rows.append(
            {
                "path": path,
                "impl": parts[1],
                "api": api,
                "c": c,
                "rs": rs,
                "py": py,
                "async": parts[6],
                "stub": parts[7],
                "browser": parts[8],
                "fw": parts[9],
                "note": parts[10],
                "dev": parts[11],
            }
        )
    return rows


def parse_snapshot(text: str | None = None) -> dict[str, str]:
    if text is None:
        text = MATRIX_MD.read_text(encoding="utf-8")
    snap: dict[str, str] = {}
    in_snap = False
    for line in text.splitlines():
        if line.startswith("## Snapshot"):
            in_snap = True
            continue
        if in_snap and line.startswith("## "):
            break
        if not in_snap or not line.startswith("|"):
            continue
        parts = [p.strip() for p in line.strip().strip("|").split("|")]
        if len(parts) < 2 or parts[0] in ("Metric", "------"):
            continue
        snap[parts[0]] = parts[1].strip("*")
    return snap


def pyi_candidates(path: str) -> list[Path]:
    parts = path.split(".")
    return [
        TYPINGS.joinpath(*parts).with_suffix(".pyi"),
        TYPINGS.joinpath(*parts) / "__init__.pyi",
    ]


def has_pyi(path: str) -> bool:
    return any(p.is_file() for p in pyi_candidates(path))


def glue_candidates(path: str) -> list[Path]:
    parts = path.split(".")
    return [
        GLUE.joinpath(*parts).with_suffix(".c"),
        GLUE.joinpath(*parts) / "__init__.c",
    ]


def has_glue(path: str) -> bool:
    return any(p.is_file() for p in glue_candidates(path))


def frozen_src_rel(path: str) -> str:
    """Relative path under src/pymergetic/ for a frozen seat package file."""
    return "metal/" + path.replace(".", "/") + "/__init__.py"


def manifest_has_frozen(manifest: Path, path: str) -> bool:
    text = manifest.read_text(encoding="utf-8")
    rel = frozen_src_rel(path)
    # arch.wasm → metal/arch/wasm/__init__.py
    return rel in text or rel.replace("/__init__.py", "/") in text
