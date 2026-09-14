#!/usr/bin/env python3
"""Resolve and validate Metal's pinned browser toolchain."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
PIN = (HERE / "emscripten.version").read_text().strip()
REQUIRED = ("emcc", "emar", "node", "python3", "rustc")


def fail(message: str) -> None:
    raise SystemExit(f"emscripten toolchain: {message}")


def candidate_path() -> str:
    emsdk = os.environ.get("EMSDK")
    path = os.environ.get("PATH", "")
    if emsdk:
        bindir = Path(emsdk) / "upstream" / "emscripten"
        if (bindir / "emcc").is_file():
            path = str(bindir) + os.pathsep + path
    return path


def resolve(path: str) -> dict[str, str]:
    found = {name: shutil.which(name, path=path) or "" for name in REQUIRED}
    missing = [name for name, value in found.items() if not value]
    if missing:
        fail("missing " + ", ".join(missing) + f"; install and activate emsdk {PIN}")
    return found


def emcc_version(emcc: str) -> str:
    proc = subprocess.run([emcc, "--version"], text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)
    if proc.returncode != 0:
        fail(f"{emcc} --version failed")
    match = re.search(r"(?:emcc|Emscripten[^\n]*?)\s+(\d+\.\d+\.\d+)", proc.stdout)
    if not match:
        fail(f"cannot parse version from {emcc} --version")
    return match.group(1)


def validate() -> tuple[str, dict[str, str]]:
    path = candidate_path()
    tools = resolve(path)
    actual = emcc_version(tools["emcc"])
    if actual != PIN:
        fail(f"emcc {actual} found at {tools['emcc']}; expected pinned {PIN}")
    emcc_root = Path(tools["emcc"]).resolve().parent
    emar_root = Path(tools["emar"]).resolve().parent
    if emcc_root != emar_root:
        fail(f"emcc and emar come from different SDK roots: {emcc_root} != {emar_root}")
    rust = subprocess.run([tools["rustc"], "--print", "target-libdir", "--target",
                           "wasm32-unknown-unknown"], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False)
    if rust.returncode != 0:
        fail("Rust target wasm32-unknown-unknown is not installed")
    return path, tools


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("check", "path", "version", "prepare"))
    parser.add_argument("build_dir", nargs="?")
    args = parser.parse_args()
    if args.command == "version":
        print(PIN)
        return
    path, tools = validate()
    if args.command == "path":
        print(path)
        return
    if args.command == "prepare":
        if not args.build_dir:
            fail("prepare requires a build directory")
        build_dir = Path(args.build_dir)
        stamp = build_dir / ".emscripten-toolchain"
        identity = f"emscripten={PIN}\nemcc={Path(tools['emcc']).resolve()}\n"
        if not stamp.is_file() or stamp.read_text() != identity:
            if build_dir.exists():
                shutil.rmtree(build_dir)
            build_dir.mkdir(parents=True)
            stamp.write_text(identity)
        print(f"emscripten toolchain {PIN}: prepared {build_dir}")
        return
    print(f"emscripten toolchain {PIN}: {tools['emcc']}")


if __name__ == "__main__":
    main()
