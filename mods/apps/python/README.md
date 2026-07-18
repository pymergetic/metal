# apps/python

Guest paths (embedded when `PM_METAL_APP_PYTHON=1` — same on every platform):

| Source | Guest path | Package |
|--------|------------|---------|
| `build/cpython/python.wasm` | `/mods/apps/python.wasm` | `mods-apps-python` |
| `mods/apps/python/pm-test.py` | `/mods/apps/pm-test.py` | `mods-apps-python` |
| `external/cpython/Lib/` (full) | `/lib/python3.14/` | `python-stdlib` |
| wasi `lib.wasi-wasm32-3.14/` sysconfig | `/lib/python3.14/` (folded) | `python-stdlib` |

Binary from `scripts/build.d/guest/cpython.sh`. Packages from
`scripts/lib/guest-pkgs.sh` (linked on Linux/NuttX/Zephyr; applied via
`pm_metal_pkg_apply_all()` at boot). `mods-apps-python` depends on `python-stdlib`.

```text
PYTHONHOME=/
/mods/apps/python.wasm --version
/mods/apps/python.wasm /mods/apps/pm-test.py
```
