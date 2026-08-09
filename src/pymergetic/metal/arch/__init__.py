"""Metal architecture seats: x86 / x86_64 / wasm.

C build sets PM_METAL_CFG_ARCH_* (see include/pymergetic/metal/arch.h).
This module picks the matching Py seat for autoexec / sim.
"""

_NAMES = ("x86", "x86_64", "wasm")


def names():
    return _NAMES


def current():
    """Return the arch module for this seat."""
    try:
        import sys

        # Frozen browser builds use platform webassembly.
        plat = getattr(sys, "platform", "")
        if plat in ("webassembly", "emscripten") or "wasm" in str(plat):
            from pymergetic.metal.arch import wasm

            return wasm
        # Optional override from C/autoexec: sys.metal_arch = "x86_64"
        forced = getattr(sys, "metal_arch", None)
        if forced == "x86":
            from pymergetic.metal.arch import x86

            return x86
        if forced == "wasm":
            from pymergetic.metal.arch import wasm

            return wasm
    except Exception:
        pass
    from pymergetic.metal.arch import x86_64

    return x86_64


def name():
    return current().NAME


def firmware():
    """Firmware face string for the current seat (bios/uefi/browser/unix)."""
    cur = current()
    fn = getattr(cur, "firmware", None)
    if callable(fn):
        return fn()
    return getattr(cur, "FIRMWARE", "")


def autoexec():
    """Run the current seat's autoexec epilogue."""
    return current().autoexec()
