"""metal pack / pack inspect — wasm + metal.pkg payload."""
from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

from metal_cli.lib.pkgfmt import (
    WASM_CUSTOM_SECTION_NAME,
    build_minimal_wasm_with_custom_section,
    decode_payload,
    extract_custom_section,
    encode_payload,
    list_ustar_from_lz4,
)
from metal_cli.mod.module_meta import (
    ModuleType,
    load_module_meta,
    module_json_path,
)
from metal_cli.paths import packages_dir


def _stage_module(mod_dir: Path, stage: Path) -> dict[str, str]:
    meta = load_module_meta(mod_dir)
    stage.mkdir(parents=True, exist_ok=True)
    # Copy tree except .gitkeep and build junk
    for src in mod_dir.rglob("*"):
        if not src.is_file():
            continue
        if src.name == ".gitkeep":
            continue
        if "target" in src.parts or ".target" in src.parts or src.suffix == ".pyc":
            continue
        rel = src.relative_to(mod_dir)
        dst = stage / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return meta


async def cmd_pack(path: str, out: str | None = None) -> int:
    mod_dir = Path(path).resolve()
    if not mod_dir.is_dir():
        print(f"metal pack: not a directory: {mod_dir}", file=sys.stderr)
        return 2
    if not module_json_path(mod_dir).is_file():
        print(f"metal pack: refusing {mod_dir} (no .pm/module)", file=sys.stderr)
        return 1
    meta = load_module_meta(mod_dir)
    if meta.get("type") != ModuleType.PACKAGE.value:
        print(
            f"metal pack: refusing {mod_dir} "
            f"(type={meta.get('type')!r}, want package)",
            file=sys.stderr,
        )
        return 1

    with tempfile.TemporaryDirectory(prefix="metal-pack-") as td:
        stage = Path(td) / "stage"
        meta = _stage_module(mod_dir, stage)
        name = meta.get("name", mod_dir.name)
        impl = meta.get("impl", "c")
        version = meta.get("version", "0.1.0")
        manifest = (
            f'name = "{name}"\n'
            f'impl = "{impl}"\n'
            f'version = "{version}"\n'
        )
        payload = encode_payload(manifest, stage)
        wasm = build_minimal_wasm_with_custom_section(WASM_CUSTOM_SECTION_NAME, payload)

    pkg_dir = packages_dir()
    pkg_dir.mkdir(parents=True, exist_ok=True)
    if out:
        out_path = Path(out)
    else:
        out_path = pkg_dir / f"{name.split('.')[-1]}.wasm"
    out_path.write_bytes(wasm)
    print(f"metal pack: wrote {out_path} ({len(wasm)} bytes)")
    return 0


async def cmd_pack_inspect(path: str) -> int:
    p = Path(path).resolve()
    if not p.is_file():
        print(f"metal pack inspect: not a file: {p}", file=sys.stderr)
        return 2
    data = p.read_bytes()
    sec = extract_custom_section(data, WASM_CUSTOM_SECTION_NAME)
    if sec is None:
        print(f"metal pack inspect: no {WASM_CUSTOM_SECTION_NAME!r} section", file=sys.stderr)
        return 1
    manifest, blob, unc = decode_payload(sec)
    print("--- manifest ---")
    print(manifest)
    print("--- archive ---")
    for name in list_ustar_from_lz4(blob, unc):
        print(f"  {name}")
    return 0
