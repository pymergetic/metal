"""metal CLI argparse entry — async commands; sync helpers elsewhere."""
from __future__ import annotations

import argparse
import asyncio
import sys

from metal_cli import __version__
from metal_cli.integrate.cmd import cmd_integrate
from metal_cli.kernel.cmd import cmd_kernel_br, cmd_kernel_build, cmd_kernel_run
from metal_cli.mod.cmd import (
    cmd_mod_build,
    cmd_mod_check,
    cmd_mod_clean,
    cmd_mod_ls,
    cmd_mod_sync,
    cmd_mod_test,
)
from metal_cli.pack.cmd import cmd_pack, cmd_pack_inspect


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="metal",
        description="Unified Metal toolset (kernel, modules, packages). See docs/TOOLING.md.",
    )
    parser.add_argument("--version", action="version", version=f"metal {__version__}")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_mod = sub.add_parser("mod", help="module codegen / check (.pm/module gate)")
    mod_sub = p_mod.add_subparsers(dest="mod_cmd", required=True)
    p_sync = mod_sub.add_parser(
        "sync",
        help="export impl -> in-memory catalog -> other lang-pool faces",
    )
    p_sync.add_argument(
        "--emit",
        action="append",
        default=[],
        metavar="SLOT",
        help="extra pool slot to emit (repeatable); only toml is non-default today",
    )
    mod_sub.add_parser("check", help="validate .pm/module + {base}.{impl_ext} + banner gate")
    mod_sub.add_parser("clean", help="remove banner-owned generated files + gitignore block")
    mod_sub.add_parser("ls", help="list module trees (generated faces hidden)")
    p_build = mod_sub.add_parser(
        "build",
        help="cargo build --lib for Rust module(s) (default target x86_64-unknown-none)",
    )
    p_build.add_argument(
        "module",
        nargs="?",
        default=None,
        help="module id/stem (e.g. mem) or omit for all rs crates",
    )
    p_build.add_argument(
        "--host",
        action="store_true",
        help="build for host (no --target none)",
    )
    p_build.add_argument(
        "--debug",
        action="store_true",
        help="debug profile (default: release)",
    )
    p_test = mod_sub.add_parser(
        "test",
        help="run module .pm/smoke.{rs|c|cpp|py} on host",
    )
    p_test.add_argument(
        "module",
        nargs="?",
        default=None,
        help="module id (e.g. mem) or omit for all with .pm/smoke.*",
    )
    p_test.add_argument(
        "--debug",
        action="store_true",
        help="debug profile (default: release)",
    )

    p_pack = sub.add_parser("pack", help="pack <module-dir> | pack inspect <file.wasm>")
    p_pack.add_argument(
        "path",
        help="module directory, or 'inspect' then file as --file / second token",
    )
    p_pack.add_argument(
        "file",
        nargs="?",
        default=None,
        help="with path=inspect: the .wasm to inspect",
    )
    p_pack.add_argument("-o", "--out", default=None, help="output .wasm path (pack mode)")

    p_int = sub.add_parser("integrate", help="unpack package for build-against")
    p_int.add_argument("pkg", help=".wasm package")
    p_int.add_argument("--out", required=True, help="output sysroot directory")

    p_kern = sub.add_parser("kernel", help="build/run firmware (facade)")
    kern_sub = p_kern.add_subparsers(dest="kern_cmd", required=True)
    p_kb = kern_sub.add_parser("build", help="build efi|bios|exp2")
    p_kb.add_argument("target", choices=["efi", "bios", "exp2"])
    p_kb.add_argument("arch", nargs="?", default=None, help="bios/exp2 arch (e.g. x86_64)")
    p_kr = kern_sub.add_parser("run", help="run efi|bios|exp2 in QEMU")
    p_kr.add_argument("target", choices=["efi", "bios", "exp2"])
    p_kbr = kern_sub.add_parser("br", help="build then run")
    p_kbr.add_argument("target", choices=["efi", "bios", "exp2"])
    p_kbr.add_argument("arch", nargs="?", default=None, help="bios/exp2 arch (e.g. x86_64)")

    p_br = sub.add_parser("br", help="build then run (shortcut for kernel br)")
    p_br.add_argument("target", nargs="?", default="exp2", choices=["efi", "bios", "exp2"])
    p_br.add_argument("arch", nargs="?", default=None, help="bios/exp2 arch (e.g. x86_64)")

    p_all = sub.add_parser("all", help="mod sync then kernel build")
    p_all.add_argument("target", nargs="?", default="bios", choices=["efi", "bios", "exp2"])
    return parser


async def async_main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.cmd == "mod":
        if args.mod_cmd == "sync":
            extra = frozenset(args.emit) if args.emit else None
            return await cmd_mod_sync(extra_emit=extra)
        if args.mod_cmd == "check":
            return await cmd_mod_check()
        if args.mod_cmd == "clean":
            return await cmd_mod_clean()
        if args.mod_cmd == "ls":
            return await cmd_mod_ls()
        if args.mod_cmd == "build":
            tgt = None if args.host else "x86_64-unknown-none"
            return await cmd_mod_build(
                args.module, release=not args.debug, target=tgt or ""
            )
        if args.mod_cmd == "test":
            return await cmd_mod_test(args.module, release=not args.debug)
    if args.cmd == "pack":
        if args.path == "inspect":
            if not args.file:
                print("metal pack inspect: need a .wasm file", file=sys.stderr)
                return 2
            return await cmd_pack_inspect(args.file)
        return await cmd_pack(args.path, args.out)
    if args.cmd == "integrate":
        return await cmd_integrate(args.pkg, args.out)
    if args.cmd == "kernel":
        if args.kern_cmd == "build":
            return await cmd_kernel_build(args.target, args.arch)
        if args.kern_cmd == "run":
            return await cmd_kernel_run(args.target)
        if args.kern_cmd == "br":
            return await cmd_kernel_br(args.target, args.arch)
    if args.cmd == "br":
        return await cmd_kernel_br(args.target, args.arch)
    if args.cmd == "all":
        rc = await cmd_mod_sync()
        if rc != 0:
            return rc
        return await cmd_kernel_build(args.target)

    parser.print_help()
    return 2


def main(argv: list[str] | None = None) -> int:
    """Sync entry for the launcher — runs the async CLI."""
    return asyncio.run(async_main(argv))
