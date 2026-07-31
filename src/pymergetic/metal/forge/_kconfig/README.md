# Vendored Kconfiglib

Upstream: https://github.com/ulfalizer/Kconfiglib (ISC license).

Used by forge-private wrappers in this directory (`menuconfig.py`,
`confgen.py`, `oldconfig.py`) via `forge config edit|gen|old`:

- `kconfiglib.py`
- `_menuconfig_ui.py` (upstream `menuconfig.py`)
- `genconfig.py` (reference; Metal uses `confgen.py`)
