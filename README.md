# pymergetic-metal

Pymergetic: **engine** (native per target) + **orchestrator** (portable wasm) + **instances** (mods, apps).

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Engine** — `host/<plat>/`: probe, publish host info (`pm_hostinfo` → `/sys/pm`), WASI impl, run orchestrator wasm.  
**Orchestrator** — `guest/`: root wasm policy — boot, layout, instance loader.  
**Instances** — `mods/`, `apps/`: wasm units the orchestrator loads after boot.

`backup/1st_try/` is reference only — not the codebase being built.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/NAMING.md](docs/NAMING.md) | **Symbol prefixes** — module path → `pm_*` names |
| [docs/LAYERS.md](docs/LAYERS.md) | Roles + layer model |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Repo layout (`include/` / `host/` / `guest/`) |
| [docs/PLATFORM.md](docs/PLATFORM.md) | Contract / policy / port split |
| [docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md) | RAM probes, `/sys/pm` handoff |
| [docs/MEMORY_BASELINE.md](docs/MEMORY_BASELINE.md) | Fake vs real metal numbers (reference) |

**Symmetry rule:** `include/pymergetic/metal/<mod>.h` + engine `.c` under `host/<plat>/` + orchestrator `.c` under `guest/`.

---

## Layout

```
packages/metal/
├── include/              contract
├── host/<plat>/          engine (linux, zephyr, rump, unikraft)
├── guest/                orchestrator (wasm32-wasip1)
├── mods/, apps/          instances
├── scripts/
├── docs/
├── west-manifest/
└── backup/1st_try/       reference only
```

---

## Build status

Greenfield rebuild — skeleton in place. First slice: `pm_sys` + `pm_hostinfo` + orchestrator `boot.c` on linux and zephyr engines in parallel.

---

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Engine + orchestrator + instance loading |
| **kernel** | CPython 3.14, vendored C libs, language runtime |
