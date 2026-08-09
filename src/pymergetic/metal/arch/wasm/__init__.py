"""wasm metal seat — full simulated experiment machine in the browser."""

NAME = "wasm"
FIRMWARE = "browser"
VERSION = "0.1.0"
CPU = "wasm32"

from pymergetic.metal.arch.wasm import sim as sim  # noqa: F401


def boot_sections():
    return sim.boot_sections()


def autoexec():
    from pymergetic.metal.arch.wasm.autoexec import run

    return run()
