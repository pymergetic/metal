# Frozen CORE modules for metal product (Microdot + asyncio + Inspect app).
# Microdot / Inspect / arch are kernel CORE Py — not wasm packs.
# C/RS faces are nested builtins (metal/glue) — no frozen reexports.

include("$(MPY_DIR)/extmod/asyncio")

package(
    "pymergetic",
    (
        "metal/net/microdot/__init__.py",
        "metal/net/microdot/microdot.py",
        "metal/net/microdot/helpers.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)

# Guest Inspect app (MicrodotAdapter); omit adapter_fastapi (host/CDN only).
# Parents: nest builtins (glue) with metal.__path__ — no frozen_ns markers.
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

# Arch seats + post-ready autoexec (boot tree is C: boot/tree.c + port/boot/boot.c).
package(
    "pymergetic",
    (
        "metal/arch/__init__.py",
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

# Host-sim seat identity (into-Py bridges); product runtime remains the unix port.
package(
    "pymergetic",
    (
        "metal/unix/__init__.py",
        "metal/unix/x86/__init__.py",
        "metal/unix/x86/autoexec.py",
        "metal/unix/x86_64/__init__.py",
        "metal/unix/x86_64/autoexec.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)
