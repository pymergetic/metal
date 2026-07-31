# Host Python ABI smoke — metal mod test (ctypes + host cdylib).
from __future__ import annotations

import ctypes
import shutil
import subprocess
import sys
from pathlib import Path

# Module root is parent of .pm/
MOD = Path(__file__).resolve().parent.parent
TARGET = MOD / ".target"
_SO_NAME = (
    "pymergetic_metal_mem.dll" if sys.platform == "win32" else "libpymergetic_metal_mem.so"
)
ARENA = 512 * 1024


def _host_environ() -> dict[str, str]:
    # typings/os.pyi is Metal guest os (no environ). Reach host via builtins.
    env = getattr(__import__("os"), "environ", None)
    if env is None:
        return {}
    return {str(k): str(v) for k, v in env.items()}


def _build_host_cdylib() -> Path:
    env = _host_environ()
    env["CARGO_TARGET_DIR"] = str(TARGET)
    # Force shared lib for ctypes; not in Cargo.toml (unsupported on none).
    subprocess.check_call(
        [
            "cargo",
            "rustc",
            "--manifest-path",
            str(MOD / ".pm" / "Cargo.toml"),
            "--lib",
            "--release",
            "--",
            "--crate-type",
            "cdylib",
        ],
        cwd=str(MOD),
        env=env,
    )
    deps = TARGET / "release" / "deps"
    found = sorted(
        list(deps.glob("libpymergetic_metal_mem-*.so"))
        + list(deps.glob(_SO_NAME))
        + list((TARGET / "release").glob(_SO_NAME)),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not found:
        raise SystemExit(f"mem .pm/smoke.py: missing {_SO_NAME} under {TARGET}/release")
    stable = TARGET / "release" / _SO_NAME
    if found[0].resolve() != stable.resolve():
        shutil.copy2(found[0], stable)
    return stable


def main() -> None:
    lib = ctypes.CDLL(str(_build_host_cdylib()))
    lib.pm_metal_mem_init.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    lib.pm_metal_mem_init.restype = ctypes.c_int32
    lib.pm_metal_mem_alloc.argtypes = [ctypes.c_size_t]
    lib.pm_metal_mem_alloc.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.pm_metal_mem_free.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
    lib.pm_metal_mem_free.restype = None
    lib.pm_metal_mem_realloc.argtypes = [
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
    ]
    lib.pm_metal_mem_realloc.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.pm_metal_mem_memalign.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
    lib.pm_metal_mem_memalign.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.pm_metal_mem_map.argtypes = [ctypes.c_size_t]
    lib.pm_metal_mem_map.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.pm_metal_mem_unmap.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    lib.pm_metal_mem_unmap.restype = ctypes.c_int32

    # Host-only seed buffer (fake). On iron this range comes from
    # BIOS/UEFI/DT RAM — not from pm_metal_mem_alloc (needs init first).
    buf = (ctypes.c_uint8 * ARENA)()
    # page-align: over-allocate via mmap-like alignment on the array address
    addr = ctypes.addressof(buf)
    align = 4096
    off = (align - (addr % align)) % align
    if off + ARENA > len(buf):
        # rare: re-alloc larger
        big = (ctypes.c_uint8 * (ARENA + align))()
        addr = ctypes.addressof(big)
        off = (align - (addr % align)) % align
        base = ctypes.cast(addr + off, ctypes.POINTER(ctypes.c_uint8))
        keep = big
    else:
        base = ctypes.cast(addr + off, ctypes.POINTER(ctypes.c_uint8))
        keep = buf
    _ = keep

    assert lib.pm_metal_mem_init(base, ARENA) == 0
    a = lib.pm_metal_mem_alloc(64)
    b = lib.pm_metal_mem_alloc(128)
    assert a and b
    lib.pm_metal_mem_free(b)
    lib.pm_metal_mem_free(a)

    r = lib.pm_metal_mem_alloc(32)
    assert r
    ctypes.memset(r, 0xA5, 32)
    r = lib.pm_metal_mem_realloc(r, 256)
    assert r and r[0] == 0xA5
    r = lib.pm_metal_mem_realloc(r, 0)
    assert not r

    al = lib.pm_metal_mem_memalign(64, 100)
    assert al
    al_addr = ctypes.cast(al, ctypes.c_void_p).value
    assert al_addr is not None and al_addr % 64 == 0
    lib.pm_metal_mem_free(al)
    assert not lib.pm_metal_mem_memalign(3, 16)

    p0 = lib.pm_metal_mem_map(4096)
    p1 = lib.pm_metal_mem_map(4096)
    assert p0 and p1
    assert lib.pm_metal_mem_unmap(p0, 4096) == -1
    assert lib.pm_metal_mem_unmap(p1, 4096) == 0
    assert lib.pm_metal_mem_unmap(p0, 4096) == 0

    print("mem .pm/smoke.py: PASS")


if __name__ == "__main__":
    main()
