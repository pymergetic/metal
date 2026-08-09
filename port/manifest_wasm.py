# Frozen CORE for CDN `mp` (arch.wasm) — Microdot + Inspect + arch.
# C/RS faces: nested builtins under pymergetic.metal (via wasmmod root).

include("$(MPY_DIR)/extmod/asyncio")

package(
    "microdot",
    (
        "__init__.py",
        "microdot.py",
        "helpers.py",
    ),
    base_path="$(METAL)/src/pymergetic/metal",
    opt=3,
)

# Parents: nest builtins (glue / wasmmod) with metal.__path__ — no frozen_ns.

package(
    "pymergetic",
    (
        "metal/inspect/__init__.py",
        "metal/inspect/stubs.py",
        "metal/inspect/self_desc.py",
        "metal/inspect/adapter_microdot.py",
        "metal/inspect/app.py",
        "metal/inspect/dispatch.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)

package(
    "pymergetic",
    (
        "metal/arch/__init__.py",
        "metal/boot/__init__.py",
        "metal/arch/x86/__init__.py",
        "metal/arch/x86/autoexec.py",
        "metal/arch/x86_64/__init__.py",
        "metal/arch/x86_64/autoexec.py",
        "metal/arch/wasm/__init__.py",
        "metal/arch/wasm/sim.py",
        "metal/arch/wasm/autoexec.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)
