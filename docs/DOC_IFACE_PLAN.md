# Plan: docs registry, iface packs, HTTP UI, exec

One document. Spec + locked interfaces + file-exact runbook.

Status: **implemented** — P, S0, I (I-A…I-G), II (II-A…II-G) landed;
`./scripts/build efi` and `./scripts/build bios x86_64` both green with no
new warnings. Part III (HTTP) not started (see Agent claims). Run
[Part IV](#part-iv--exec-runbook) §0 first when implementing further.

| Part | Contents |
|------|----------|
| [P](#part-p--prelude-git-tag-version) | Dynamic `PM_METAL_VERSION` from git tag |
| [0](#part-0--shared-model) | Two machines, base+face, definers, agents |
| [I](#part-i--callable-docs-registry) | Doc catalog design, preview, phases A–G |
| [II](#part-ii--iface-packages--symbol-table) | Header packs + NativeSymbol table |
| [III](#part-iii--http-json--html-ui) | Microdot + utemplate (after I-E + II-D) |
| [IV](#part-iv--exec-runbook) | Post-scan + S0 → … → HTTP |

---

## Part P — Prelude: git-tag version

**Today:** hand-bumped string in
`include/pymergetic/metal/version.h`:

```c
#define PM_METAL_VERSION "0.1.0-experimental"
```

Used by banners (`py_shell.c`), kernel about (`authors.c`), etc.

**Do (small, before or parallel with S0):**

1. Build step (efi + bios `default.sh`, early like `gen_py_stubs`) runs
   something like:

   ```bash
   # prefer annotated/lightweight tag describing this commit; else describe
   ver=$(git describe --tags --dirty --always 2>/dev/null || echo "0.0.0-unknown")
   ```

   Write generated header (do **not** hand-edit), e.g.
   `build/pm_metal_version.inc.h` or replace contents of a generated
   sibling included by `version.h`:

   ```c
   #define PM_METAL_VERSION "v0.2.0"           /* or v0.2.0-3-gabc1234-dirty */
   ```

2. `version.h` becomes a stable wrapper:

   ```c
   #include "pm_metal_version.inc.h"  /* build-generated; path via -I build/ */
   ```

   Keep a `#ifndef` fallback string if the inc is missing (clangd /
   odd one-file compiles).

3. Tag convention (locked for humans): `vMAJOR.MINOR.PATCH` on releases;
   untagged builds use `git describe` (`v0.2.0-5-gdeadbee[-dirty]`).
   Strip a leading `v` in the `#define` only if existing call sites assume
   no `v` — today the hand string has no `v`; pick one shape in the claim
   and match banners (`Metal %s`).

4. No new runtime API required unless useful:
   `const char *pm_metal_version_cstr(void);` → `PM_METAL_VERSION`
   (optional; mirrors `pm_metal_py_version_cstr`).

**Accept:** clean tree on tag `vX.Y.Z` → `PM_METAL_VERSION` is that version;
dirty/untagged → describe string; efi/bios both regenerate; banners/about
print it.

**Owns:** `version.h`, tiny `scripts/gen_metal_version.sh` (or inline in
default.sh), efi/bios build wire, `-I` to generated inc.  
**Must not:** invent a second version string beside `PM_METAL_VERSION`.

Add as **P** at the front of Part IV exec order: `P → S0 → I-A …`.

---

## Part 0 — Shared model

### Two registration machines

| Machine | What it exports | Cross-border call |
|---------|-----------------|-------------------|
| **Mod** `register_func` / `register_cmd` | Mod callables + shell cmds | host, guest, mod↔mod, shell, `pmcmd`, `metal.mod.*` |
| **Firmware faces** | Optional faces on a `pm_metal_*` body | C/guest = dual-ABI; Python = `PY_BIND`; shell = only if `SHELL_CMD` |

`PY_BIND` is the **Python call face**, not a universal exporter. Docs on
that face are readable from C/shell/Py via `doc.*`; that does **not** mean
every language calls through Python.

### Base + face (what vs how)

```text
summary  — what it does (shared meaning; language-agnostic when possible)
sig      — how you call it on THIS face (py/c/shell differ; shell often a lot)
body     — optional longer face detail
```

| Situation | Write |
|-----------|--------|
| One face only | `summary` + optional `sig`/`body` on that face |
| Same face, many readers (shell ≡ pmcmd) | **one** home; all readers share pointers |
| Several call shapes | same `summary` idea, **separate** `sig`/`body` per face |

### Definers

| Definer | Defines |
|---------|---------|
| `SHELL_CMD` / `SHELL_CMD_DOC` | Shell cmd name + summary (+ sig/body) + handler |
| `register_cmd(name, func, help)` | Same shell/pmcmd face (help → summary); live table |
| `register_func` / `register_func_doc` | Mod callable → export (+ optional summary/sig/body) |
| `PY_BIND` / `PY_BIND_DOC` | Python attribute path + callable (+ optional summary/sig/body) |
| Python `"""..."""` | Docs on that Python object (live); catalog reads it |
| `NativeSymbol` / dual-ABI | **Call** import only (Part II) — **no** help text |
| iface `doc_key` | Pointer `"kind:key"` into doc catalog — **no** new text |

### Readers

| Face home | Readers |
|-----------|---------|
| Shell row | `help`, `pmcmd.__doc__`, `doc.lookup("shell",…)` |
| Py bind / Py `__doc__` | `attr.__doc__`, `doc.lookup("py",…)` |
| Mod func doc | `metal.mod.*.__doc__`, `doc.lookup("mod",…)` |

**Required:** shell enumeration and `pmcmd` use the **live** cmd table
(not only the linker section) so guest `register_cmd` ≡ host `SHELL_CMD`.

### Parallel agents

No cross-chat lock. **Claims table at end of this file = lock.**

1. Claim a phase before editing; edit only that phase’s files.
2. Prefer **one agent** running Part IV sequentially (shared glue:
   `metal.h`, `Metal.inf`, build scripts, `py.c`, `wasm.c`).
3. No `git commit` unless human asks.
4. Dialect + ASCII in printed strings (`AGENTS.md`, metal-c-dialect,
   metal-ascii-strings).
5. After `src/pymergetic/metal/**` edits:

   ```bash
   grep -rlE '#include\s*<(Uefi\.h|Library/|Protocol/|IndustryStandard/)' \
     src/pymergetic/metal --include=*.c --include=*.h
   ```

   Must be empty.

---

## Part I — Callable docs registry

### Design lock

**View** (adapter — pointers into face homes, no copy):

```c
typedef enum {
  PM_METAL_DOC_SHELL = 1,
  PM_METAL_DOC_PY    = 2,
  PM_METAL_DOC_MOD   = 3
} pm_metal_doc_kind_t;

typedef struct {
  pm_metal_doc_kind_t kind;
  const char         *key;
  const char         *summary;
  const char         *sig;
  const char         *body;
} pm_metal_doc_view_t;
```

Kind strings: `shell` / `py` / `mod`.

**Shell** — trailing fields; old `{name,help,fn}` still valid (zero-fill):

```c
typedef struct pm_metal_shell_cmd {
  const char           *name;
  const char           *help;   /* summary */
  pm_metal_shell_cmd_fn fn;
  const char           *sig;
  const char           *body;
} pm_metal_shell_cmd_t;
```

`PM_METAL_SHELL_CMD_DOC(var, name, help, sig, body, fn)`.  
View: `summary=help`. `pmcmd.__doc__`: join non-NULL help/sig/body with `\n\n`.

**Py bind**:

```c
typedef struct pm_metal_py_bind {
  const char         *mod;
  const char         *name;
  void               *fn;
  pm_metal_py_class_t class_;
  const char         *summary;
  const char         *sig;
  const char         *body;
} pm_metal_py_bind_t;
```

`PM_METAL_PY_BIND_DOC(..., summary, sig, body)`. Old `PY_BIND` → NULLs.

**Mod:** `register_func_doc(name, export, summary, sig, body)`;  
`register_cmd` → shell face.

**API (dual-ABI required):**

```c
int32_t pm_metal_doc_count(void);
int32_t pm_metal_doc_at(uint32_t i, pm_metal_doc_view_t *out);
int32_t pm_metal_doc_lookup(pm_metal_doc_kind_t kind, const char *key,
                            pm_metal_doc_view_t *out);
int32_t pm_metal_doc_lookup_key(const char *doc_key, pm_metal_doc_view_t *out);
void    pm_metal_doc_print(pm_metal_doc_kind_t kind, const char *key);
void    pm_metal_doc_print_index(pm_metal_doc_kind_t kind);
```

`doc_key`: `"<kind>:<key>"`, first `:` only.

```python
import pymergetic.metal.doc as doc
doc.lookup("shell", "mem")
doc.lookup_key("py:pymergetic.metal.fs.open")
doc.list(kind="shell")
doc.help("mem")
```

### Interface preview (LOCKED)

```c
PM_METAL_SHELL_CMD_DOC(
  g_pm_metal_shell_cmd_mem,
  "mem", g_mem_summary, g_mem_sig, g_mem_body, CoreMemCmd);

PM_METAL_PY_BIND_DOC(
  g_py_bind_fs_open, "pymergetic.metal.fs", "open",
  py_fs_open_obj, PM_METAL_PY_SYNC,
  g_fs_open_summary, g_fs_open_sig, g_fs_open_body);

pm_metal_mod_register_func_doc(
  "run", "hello_run",
  "t0_hello entry; log one line and finish", "run() -> Done", NULL);
pm_metal_mod_register_cmd("hello", "run", "Run the t0_hello proof");
```

```text
help / help mem
```

```python
fs.open.__doc__
pmcmd.mem.__doc__
doc.lookup("py", "pymergetic.metal.fs.open")
```

| `doc_key` | lookup |
|-----------|--------|
| `py:pymergetic.metal.fs.open` | `("py", "pymergetic.metal.fs.open")` |
| `shell:mem` | `("shell", "mem")` |
| `mod:hello.run` | `("mod", "hello.run")` |

### Ownership (Part I)

| Phase | Owns |
|-------|------|
| **I-A** | `util/doc.h`, `util/doc.c`, dual-ABI, `metal.h` / `Metal.inf` / `wasm.c` glue |
| **I-B** | `shell_cmd.h/c`, `shell_core_cmds.c` (help), `shell_py_bind.c` |
| **I-C** | `py.h`, `py_bind.c`, `gen_py_stubs.py` |
| **I-D** | `mod_lifecycle.h`, `mod.c` |
| **I-E** | `doc_py_bind.c` |
| **I-F** | seeds only (fs open, mem, t0_hello, py demo) |
| **I-G** | MICROPYTHON.md blurb + verify |

### Phases I-A … I-G

**I-A** — `util/doc` + dual-ABI; shell adapter via live table; no mass seeds.  
**Accept:** efi build; `lookup(SHELL,"help")`.

**I-B** — `sig`/`body` on shell; `help`/`help name`; **live** pmcmd + `__doc__`.  
**Accept:** guest `register_cmd` visible to `pmcmd`.

**I-C** — `PY_BIND_DOC`; `__doc__` install; stubs harvest.  
**Accept:** old `PY_BIND` unchanged.

**I-D** — `register_func_doc`; wrap old `register_func`.  
**Accept:** existing mods compile.

**I-E** — `pymergetic.metal.doc` (`lookup` / `lookup_key` / `list` / `help`).  
**Accept:** import works.

**I-F** — Seeds: `fs_py_bind.c` open; `shell_core_cmds.c` mem; `t0_hello`; py demo `__doc__`.  
**Accept:** ASCII; stubs show open text.

**I-G** — Prose + greps + `scripts/build efi`.

### Part I checklist

- [x] I-A … I-G
- [x] No duplicate text for shell ≡ pmcmd
- [x] No EDK2 under `src/pymergetic/metal/**`
- [x] No non-ASCII in runtime string literals

### Part I non-goals

Sphinx; doc every bind; Doxygen all headers; `PY_BIND` as universal export.

---

## Part II — Iface packages + symbol table

### Model

| Artifact | Is | Is not |
|----------|-----|--------|
| Header package | **lz4(ustar)** of public `.h` (default) | sysroot by itself; raw tar in image by default |
| Sym table | module + name + WAMR `sig` (+ optional `doc_key`) | help text / second ABI list |
| `doc_key` | `"py:…"` → Part I catalog | pasted docs |

**Harvest:** build-time scrape of `NativeSymbol[]` only (locked).

| Package | Kind | Who |
|---------|------|-----|
| `metal.guest` | headers | kernel v1 |
| `wasi.sysroot` | sysroot | later — never inside `metal.guest` |
| `mod.<name>` | headers | optional |
| `mod.t8_multimod_lib` | headers | **v1 seed** (not Doom) |

No first-file tar INDEX in v1 — small packs; optional RAM map after inflate.
Sym table = plain C array (not tar).

### Design lock

```c
typedef enum {
  PM_METAL_IFACE_PKG_HEADERS = 1,
  PM_METAL_IFACE_PKG_SYSROOT = 2
} pm_metal_iface_pkg_kind_t;

typedef struct {
  const char                *name;
  pm_metal_iface_pkg_kind_t  kind;
  const char                *version;
  const char                *abi_hash;
  uint32_t                   nfiles;
  uint32_t                   blob_len;
} pm_metal_iface_pkg_info_t;

typedef struct {
  const char *module;
  const char *name;
  const char *sig;
  uint8_t     class_;
  const char *doc_key;
} pm_metal_iface_sym_t;
```

```c
int32_t pm_metal_iface_pkg_count(void);
int32_t pm_metal_iface_pkg_at(uint32_t i, pm_metal_iface_pkg_info_t *out);
int32_t pm_metal_iface_pkg_register(
  const char *name, pm_metal_iface_pkg_kind_t kind,
  const char *version, const char *abi_hash,
  const uint8_t *blob, uint32_t blob_len, uint32_t uncompressed_len);
int32_t pm_metal_iface_file_count(const char *pkg);
int32_t pm_metal_iface_file_at(const char *pkg, uint32_t i,
                               char *path, uint32_t path_cap);
int32_t pm_metal_iface_file_open(const char *pkg, const char *path,
                                 const uint8_t **data, uint32_t *len);
int32_t pm_metal_iface_sym_count(void);
int32_t pm_metal_iface_sym_at(uint32_t i, pm_metal_iface_sym_t *out);
int32_t pm_metal_iface_sym_lookup(const char *module, const char *name,
                                  pm_metal_iface_sym_t *out);
```

Dual-ABI required. Reuse `util/tar` + `util/lz4`.  
`blob = lz4(ustar)`; `uncompressed_len` = ustar size. Raw ustar = debug opt-out only.

### Interface preview (LOCKED)

```text
iface / iface ls / iface ls metal.guest
iface cat metal.guest pymergetic/metal/fs/fs.h
iface sym [module [name]]
```

```python
import pymergetic.metal.iface as iface
iface.info(); iface.list(); iface.list("metal.guest")
iface.read("metal.guest", "pymergetic/metal/fs/fs.h")
s = iface.sym("pymergetic.metal.fs", "pm_metal_fs_open")
doc.lookup_key(s["doc_key"])  # when set
```

Methods: `info`, `list`, `read`, `sym`.

### Ownership (Part II)

| Phase | Owns |
|-------|------|
| **II-A** | `util/iface.h`, `iface.c`, dual-ABI |
| **II-B** | `scripts/gen_iface_syms.py`, build wire |
| **II-C** | `embed-iface.sh`, `metal.guest` lz4 embed |
| **II-D** | `iface_shell.c`, `iface_py_bind.c` |
| **II-E** | mod pkg register + t8 header pack |
| **II-F** | `docs/IFACE.md` + SOURCETREE blurb |
| **II-G** | `iface_doc_keys.txt` bridge (needs I-E) |

### Phases II-A … II-G

**II-A** — registry + empty/stub syms + dual-ABI.  
**II-B** — scrape NativeSymbol → `iface_syms.inc.c`; wire efi/bios.  
**II-C** — allowlist from `metal.h` + extras; ustar→lz4→embed.  
**II-D** — shell `iface` + `pymergetic.metal.iface`.  
**II-E** — `t8_multimod_lib.h` → `mod.t8_multimod_lib` (not Doom).  
**II-F** — prose + verify.  
**II-G** — seed `doc_key` e.g. fs open → `py:pymergetic.metal.fs.open`.

### Part II checklist

- [x] II-A … II-G
- [x] lz4 default; no wasi inside `metal.guest`
- [x] No hand-written second sig list

### Part II later / non-goals

`wasi.sysroot`; in-app C compiler; JIT; auto doc_key for all natives.  
Non-goals: C compiler in image; parse headers for sigs; DOC catalog (Part I).

---

## Part III — HTTP JSON + HTML UI

Starts after **I-E** and **II-D**. Same catalogs — no second store. Prefer
extend `mods/api/` routes (or `mods/httpd/`); re-read
ASGI APIs after post-scan (parallel work moves them).

### Stack (locked)

| Piece | Choice |
|-------|--------|
| Server | Microdot (ASGI) |
| Templates | **utemplate** — not Jinja |
| Data | `doc.*` + `iface.*` only |
| Style | One static CSS; no SPA |

### JSON API (locked)

| Method | Path |
|--------|------|
| `GET` | `/api/doc` — `?kind=` |
| `GET` | `/api/doc/{kind}/{key}` |
| `GET` | `/api/doc/key/{doc_key}` |
| `GET` | `/api/iface` |
| `GET` | `/api/iface/pkg` / `/api/iface/pkg/{name}` / `.../files` / `.../file/{path}` |
| `GET` | `/api/iface/sym` / `/api/iface/sym/{module}/{name}` |

### HTML pages (locked)

`/`, `/docs`, `/docs/{kind}/{key}`, `/iface`, `/iface/pkg/{name}`,
`/iface/pkg/{name}/file/{path}`, `/iface/sym`, `/iface/sym/{module}/{name}`.

### Phases

**III-H1** — JSON routes.  
**III-H2** — utemplate HTML + CSS.  
**III-H3** — short doc pointer (no Sphinx).

### Part III checklist

- [x] H1–H3; utemplate only; depends on I-E + II-D

### Part III non-goals

Jinja; SPA; auth; edit-over-HTTP; HTML inside C core.

---

## Part IV — Exec runbook

### §0 Gate + post-scan (every time)

Wait until parallel agent is done / efi build green.

```bash
cd /home/ladmin/Devel/os-sdk/packages/metal

git status --short -- \
  include/pymergetic/metal/util/doc.h \
  include/pymergetic/metal/util/iface.h \
  src/pymergetic/metal/util/doc.c \
  src/pymergetic/metal/util/iface.c \
  include/pymergetic/metal/shell/shell_cmd.h \
  src/pymergetic/metal/shell/shell/shell_cmd.c \
  src/pymergetic/metal/shell/shell/shell_py_bind.c \
  src/pymergetic/metal/shell/shell/shell_core_cmds.c \
  include/pymergetic/metal/py/py.h \
  src/pymergetic/metal/py/py_bind.c \
  include/pymergetic/metal/guest/mod/mod_lifecycle.h \
  src/pymergetic/metal/guest/mod/mod.c \
  scripts/gen_py_stubs.py \
  scripts/gen_iface_syms.py \
  scripts/iface_doc_keys.txt \
  scripts/build.d/port/efi/embed-iface.sh \
  mods/tests/t8_multimod_lib/

git diff --stat -- \
  include/pymergetic/metal/metal.h \
  src/efi/MetalPkg/Metal.inf \
  scripts/build.d/port/efi/default.sh \
  scripts/build.d/port/bios/default.sh \
  src/pymergetic/metal/py/py.c \
  src/pymergetic/metal/py/py_ctx.c \
  src/pymergetic/metal/guest/wasm/wasm.c \
  src/efi/MetalPkg/shell_cmds.lds \
  src/bios/BiosPkg/link.ld \
  src/bios/BiosPkg/link_i386.ld \
  src/pymergetic/metal/util/tar.c \
  src/pymergetic/metal/util/lz4.c

rg -n 'pm_metal_shell_cmd_register|mCmds|pm_metal_py_binds_install|pm_metal_py_pmcmd_install' \
  src/pymergetic/metal/shell src/pymergetic/metal/py --glob '*.c'
rg -n 'pm_metal_mod_register_func|pm_metal_mod_register_cmd' \
  include/pymergetic/metal/guest/mod src/pymergetic/metal/guest/mod
rg -n 'wasm_runtime_register_natives|NativeSymbol' \
  src/pymergetic/metal --glob '*.c' | head -40
rg -n 'metal_asgi_launcher|utemplate' mods/py --glob '*.py' | head -20
```

Exclusive files dirty from someone else → stop. Shared glue moved →
re-anchor (add next to `tar.c`/`lz4.c` / util includes / native_register
chain / pmcmd install).

Claim: `CLAIM exec` in [Agent claims](#agent-claims).

### Collision map (scan-time)

| Area | Risk | Action |
|------|------|--------|
| shell/py/mod doc targets | Low if clean | Edit after §0 |
| `metal.h`, `Metal.inf`, build sh, `py.c`, `wasm.c` | High | Surgical re-anchor |
| `tar.c` / `lz4.c` | Medium | Call APIs only |
| ASGI / microdot | Parallel | HTTP last |
| Dropbear/SSH | Unrelated | Ignore |

### S0 — Shell live enum (before DOC)

`mCmds[]` is static today. Add:

```c
uint32_t pm_metal_shell_cmd_count(void);
const pm_metal_shell_cmd_t *pm_metal_shell_cmd_at(uint32_t i);
const pm_metal_shell_cmd_t *pm_metal_shell_cmd_find(const char *name);
```

in `shell_cmd.h` / `shell_cmd.c`. DOC + pmcmd use these only.

### Exec order

```text
P → S0 → I-A … I-F → II-A … II-E → II-G → I-G/II-F prose → III-H*
```

### File-exact steps

**P** — git-tag `PM_METAL_VERSION` (Part P): `gen_metal_version` +
`version.h` include + efi/bios wire.  
**S0** — `shell_cmd.h/c`: count/at/find.  
**I-A** — create `util/doc.h`, `doc.c`; wire `metal.h`, `Metal.inf`, bios
sources, `wasm.c` natives.  
**I-B** — `sig`/`body`; help detail; live pmcmd (no linker-only).  
**I-C** — `py.h` / `py_bind.c` / `gen_py_stubs.py`.  
**I-D** — `mod_lifecycle.h` / `mod.c`.  
**I-E** — `doc_py_bind.c` (+ Metal.inf).  
**I-F** — seeds (fs open, mem, t0_hello, py demo).  
**II-A** — `util/iface.h`, `iface.c` + glue.  
**II-B** — `scripts/gen_iface_syms.py` + efi/bios wire.  
**II-C** — `scripts/build.d/port/efi/embed-iface.sh` lz4 `metal.guest`.  
**II-D** — `iface_shell.c`, `iface_py_bind.c`.  
**II-E** — `mods/tests/t8_multimod_lib/include/t8_multimod_lib.h` pack.  
**II-G** — `scripts/iface_doc_keys.txt` + merge.  
**Prose** — MICROPYTHON.md, `docs/IFACE.md`, SOURCETREE; greps; build efi.  
**III** — JSON then utemplate on ASGI launcher path.

### Verify

```bash
grep -rlE '#include\s*<(Uefi\.h|Library/|Protocol/|IndustryStandard/)' \
  src/pymergetic/metal --include=*.c --include=*.h
grep -rnP '"([^"\\]|\\.)*[→—–…‘’“”✓×°±]([^"\\]|\\.)*"' src --include=*.c
./scripts/build efi
```

### Done definition

- [x] §0 post-scan logged
- [x] P: version from `git describe` / tag
- [x] S0 + I-A…F + II-A…E + II-G
- [x] Live pmcmd sees guest cmds; lz4 packs; t8 readable
- [x] greps + efi build green (bios also green)
- [x] III H1–H3 (JSON + utemplate HTML + prose); live smoke `scripts/verify.d/port/efi/doc-iface-smoke.sh`

---

## Agent claims

| When | Agent | Event | Notes |
|------|-------|-------|-------|
| 2026-07-27 22:36 | cursor-agent (composer) | CLAIM exec | Implementing P, S0, I, II (+HTTP if time), sole agent, sequential Part IV |
| 2026-07-27 23:31 | cursor-agent (composer) | DONE exec | P, S0, I (I-A…I-G), II (II-A…II-G) landed + wired (efi/bios `default.sh`, `metal.h`, `Metal.inf`); `./scripts/build efi` and `./scripts/build bios x86_64` both green, zero new warnings (fixed a pre-existing `snprintf` missing-`<stdio.h>` in `py_bind.c` found along the way, plus two `/*`-in-comment `-Wcomment` typos in `mod_lifecycle.h`/`iface.h`). Part III landed too (JSON-only, no utemplate HTML): `/api/doc`, `/api/doc/<kind>/<key>`, `/api/doc/key/<doc_key>`, `/api/iface`, `/api/iface/pkg[/…]`, `/api/iface/sym[/…]` added to `mods/py/metal_asgi_launcher.py`, thin pass-through onto `pymergetic.metal.doc`/`.iface` (not runtime-tested against a live boot, only `py_compile`-checked). Prose: `docs/IFACE.md` (new), `docs/MICROPYTHON.md` (one Surfaces bullet), `docs/SOURCETREE.md` (two `Header ↔ .c map` rows + one paragraph), this file's status/checklists. **Post-landing fix (same claim):** `gen_py_stubs.py`'s DOC-field regex didn't handle C adjacent-string-literal concatenation (the normal way a long `summary`/`sig`/`body` wraps under ~100 cols in this tree) — it was silently dropping any row whose last DOC field was split across lines, which had gone unnoticed because nothing diffed the regenerated `typings/**` after the I-F/II-G seeds landed. Concretely this had erased `pymergetic.metal.doc` entirely, most of `pymergetic.metal.iface`, `fs.open` from `fs.pyi`, and `t0_hello` from `mod.pyi` (its `register_func_doc` call also wasn't matched by the old mod-func scan at all). Fixed the regex (`_DOC_FIELD`/`doc_field()` now join every adjacent string literal) and added the missing `pm_metal_mod_register_func_doc` + 5-field `SHELL_CMDS` row scans; reran `--check` (now idempotent) and rebuilt both efi/bios green. Lesson for the next agent touching this file: always `git diff typings/` (or eyeball the specific `.pyi` a new DOC seed should land in) after regenerating, not just check the exit code. |
| 2026-07-27 23:55 | cursor-agent (composer) | DONE III+smoke | Part III H2/H3: utemplate HTML (`mods/py/templates/` + `compile_templates.py` + vendored `mods/py/utemplate/`), `mods/www/doc.css`, HTML routes on `metal_asgi_launcher.py`, `iface.sym()` list overload, prose in IFACE.md / MICROPYTHON.md, live smoke `scripts/verify.d/port/efi/doc-iface-smoke.sh`. |
| 2026-07-28 00:05 | cursor-agent (composer) | DONE III+smoke | Live smoke green. Fixes along the way: (1) `pm_metal_py_bind_t` pad to 64 so aligned(16) section stride matches pointer math — DOC fields had made sizeof 56 and most PY_BINDs never installed (`ImportError: pymergetic.metal.fs`); (2) curated ESP `mods/py` stage (no microdot_src) so ESP cache does not drop stdlib.zip; (3) `utemplate.zip`/`templates.zip` + sys.path (ESP import_stat has no DIR); (4) list `?limit=` because sync `asgi conn_send` aborts large bodies. Smoke: `scripts/verify.d/port/efi/doc-iface-smoke.sh`. |

---

## Full acceptance checklist

**Prelude:** P git-tag version.  
**Docs:** I-A…I-G; shell≡pmcmd one string; dialect/ASCII.  
**Iface:** II-A…II-G; lz4 default; t8 seed; no second sig list.  
**HTTP:** III-H1…H3 after I-E + II-D.  
**Exec:** §0 + P + S0 + ordered steps + build green.
