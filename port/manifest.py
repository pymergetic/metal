# Frozen CORE modules for metal product (Microdot + asyncio).
# Microdot is kernel CORE Py — not a wasm pack. See docs/SOURCETREE.md.

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
