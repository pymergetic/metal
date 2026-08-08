# Frozen CORE modules for metal product (Microdot + asyncio + Inspect app).
# Microdot / Inspect are kernel CORE Py — not wasm packs. See docs/SOURCETREE.md.

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

# Guest Inspect app (MicrodotAdapter); omit adapter_fastapi (host/CDN only).
package(
    "pymergetic",
    (
        "__init__.py",
        "metal/__init__.py",
        "metal/inspect/__init__.py",
        "metal/inspect/stubs.py",
        "metal/inspect/adapter_microdot.py",
        "metal/inspect/app.py",
        "metal/inspect/dispatch.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)
