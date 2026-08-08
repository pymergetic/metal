# Frozen CORE modules for metal product (Microdot + asyncio + Inspect app).
# Microdot / Inspect are kernel CORE Py — not wasm packs. See docs/SOURCETREE.md.
#
# Host tree: PEP 420 for pymergetic + pymergetic.metal (no src __init__.py).
# µPy has no namespaces → empty markers from port/frozen_ns only.

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

# PEP 420 parents — freeze markers only (not in src/).
package(
    "pymergetic",
    (
        "__init__.py",
        "metal/__init__.py",
    ),
    base_path="$(METAL)/port/frozen_ns",
    opt=3,
)

# Guest Inspect app (MicrodotAdapter); omit adapter_fastapi (host/CDN only).
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
