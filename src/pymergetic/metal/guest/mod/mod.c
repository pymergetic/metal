/** @file
  Mod registry — on_load/on_unload; register funcs/cmds; cmd → process.
  See docs/MODS.md.
**/
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/ui/tab.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/py/py.h>

#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/slot/spin.h>

#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

typedef enum {
  MOD_EMPTY = 0,
  MOD_READY,
  MOD_RUNNING
} mod_state_t;

typedef struct {
  int32_t                   used;
  char                      name[64];
  pm_metal_wasm_mod_image_t img;
  mod_state_t               state;
  uint32_t                  open_tasks;
  pm_metal_mod_cap_t        cap; /* declared via set_capability() from on_load; default SINGLE */
  uint32_t                  fresh_open; /* live FRESH-mode instances of this mod right now */
  /* declared via set_about() from on_load; NULL (never declared) by
   * default. Heap, not inline: pm_metal_mod_about_t is ~2.7 KB (mostly
   * desc) and mMods[] below is a 128-entry static array — inlining it
   * would cost ~330 KB of BSS most mods never use. Allocated once on
   * first set_about() for this slot, contents overwritten on later
   * calls (own memory never freed+reallocated, size is fixed). */
  pm_metal_mod_about_t *about;
} mod_slot_t;

typedef struct {
  int32_t used;
  char    name[64];
  char    mod_name[64];
  char    export_name[64];
  void   *fn; /* wasm_function_inst_t */
  /* Doc catalog fields (docs/DOC_IFACE_PLAN.md Part I) — set only via
   * pm_metal_mod_register_func_doc; "" (never NULL) when unset. */
  char doc_summary[96];
  char doc_sig[64];
  char doc_body[128];
} mod_func_t;

typedef struct {
  int32_t used;
  char    name[64];
  char    mod_name[64];
  char    func_name[64];
  char    help[96];
  int32_t shell_registered;
} mod_cmd_t;

typedef struct {
  int32_t                   used;
  char                      mod_name[64];
  pm_metal_wasm_mod_image_t img;
} mod_fresh_t;

static mod_slot_t mMods[PM_METAL_MOD_MAX];
/*
 * Guards find-or-claim on mMods[] only (short, no await inside). Runners
 * are genuine parallel APs (StartupAllAPs) and MetalPickCpu round-robins
 * task creation across all of them (no more single-session-CPU affinity)
 * -- two mod loads landing on different APs at once used to race on which
 * free slot to claim, or read a slot mid-memset, corrupting a name/image
 * and eventually jumping through a garbage function pointer much later.
 */
static pm_metal_spin_t      mModsLock;
static mod_func_t           mFuncs[PM_METAL_MOD_FUNC_MAX];
static mod_cmd_t            mCmds[PM_METAL_MOD_CMD_MAX];
static pm_metal_shell_cmd_t mShellCmds[PM_METAL_MOD_CMD_MAX];
static mod_slot_t          *mConnecting;
/* Slot 0 unused so a raw index doubles as the handle (0 = invalid). */
static mod_fresh_t mFresh[PM_METAL_MOD_FRESH_MAX + 1];

static mod_slot_t *ModFind(const char *name)
{
  uint32_t    i;
  mod_slot_t *found;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  found = NULL;
  pm_metal_spin_lock(&mModsLock);
  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used && strcmp(mMods[i].name, name) == 0) {
      found = &mMods[i];
      break;
    }
  }
  pm_metal_spin_unlock(&mModsLock);

  return found;
}

static mod_slot_t *ModAlloc(const char *name)
{
  uint32_t    i;
  mod_slot_t *s;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  /* Single critical section for find-then-claim -- two separate
   * lock/unlock pairs (find, then re-scan-and-claim) would leave a gap
   * where two APs both see "not found" and both claim a slot for the
   * same name. */
  pm_metal_spin_lock(&mModsLock);

  s = NULL;
  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used && strcmp(mMods[i].name, name) == 0) {
      s = &mMods[i];
      break;
    }
  }

  if (s == NULL) {
    for (i = 0; i < PM_METAL_MOD_MAX; i++) {
      if (!mMods[i].used) {
        memset(&mMods[i], 0, sizeof(mMods[i]));
        mMods[i].used = 1;
        strncpy(mMods[i].name, name, sizeof(mMods[i].name) - 1);
        mMods[i].state = MOD_EMPTY;
        s              = &mMods[i];
        break;
      }
    }
  }

  pm_metal_spin_unlock(&mModsLock);
  return s;
}

static mod_func_t *FuncFind(const char *mod_name, const char *name)
{
  uint32_t i;

  if (mod_name == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
    if (mFuncs[i].used && strcmp(mFuncs[i].mod_name, mod_name) == 0 &&
        strcmp(mFuncs[i].name, name) == 0) {
      return &mFuncs[i];
    }
  }

  return NULL;
}

static mod_cmd_t *CmdFind(const char *name)
{
  uint32_t i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
    if (mCmds[i].used && strcmp(mCmds[i].name, name) == 0) {
      return &mCmds[i];
    }
  }

  return NULL;
}

static void ModClearFuncs(const char *mod_name)
{
  uint32_t i;

  for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
    if (mFuncs[i].used && strcmp(mFuncs[i].mod_name, mod_name) == 0) {
      memset(&mFuncs[i], 0, sizeof(mFuncs[i]));
    }
  }
}

/** Space-join argv[start..argc-1] into out (truncated to out_sz), or leave
 * out empty if there is nothing to join. Shared by ModShellCmdFn's bare
 * "<mod> args..." and the CoreRunCmd/CoreTabCmd "run/tab <mod> args..."
 * shell forms (that one lives in shell_core_cmds.c, a different mod --
 * same small loop, not worth a shared header for six lines). */
static void ModJoinArgs(char *out, uint32_t out_sz, int argc, char **argv, int start)
{
  uint32_t off;
  uint32_t n;
  int      i;

  if (out == NULL || out_sz == 0) {
    return;
  }

  off    = 0;
  out[0] = '\0';
  for (i = start; i < argc && argv[i] != NULL; i++) {
    n = strlen(argv[i]);
    if (off != 0) {
      if (off + 1 >= out_sz) {
        break;
      }

      out[off++] = ' ';
    }

    if (off + n >= out_sz) {
      n = out_sz - 1u - off;
    }

    memcpy(out + off, argv[i], n);
    off += n;
  }

  out[off] = '\0';
}

static void ModShellCmdFn(int argc, char **argv)
{
  const char *cmd;
  char        args[128];

  if (argv == NULL || argv[0] == NULL) {
    return;
  }

  cmd = argv[0];
  ModJoinArgs(args, (uint32_t)sizeof(args), argc, argv, 1);
  /* Bare "doom" from the shell == "run doom" — same AUTO instance choice as run/tab. */
  (void)pm_metal_mod_cmd_invoke(cmd,
                                PM_METAL_PROC_UI_FULLSCREEN,
                                pm_metal_ui_tab_active(),
                                PM_METAL_MOD_INSTANCE_AUTO,
                                PM_METAL_MOD_FLAG_NONE,
                                args[0] != '\0' ? args : NULL);
}

static int32_t ModSetCapabilityHost(pm_metal_mod_cap_t cap)
{
  if (mConnecting == NULL) {
    pm_metal_log("metal-mod: set_capability outside on_load");
    return -1;
  }

  mConnecting->cap = cap;
  pm_metal_logf("metal-mod: %s capability = %s",
                mConnecting->name,
                cap == PM_METAL_MOD_CAP_MULTI ? "multi" : "single");
  return 0;
}

const char *pm_metal_mod_author_role_name(pm_metal_mod_author_role_t role)
{
  switch (role) {
  case PM_METAL_MOD_AUTHOR_ROLE_MAINTAINER:
    return "maintainer";
  case PM_METAL_MOD_AUTHOR_ROLE_CONTRIBUTOR:
    return "contributor";
  default:
    return "author";
  }
}

/** NUL-terminate every fixed buffer in *about — defensive against a
 * guest that built the struct without terminating each field itself
 * (memcpy from guest memory copies whatever bytes are there). */
static void ModAboutSanitize(pm_metal_mod_about_t *about)
{
  uint32_t i;

  about->version[sizeof(about->version) - 1] = '\0';
  about->desc[sizeof(about->desc) - 1]       = '\0';
  about->url[sizeof(about->url) - 1]         = '\0';
  if (about->author_count > PM_METAL_MOD_AUTHOR_MAX) {
    about->author_count = PM_METAL_MOD_AUTHOR_MAX;
  }

  for (i = 0; i < about->author_count; i++) {
    about->authors[i].name[sizeof(about->authors[i].name) - 1]   = '\0';
    about->authors[i].email[sizeof(about->authors[i].email) - 1] = '\0';
  }
}

static int32_t ModSetAboutHost(const pm_metal_mod_about_t *about)
{
  if (mConnecting == NULL) {
    pm_metal_log("metal-mod: set_about outside on_load");
    return -1;
  }

  if (about == NULL) {
    return -1;
  }

  if (mConnecting->about == NULL) {
    mConnecting->about = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
      sizeof(*mConnecting->about), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (mConnecting->about == NULL) {
      pm_metal_log("metal-mod: set_about alloc failed");
      return -1;
    }
  }

  *mConnecting->about = *about;
  ModAboutSanitize(mConnecting->about);
  pm_metal_logf("metal-mod: %s about = %s (%u authors)",
                mConnecting->name,
                mConnecting->about->version,
                mConnecting->about->author_count);
  return 0;
}

int32_t pm_metal_mod_set_about(const pm_metal_mod_about_t *about)
{
  return ModSetAboutHost(about);
}

int32_t pm_metal_mod_about_get(const char *mod_name, pm_metal_mod_about_t *out)
{
  mod_slot_t *s;

  if (out == NULL) {
    return -1;
  }

  s = ModFind(mod_name);
  if (s == NULL) {
    return -1;
  }

  if (s->about == NULL) {
    memset(out, 0, sizeof(*out)); /* never declared — same "all-zero" contract as before */
    return 0;
  }

  *out = *s->about;
  return 0;
}

static int32_t ModRegisterFuncDocHost(
  const char *name, const char *export_name, const char *summary, const char *sig, const char *body)
{
  mod_func_t *f;
  void       *fn;
  uint32_t    i;

  if (mConnecting == NULL || name == NULL || name[0] == '\0' || export_name == NULL ||
      export_name[0] == '\0') {
    return -1;
  }

  fn = pm_metal_wasm_mod_image_lookup(&mConnecting->img, export_name);
  if (fn == NULL) {
    pm_metal_logf("metal-mod: register_func missing export '%s'", export_name);
    return -1;
  }

  f = FuncFind(mConnecting->name, name);
  if (f == NULL) {
    for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
      if (!mFuncs[i].used) {
        f = &mFuncs[i];
        memset(f, 0, sizeof(*f));
        f->used = 1;
        strncpy(f->name, name, sizeof(f->name) - 1);
        strncpy(f->mod_name, mConnecting->name, sizeof(f->mod_name) - 1);
        break;
      }
    }
  }

  if (f == NULL) {
    return -1;
  }

  strncpy(f->export_name, export_name, sizeof(f->export_name) - 1);
  f->fn = fn;
  if (summary != NULL) {
    strncpy(f->doc_summary, summary, sizeof(f->doc_summary) - 1);
  }
  if (sig != NULL) {
    strncpy(f->doc_sig, sig, sizeof(f->doc_sig) - 1);
  }
  if (body != NULL) {
    strncpy(f->doc_body, body, sizeof(f->doc_body) - 1);
  }
  pm_metal_logf("metal-mod: func '%s' -> export '%s' (%s)", name, export_name, mConnecting->name);
  return 0;
}

static int32_t ModRegisterFuncHost(const char *name, const char *export_name)
{
  return ModRegisterFuncDocHost(name, export_name, NULL, NULL, NULL);
}

static int32_t ModRegisterCmdHost(const char *cmd_name, const char *func_name, const char *help)
{
  mod_cmd_t            *c;
  mod_func_t           *f;
  uint32_t              i;
  pm_metal_shell_cmd_t *sc;

  if (mConnecting == NULL || cmd_name == NULL || cmd_name[0] == '\0' || func_name == NULL ||
      func_name[0] == '\0') {
    return -1;
  }

  f = FuncFind(mConnecting->name, func_name);
  if (f == NULL || f->fn == NULL) {
    pm_metal_logf("metal-mod: register_cmd unknown func '%s'", func_name);
    return -1;
  }

  c = CmdFind(cmd_name);
  if (c != NULL && strcmp(c->mod_name, mConnecting->name) != 0) {
    pm_metal_logf("metal-mod: cmd '%s' owned by other mod", cmd_name);
    return -1;
  }

  if (c == NULL) {
    for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
      if (!mCmds[i].used) {
        c  = &mCmds[i];
        sc = &mShellCmds[i];
        memset(c, 0, sizeof(*c));
        c->used = 1;
        strncpy(c->name, cmd_name, sizeof(c->name) - 1);
        strncpy(c->mod_name, mConnecting->name, sizeof(c->mod_name) - 1);
        if (help != NULL && help[0] != '\0') {
          strncpy(c->help, help, sizeof(c->help) - 1);
        } else {
          strncpy(c->help, "mod command", sizeof(c->help) - 1);
        }

        sc->name = c->name;
        sc->help = c->help;
        sc->fn   = ModShellCmdFn;
        pm_metal_shell_cmd_register(sc);
        c->shell_registered = 1;
        break;
      }
    }
  } else {
    strncpy(c->func_name, func_name, sizeof(c->func_name) - 1);
    if (help != NULL && help[0] != '\0') {
      strncpy(c->help, help, sizeof(c->help) - 1);
    }

    pm_metal_logf("metal-mod: cmd '%s' -> func '%s' (%s)", cmd_name, func_name, mConnecting->name);
    return 0;
  }

  if (c == NULL) {
    return -1;
  }

  strncpy(c->func_name, func_name, sizeof(c->func_name) - 1);
  pm_metal_logf("metal-mod: cmd '%s' -> func '%s' (%s)", cmd_name, func_name, mConnecting->name);
  return 0;
}

static int ModConnect(mod_slot_t *s)
{
  mod_slot_t *prev;
  int32_t     ret;
  int32_t     rc;

  if (s == NULL) {
    return -1;
  }

  /* Nested load from another mod's on_load (deps) — restore after. */
  prev        = mConnecting;
  mConnecting = s;
  ret         = 0;
  rc          = pm_metal_wasm_mod_image_call0(&s->img, "pm_metal_mod_on_load", &ret);
  mConnecting = prev;
  if (rc != 0) {
    pm_metal_logf("metal-mod: missing on_load in %s", s->name);
    return -1;
  }

  if (ret != 0) {
    pm_metal_logf("metal-mod: on_load %s failed (%d)", s->name, (int)ret);
    return -1;
  }

  return 0;
}

static int ModDisconnect(mod_slot_t *s)
{
  int32_t ret;
  int32_t rc;

  if (s == NULL || s->img.inst == NULL) {
    return 0;
  }

  ret = 0;
  rc  = pm_metal_wasm_mod_image_call0(&s->img, "pm_metal_mod_on_unload", &ret);
  if (rc != 0) {
    /* unload hook optional if image was torn down mid-flight */
    return 0;
  }

  if (ret != 0) {
    pm_metal_logf("metal-mod: on_unload %s rc=%d", s->name, (int)ret);
  }

  return 0;
}

static int ModEnsureReady(mod_slot_t *s)
{
  const uint8_t *bytes;
  uint32_t       len;
  uint8_t       *esp_owned;
  int32_t        rc;

  if (s == NULL) {
    return -1;
  }

  if (s->state == MOD_READY && s->img.module != NULL) {
    return 0;
  }

  if (s->state == MOD_RUNNING) {
    return -1;
  }

  if (s->img.module != NULL) {
    (void)ModDisconnect(s);
    pm_metal_wasm_mod_image_close(&s->img);
  }

  esp_owned = NULL;
  rc        = pm_metal_wasm_mod_fetch(s->name, &bytes, &len, &esp_owned);
  if (rc != 0) {
    return -1;
  }

  rc = pm_metal_wasm_mod_image_open(s->name, bytes, len, &s->img);
  if (esp_owned != NULL) {
    pm_metal_mem_free(esp_owned);
  }

  if (rc != 0) {
    s->state = MOD_EMPTY;
    return -1;
  }

  if (ModConnect(s) != 0) {
    pm_metal_wasm_mod_image_close(&s->img);
    s->state = MOD_EMPTY;
    return -1;
  }

  s->state      = MOD_READY;
  s->open_tasks = 0;
  return 0;
}

int pm_metal_mod_load(const char *name)
{
  mod_slot_t *s;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (!pm_metal_wasm_ready()) {
    return -1;
  }

  s = ModFind(name);
  if (s != NULL && s->state == MOD_RUNNING) {
    (void)pm_metal_py_autoload_for_mod(name);
    return 0;
  }

  if (s != NULL && s->state == MOD_READY && s->img.module != NULL) {
    (void)pm_metal_py_autoload_for_mod(name);
    return 0;
  }

  s = ModAlloc(name);
  if (s == NULL) {
    pm_metal_log("metal-mod: registry full");
    return -1;
  }

  if (ModEnsureReady(s) != 0) {
    if (s->state == MOD_EMPTY && s->img.module == NULL && CmdFind(name) == NULL) {
      /* on_load may have gotten as far as set_about() before some later
       * step in the same connect attempt failed — free it, else this
       * abandoned slot's heap about record leaks (slot itself is never
       * reused for a different name, but we still own this allocation). */
      pm_metal_mem_free(s->about);
      s->about = NULL;
      s->used  = 0;
    }

    return -1;
  }

  pm_metal_logf("metal-mod: ready %s", name);
  (void)pm_metal_py_autoload_for_mod(name);
  return 0;
}

int pm_metal_mod_unload(const char *name)
{
  mod_slot_t *s;
  uint32_t    i;

  s = ModFind(name);
  if (s == NULL) {
    return -1;
  }

  if (s->state == MOD_RUNNING || s->open_tasks != 0 || s->fresh_open != 0) {
    pm_metal_logf("metal-mod: unload refused %s (busy)", name);
    return -1;
  }

  if (pm_metal_process_active() && pm_metal_process_name(pm_metal_process_current()) != NULL &&
      strcmp(pm_metal_process_name(pm_metal_process_current()), name) == 0) {
    pm_metal_logf("metal-mod: unload refused %s (process live)", name);
    return -1;
  }

  (void)ModDisconnect(s);
  pm_metal_wasm_mod_image_close(&s->img);
  ModClearFuncs(name);
  for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
    if (mCmds[i].used && strcmp(mCmds[i].mod_name, name) == 0) {
      /* keep shell name; mark func stale until next load */
      mCmds[i].func_name[0] = '\0';
    }
  }

  s->state      = MOD_EMPTY;
  s->open_tasks = 0;
  pm_metal_logf("metal-mod: unloaded %s", name);
  return 0;
}

int pm_metal_mod_reset(const char *name)
{
  mod_slot_t *s;
  void       *module;
  uint8_t    *copy;
  char        nm[64];

  s = ModFind(name);
  if (s == NULL || s->img.module == NULL) {
    pm_metal_logf("metal-mod: reset refused %s (not loaded)", name != NULL ? name : "?");
    return -1;
  }

  if (s->state == MOD_RUNNING || s->open_tasks != 0 || s->fresh_open != 0) {
    pm_metal_logf("metal-mod: reset refused %s (busy)", name);
    return -1;
  }

  if (pm_metal_process_active() && pm_metal_process_name(pm_metal_process_current()) != NULL &&
      strcmp(pm_metal_process_name(pm_metal_process_current()), name) == 0) {
    pm_metal_logf("metal-mod: reset refused %s (process live)", name);
    return -1;
  }

  (void)ModDisconnect(s);
  module = s->img.module;
  copy   = s->img.copy;
  strncpy(nm, s->name, sizeof(nm) - 1);
  nm[sizeof(nm) - 1] = '\0';

  /* Drop instance 0's inst/exec_env only — keep the compiled module, no
   * refetch/recompile. Old func pointers were tied to the torn-down
   * instance, so they must be re-registered by rerunning on_load below. */
  pm_metal_wasm_mod_image_deinstantiate(&s->img);
  ModClearFuncs(nm);

  if (pm_metal_wasm_mod_image_instantiate(module, nm, &s->img) != 0) {
    pm_metal_logf("metal-mod: reset reinstantiate failed %s", nm);
    s->state = MOD_EMPTY;
    return -1;
  }

  s->img.copy = copy; /* image_instantiate() leaves copy NULL (non-owning) — restore ownership */
  if (ModConnect(s) != 0) {
    pm_metal_logf("metal-mod: reset reconnect failed %s", nm);
    s->state = MOD_EMPTY;
    return -1;
  }

  s->state      = MOD_READY;
  s->open_tasks = 0;
  pm_metal_logf("metal-mod: reset %s (kept compiled module)", nm);
  return 0;
}

/*
 * Teardown for a FRESH-mode instance owned by a process slot (see
 * pm_metal_process_set_owned_image). Decrements the mod's fresh-instance
 * count and, if auto_unload was requested for this run, best-effort
 * unloads the whole mod once nothing else needs it (silently refused —
 * mod just stays loaded — if still busy).
 */
void pm_metal_mod_on_fresh_instance_end(void *img, int32_t auto_unload)
{
  pm_metal_wasm_mod_image_t *pi;
  mod_slot_t                *s;
  char                       name[64];

  pi = (pm_metal_wasm_mod_image_t *)img;
  if (pi == NULL) {
    return;
  }

  strncpy(name, pi->name, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  pm_metal_wasm_mod_image_deinstantiate(pi);

  s = ModFind(name);
  if (s != NULL && s->fresh_open != 0) {
    s->fresh_open--;
  }

  if (auto_unload && name[0] != '\0') {
    (void)pm_metal_mod_unload(name); /* best-effort; refused silently if still busy */
  }
}

int pm_metal_mod_ready(const char *name)
{
  mod_slot_t *s;

  s = ModFind(name);
  return (s != NULL && (s->state == MOD_READY || s->state == MOD_RUNNING)) ? 1 : 0;
}

uint32_t pm_metal_mod_count(void)
{
  uint32_t i;
  uint32_t n;

  n = 0u;
  pm_metal_spin_lock(&mModsLock);
  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used) {
      n++;
    }
  }
  pm_metal_spin_unlock(&mModsLock);
  return n;
}

int32_t pm_metal_mod_at(uint32_t i, pm_metal_mod_info_t *out)
{
  uint32_t    seen;
  uint32_t    k;
  mod_slot_t *s;

  if (out == NULL) {
    return -1;
  }

  seen = 0u;
  s    = NULL;
  pm_metal_spin_lock(&mModsLock);
  for (k = 0; k < PM_METAL_MOD_MAX; k++) {
    if (!mMods[k].used) {
      continue;
    }
    if (seen == i) {
      s = &mMods[k];
      break;
    }
    seen++;
  }
  if (s == NULL) {
    pm_metal_spin_unlock(&mModsLock);
    return -1;
  }

  memset(out, 0, sizeof(*out));
  strncpy(out->name, s->name, sizeof(out->name) - 1u);
  out->ready      = (s->state == MOD_READY || s->state == MOD_RUNNING) ? 1 : 0;
  out->running    = (s->state == MOD_RUNNING) ? 1 : 0;
  out->cap        = (uint32_t)s->cap;
  out->open_tasks = s->open_tasks;
  out->fresh_open = s->fresh_open;
  out->has_about  = (s->about != NULL) ? 1 : 0;
  pm_metal_spin_unlock(&mModsLock);
  return 0;
}

int pm_metal_mod_cmd_exists(const char *cmd_name)
{
  return CmdFind(cmd_name) != NULL ? 1 : 0;
}

static mod_slot_t *ModFindByInst(void *inst)
{
  uint32_t    i;
  mod_slot_t *found;

  if (inst == NULL) {
    return NULL;
  }

  found = NULL;
  pm_metal_spin_lock(&mModsLock);
  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used && mMods[i].img.inst == inst) {
      found = &mMods[i];
      break;
    }
  }
  pm_metal_spin_unlock(&mModsLock);
  return found;
}

void pm_metal_mod_on_guest_coro_begin(void *module_inst)
{
  mod_slot_t *s;

  s = ModFindByInst(module_inst);
  if (s == NULL) {
    return;
  }

  s->open_tasks++;
}

void pm_metal_mod_on_guest_coro_end(void *module_inst)
{
  mod_slot_t *s;

  s = ModFindByInst(module_inst);
  if (s == NULL || s->open_tasks == 0) {
    return;
  }

  s->open_tasks--;
  if (s->open_tasks == 0 && s->state == MOD_RUNNING &&
      pm_metal_process_current() == PM_METAL_PROCESS_ID_INVALID &&
      pm_metal_process_pending() == PM_METAL_PROCESS_ID_INVALID) {
    s->state = MOD_READY;
  }
}

void pm_metal_mod_on_session_end(const char *name)
{
  mod_slot_t *s;

  if (name == NULL || name[0] == '\0') {
    return;
  }

  s = ModFind(name);
  if (s == NULL) {
    return;
  }

  /* Process redirect ended — mod stays READY with image + regs. */
  if (s->state == MOD_RUNNING && s->open_tasks == 0) {
    s->state = MOD_READY;
    pm_metal_logf("metal-mod: process end %s (still ready)", name);
  } else if (s->state == MOD_RUNNING) {
    s->state = MOD_READY;
    pm_metal_logf("metal-mod: process end %s (still ready)", name);
  }
}

int pm_metal_mod_func_exists(const char *mod_name, const char *func_name)
{
  if (mod_name == NULL || mod_name[0] == '\0' || func_name == NULL || func_name[0] == '\0') {
    return 0;
  }

  return FuncFind(mod_name, func_name) != NULL ? 1 : 0;
}

int pm_metal_mod_func_resolve(const char *mod_name, const char *func_name, pm_metal_mod_fn_t *out)
{
  mod_slot_t *s;
  mod_func_t *f;

  if (out == NULL || mod_name == NULL || mod_name[0] == '\0' || func_name == NULL ||
      func_name[0] == '\0') {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  if (pm_metal_mod_load(mod_name) != 0) {
    return -1;
  }

  s = ModFind(mod_name);
  f = FuncFind(mod_name, func_name);
  if (s == NULL || f == NULL || f->fn == NULL || s->img.inst == NULL || s->img.exec_env == NULL) {
    pm_metal_logf("metal-mod: resolve '%s.%s' failed", mod_name, func_name);
    return -1;
  }

  out->inst     = s->img.inst;
  out->exec_env = s->img.exec_env;
  out->fn       = f->fn;
  return 0;
}

int pm_metal_mod_func_resolve_on(const char        *mod_name,
                                 const char        *func_name,
                                 const void        *img_owner,
                                 pm_metal_mod_fn_t *out)
{
  const pm_metal_wasm_mod_image_t *img;
  mod_func_t                      *f;
  void                            *fn;

  if (img_owner == NULL) {
    return pm_metal_mod_func_resolve(mod_name, func_name, out);
  }

  if (out == NULL || mod_name == NULL || mod_name[0] == '\0' || func_name == NULL ||
      func_name[0] == '\0') {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  f = FuncFind(mod_name, func_name);
  if (f == NULL || f->export_name[0] == '\0') {
    pm_metal_logf("metal-mod: resolve_on '%s.%s' unknown func", mod_name, func_name);
    return -1;
  }

  img = (const pm_metal_wasm_mod_image_t *)img_owner;
  fn  = pm_metal_wasm_mod_image_lookup((pm_metal_wasm_mod_image_t *)img, f->export_name);
  if (fn == NULL || img->inst == NULL || img->exec_env == NULL) {
    pm_metal_logf("metal-mod: resolve_on '%s.%s' missing on target instance", mod_name, func_name);
    return -1;
  }

  out->inst     = img->inst;
  out->exec_env = img->exec_env;
  out->fn       = fn;
  return 0;
}

pm_metal_async_handle_t pm_metal_mod_fn_coro(const pm_metal_mod_fn_t *fn)
{
  if (fn == NULL || fn->inst == NULL || fn->exec_env == NULL || fn->fn == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_coro_create_guest(fn->inst, fn->exec_env, fn->fn, 0u);
}

static mod_fresh_t *ModFreshGet(pm_metal_mod_fresh_h_t h)
{
  if (h == PM_METAL_MOD_FRESH_H_INVALID || h > PM_METAL_MOD_FRESH_MAX) {
    return NULL;
  }

  if (!mFresh[h].used) {
    return NULL;
  }

  return &mFresh[h];
}

pm_metal_mod_fresh_h_t pm_metal_mod_fresh_open(const char *mod_name)
{
  mod_slot_t  *s;
  uint32_t     i;
  mod_fresh_t *slot;

  if (mod_name == NULL || mod_name[0] == '\0') {
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  if (pm_metal_mod_load(mod_name) != 0) {
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  s = ModFind(mod_name);
  if (s == NULL || s->img.module == NULL) {
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  /* Same guard fn_process(FRESH) already applies via ModResolveUseFresh. */
  if (s->cap == PM_METAL_MOD_CAP_SINGLE) {
    pm_metal_logf("metal-mod: fresh_open refused for '%s' (capability=single)", s->name);
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  slot = NULL;
  for (i = 1; i <= PM_METAL_MOD_FRESH_MAX; i++) {
    if (!mFresh[i].used) {
      slot = &mFresh[i];
      break;
    }
  }

  if (slot == NULL) {
    pm_metal_log("metal-mod: fresh_open: handle table full");
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  memset(slot, 0, sizeof(*slot));
  if (pm_metal_wasm_mod_image_instantiate(s->img.module, s->name, &slot->img) != 0) {
    pm_metal_logf("metal-mod: fresh_open: instantiate failed for %s", s->name);
    return PM_METAL_MOD_FRESH_H_INVALID;
  }

  strncpy(slot->mod_name, s->name, sizeof(slot->mod_name) - 1);
  slot->used = 1;
  s->fresh_open++;
  return (pm_metal_mod_fresh_h_t)(slot - mFresh);
}

int pm_metal_mod_fresh_resolve(pm_metal_mod_fresh_h_t h,
                               const char            *func_name,
                               pm_metal_mod_fn_t     *out)
{
  mod_fresh_t *slot;

  slot = ModFreshGet(h);
  if (slot == NULL) {
    return -1;
  }

  return pm_metal_mod_func_resolve_on(slot->mod_name, func_name, &slot->img, out);
}

void pm_metal_mod_fresh_close(pm_metal_mod_fresh_h_t h)
{
  mod_fresh_t *slot;
  mod_slot_t  *s;

  slot = ModFreshGet(h);
  if (slot == NULL) {
    return;
  }

  pm_metal_wasm_mod_image_deinstantiate(&slot->img);
  s = ModFind(slot->mod_name);
  if (s != NULL && s->fresh_open != 0) {
    s->fresh_open--;
  }

  memset(slot, 0, sizeof(*slot));
}

int pm_metal_mod_cmd_resolve(const char *cmd_name, pm_metal_mod_cmd_t *out)
{
  mod_cmd_t  *c;
  mod_func_t *f;
  const char *mod_name;

  if (out == NULL || cmd_name == NULL || cmd_name[0] == '\0') {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  c        = CmdFind(cmd_name);
  mod_name = (c != NULL) ? c->mod_name : cmd_name;
  if (pm_metal_mod_load(mod_name) != 0) {
    return -1;
  }

  c = CmdFind(cmd_name);
  if (c == NULL) {
    pm_metal_logf("metal-mod: no cmd '%s' after load", cmd_name);
    return -1;
  }

  if (pm_metal_mod_func_resolve(c->mod_name, c->func_name, &out->fn) != 0) {
    return -1;
  }

  strncpy(out->name, c->name, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  strncpy(out->mod_name, c->mod_name, sizeof(out->mod_name) - 1);
  out->mod_name[sizeof(out->mod_name) - 1] = '\0';

  f = FuncFind(c->mod_name, c->func_name);
  if (f != NULL) {
    strncpy(out->export_name, f->export_name, sizeof(out->export_name) - 1);
    out->export_name[sizeof(out->export_name) - 1] = '\0';
  }

  return 0;
}

/* AUTO/SHARED/FRESH -> concrete use_fresh bool, honoring the mod's
 * declared capability. -1 (and logs) if the caller's forced choice is
 * incompatible with that capability. */
static int32_t ModResolveUseFresh(const mod_slot_t *s, pm_metal_mod_instance_t mode)
{
  switch (mode) {
  case PM_METAL_MOD_INSTANCE_SHARED:
    return 0;

  case PM_METAL_MOD_INSTANCE_FRESH:
    if (s->cap == PM_METAL_MOD_CAP_SINGLE) {
      pm_metal_logf("metal-mod: fresh instance refused for '%s' (capability=single)", s->name);
      return -1;
    }

    return 1;

  case PM_METAL_MOD_INSTANCE_AUTO:
  default:
    return (s->cap == PM_METAL_MOD_CAP_MULTI) ? 1 : 0;
  }
}

int pm_metal_mod_fn_process(const pm_metal_mod_cmd_t  *cmd,
                            const char                *proc_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab,
                            pm_metal_mod_instance_t    instance_mode,
                            uint32_t                   flags,
                            const char                *args)
{
  mod_slot_t               *s;
  pm_metal_async_handle_t   coro;
  pm_metal_async_handle_t   task_h;
  pm_metal_process_id_t     pid;
  pm_metal_process_id_t     parent;
  const char               *name;
  const pm_metal_mod_fn_t  *fn;
  pm_metal_mod_fn_t         fresh_fn;
  pm_metal_wasm_mod_image_t proc_img;
  int32_t                   have_proc_img;
  int32_t                   use_fresh;
  int32_t                   auto_unload;
  int32_t                   rc;

  if (cmd == NULL) {
    return -1;
  }

  fn   = &cmd->fn;
  name = (proc_name != NULL && proc_name[0] != '\0') ? proc_name : cmd->name;
  if (fn->inst == NULL || fn->exec_env == NULL || fn->fn == NULL || name == NULL ||
      name[0] == '\0') {
    return -1;
  }

  if (pm_metal_process_pending() != PM_METAL_PROCESS_ID_INVALID) {
    pm_metal_log("metal-mod: fn_process refused (reserve pending)");
    return -1;
  }

  s = (cmd->mod_name[0] != '\0') ? ModFind(cmd->mod_name) : NULL;
  if (s == NULL) {
    s = ModFindByInst(fn->inst);
  }

  if (s == NULL) {
    pm_metal_log("metal-mod: fn_process: unknown mod instance");
    return -1;
  }

  use_fresh = ModResolveUseFresh(s, instance_mode);
  if (use_fresh < 0) {
    return -1;
  }

  auto_unload   = (flags & PM_METAL_MOD_FLAG_AUTO_UNLOAD) ? 1 : 0;
  have_proc_img = 0;
  memset(&proc_img, 0, sizeof(proc_img));

  if (use_fresh) {
    if (cmd->export_name[0] == '\0') {
      pm_metal_logf("metal-mod: fn_process: no export_name for fresh instance (%s)", s->name);
      return -1;
    }

    if (pm_metal_wasm_mod_image_instantiate(s->img.module, s->name, &proc_img) != 0) {
      pm_metal_logf("metal-mod: fresh instance failed for %s", s->name);
      return -1;
    }

    fresh_fn.fn = pm_metal_wasm_mod_image_lookup(&proc_img, cmd->export_name);
    if (fresh_fn.fn == NULL) {
      pm_metal_wasm_mod_image_deinstantiate(&proc_img);
      pm_metal_logf(
        "metal-mod: fresh instance missing export '%s' (%s)", cmd->export_name, s->name);
      return -1;
    }

    fresh_fn.inst     = proc_img.inst;
    fresh_fn.exec_env = proc_img.exec_env;
    fn                = &fresh_fn;
    have_proc_img     = 1;
    s->fresh_open++;
  }

  parent = pm_metal_process_current();
  pm_metal_process_set_spawn_hint(ui_kind, tab);
  pid = pm_metal_process_reserve(name, ui_kind, tab, args);
  pm_metal_process_clear_spawn_hint();
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    if (have_proc_img) {
      pm_metal_wasm_mod_image_deinstantiate(&proc_img);
      s->fresh_open--;
    }

    return -1;
  }

  /* Process slot owns proc_img now — reap()/release() deinstantiate it. */
  if (have_proc_img) {
    pm_metal_process_set_owned_image(pid, &proc_img, auto_unload);
  }

  coro = pm_metal_mod_fn_coro(fn);
  if (coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_process_release(pid);
    return -1;
  }

  if (pm_metal_async_session_active()) {
    /* Subprocess: child process-root task under live session. */
    pm_metal_process_stamp_begin(pid);
    task_h = pm_metal_async_create_task(coro);
    pm_metal_process_stamp_end();
    if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_async_coro_close(coro);
      pm_metal_process_release(pid);
      return -1;
    }

    pm_metal_process_commit_child(pid, task_h);
    s->state = MOD_RUNNING;
    pm_metal_logf("metal-mod: subprocess '%s' under %u (mod '%s')%s",
                  name,
                  (unsigned)parent,
                  s->name,
                  have_proc_img ? " [instance]" : "");
    return 0;
  }

  /* Top-level: stdout for startup pump + live. */
  pm_metal_wasm_set_stdout_tab(tab);
  rc = pm_metal_wasm_fn_start_async(
    s->img.module, fn->inst, fn->exec_env, fn->fn, coro, s->name, NULL, (uint32_t)pid);
  if (rc != 0) {
    pm_metal_process_release(pid);
    return rc;
  }

  if (!pm_metal_async_session_active()) {
    pm_metal_process_release(pid);
    s->state = MOD_READY;
    return 0;
  }

  pm_metal_process_bind_root_task(pid, pm_metal_async_session_root_task());
  s->state = MOD_RUNNING;
  pm_metal_logf(
    "metal-mod: process '%s' (mod '%s')%s", name, s->name, have_proc_img ? " [instance]" : "");
  return 0;
}

#ifndef PM_METAL_MOD_FN_H_SLOTS
#define PM_METAL_MOD_FN_H_SLOTS PM_METAL_MOD_FN_H_MAX
#endif

/* Guest handles store cmd shape (fn + optional name). */
static pm_metal_mod_cmd_t mFnHandles[PM_METAL_MOD_FN_H_SLOTS + 1];

static pm_metal_mod_fn_h_t ModFnHandleAlloc(const pm_metal_mod_cmd_t *cmd)
{
  uint32_t i;

  for (i = 1; i <= PM_METAL_MOD_FN_H_SLOTS; i++) {
    if (mFnHandles[i].fn.fn == NULL) {
      mFnHandles[i] = *cmd;
      return (pm_metal_mod_fn_h_t)i;
    }
  }

  return PM_METAL_MOD_FN_H_INVALID;
}

static pm_metal_mod_cmd_t *ModFnHandleGet(pm_metal_mod_fn_h_t h)
{
  if (h == PM_METAL_MOD_FN_H_INVALID || h > PM_METAL_MOD_FN_H_SLOTS) {
    return NULL;
  }

  if (mFnHandles[h].fn.fn == NULL) {
    return NULL;
  }

  return &mFnHandles[h];
}

int pm_metal_mod_cmd_invoke(const char                *cmd_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab,
                            pm_metal_mod_instance_t    instance_mode,
                            uint32_t                   flags,
                            const char                *args)
{
  pm_metal_mod_cmd_t cmd;

  if (pm_metal_mod_cmd_resolve(cmd_name, &cmd) != 0) {
    return -1;
  }

  return pm_metal_mod_fn_process(&cmd, NULL, ui_kind, tab, instance_mode, flags, args);
}

int32_t pm_metal_mod_register_func(const char *name, const char *export_name)
{
  return ModRegisterFuncHost(name, export_name);
}

int32_t pm_metal_mod_register_func_doc(
  const char *name, const char *export_name, const char *summary, const char *sig, const char *body)
{
  return ModRegisterFuncDocHost(name, export_name, summary, sig, body);
}

uint32_t pm_metal_mod_func_doc_count(void)
{
  uint32_t i;
  uint32_t n;

  n = 0;
  for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
    if (mFuncs[i].used) {
      n++;
    }
  }

  return n;
}

int32_t pm_metal_mod_func_doc_at(uint32_t     i,
                                 const char **mod_name,
                                 const char **func_name,
                                 const char **summary,
                                 const char **sig,
                                 const char **body)
{
  uint32_t j;
  uint32_t seen;

  if (mod_name == NULL || func_name == NULL || summary == NULL || sig == NULL || body == NULL) {
    return -1;
  }

  seen = 0;
  for (j = 0; j < PM_METAL_MOD_FUNC_MAX; j++) {
    if (!mFuncs[j].used) {
      continue;
    }

    if (seen == i) {
      *mod_name  = mFuncs[j].mod_name;
      *func_name = mFuncs[j].name;
      *summary   = mFuncs[j].doc_summary;
      *sig       = mFuncs[j].doc_sig;
      *body      = mFuncs[j].doc_body;
      return 0;
    }

    seen++;
  }

  return -1;
}

int32_t pm_metal_mod_func_doc_get(const char  *mod_name,
                                  const char  *func_name,
                                  const char **summary,
                                  const char **sig,
                                  const char **body)
{
  mod_func_t *f;

  if (summary == NULL || sig == NULL || body == NULL) {
    return -1;
  }

  f = FuncFind(mod_name, func_name);
  if (f == NULL) {
    return -1;
  }

  *summary = f->doc_summary;
  *sig     = f->doc_sig;
  *body    = f->doc_body;
  return 0;
}

int32_t pm_metal_mod_register_cmd(const char *cmd_name, const char *func_name, const char *help)
{
  return ModRegisterCmdHost(cmd_name, func_name, help);
}

static int32_t pm_metal_mod_load_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_load(name);
}

static int32_t pm_metal_mod_unload_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_unload(name);
}

static int32_t pm_metal_mod_ready_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_ready(name);
}

static int32_t pm_metal_mod_reset_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_reset(name);
}

static int32_t pm_metal_mod_set_capability_native(wasm_exec_env_t exec_env, uint32_t cap)
{
  (void)exec_env;
  return ModSetCapabilityHost((pm_metal_mod_cap_t)cap);
}

static int32_t pm_metal_mod_set_about_native(wasm_exec_env_t exec_env, uint32_t about)
{
  wasm_module_inst_t    inst;
  void                 *native;
  pm_metal_mod_about_t *tmp;
  int32_t               rc;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, about, sizeof(*tmp))) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, about);
  if (native == NULL) {
    return -1;
  }

  /* pm_metal_mod_about_t is ~2.7 KB (mostly desc) — a host heap temp, not
   * a stack local, same reasoning as coro step frames (see AGENTS.md). */
  tmp = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
    sizeof(*tmp), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (tmp == NULL) {
    return -1;
  }

  memcpy(tmp, native, sizeof(*tmp));
  rc = ModSetAboutHost(tmp);
  pm_metal_mem_free(tmp);
  return rc;
}

static int32_t pm_metal_mod_about_get_native(wasm_exec_env_t exec_env,
                                             const char     *mod_name,
                                             uint32_t        out)
{
  wasm_module_inst_t    inst;
  void                 *native;
  pm_metal_mod_about_t *tmp;
  int32_t               rc;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(*tmp))) {
    return -1;
  }

  tmp = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
    sizeof(*tmp), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (tmp == NULL) {
    return -1;
  }

  if (pm_metal_mod_about_get(mod_name, tmp) != 0) {
    pm_metal_mem_free(tmp);
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    pm_metal_mem_free(tmp);
    return -1;
  }

  memcpy(native, tmp, sizeof(*tmp));
  rc = 0;
  pm_metal_mem_free(tmp);
  return rc;
}

static int32_t pm_metal_mod_cmd_exists_native(wasm_exec_env_t exec_env, const char *cmd_name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_cmd_exists(cmd_name);
}

static int32_t pm_metal_mod_cmd_invoke_native(wasm_exec_env_t exec_env,
                                              const char     *cmd_name,
                                              uint32_t        ui_kind,
                                              uint32_t        tab,
                                              uint32_t        instance_mode,
                                              uint32_t        flags,
                                              const char     *args)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_cmd_invoke(cmd_name,
                                          (pm_metal_process_ui_kind_t)ui_kind,
                                          (pm_metal_ui_handle_t)tab,
                                          (pm_metal_mod_instance_t)instance_mode,
                                          flags,
                                          args);
}

static uint32_t pm_metal_mod_func_resolve_native(wasm_exec_env_t exec_env,
                                                 const char     *mod_name,
                                                 const char     *func_name)
{
  pm_metal_mod_cmd_t cmd;
  mod_func_t        *f;

  (void)exec_env;
  memset(&cmd, 0, sizeof(cmd));
  if (pm_metal_mod_func_resolve(mod_name, func_name, &cmd.fn) != 0) {
    return PM_METAL_MOD_FN_H_INVALID;
  }

  /* Carry mod/export name so fn_process(FRESH) can re-resolve. */
  if (mod_name != NULL) {
    strncpy(cmd.mod_name, mod_name, sizeof(cmd.mod_name) - 1);
    strncpy(cmd.name, mod_name, sizeof(cmd.name) - 1);
  }

  f = FuncFind(mod_name, func_name);
  if (f != NULL) {
    strncpy(cmd.export_name, f->export_name, sizeof(cmd.export_name) - 1);
  }

  return (uint32_t)ModFnHandleAlloc(&cmd);
}

static uint32_t pm_metal_mod_cmd_resolve_native(wasm_exec_env_t exec_env, const char *cmd_name)
{
  pm_metal_mod_cmd_t cmd;

  (void)exec_env;
  if (pm_metal_mod_cmd_resolve(cmd_name, &cmd) != 0) {
    return PM_METAL_MOD_FN_H_INVALID;
  }

  return (uint32_t)ModFnHandleAlloc(&cmd);
}

static uint32_t pm_metal_mod_fresh_open_native(wasm_exec_env_t exec_env, const char *mod_name)
{
  (void)exec_env;
  return (uint32_t)pm_metal_mod_fresh_open(mod_name);
}

static uint32_t pm_metal_mod_fresh_resolve_native(wasm_exec_env_t exec_env,
                                                  uint32_t        fresh_h,
                                                  const char     *func_name)
{
  pm_metal_mod_cmd_t cmd;
  mod_fresh_t       *slot;
  mod_func_t        *f;

  (void)exec_env;
  memset(&cmd, 0, sizeof(cmd));
  if (pm_metal_mod_fresh_resolve((pm_metal_mod_fresh_h_t)fresh_h, func_name, &cmd.fn) != 0) {
    return PM_METAL_MOD_FN_H_INVALID;
  }

  /* Carry mod name + export name, same as pm_metal_mod_func_resolve_native. */
  slot = ModFreshGet((pm_metal_mod_fresh_h_t)fresh_h);
  if (slot != NULL) {
    strncpy(cmd.mod_name, slot->mod_name, sizeof(cmd.mod_name) - 1);
    strncpy(cmd.name, slot->mod_name, sizeof(cmd.name) - 1);
    f = FuncFind(slot->mod_name, func_name);
    if (f != NULL) {
      strncpy(cmd.export_name, f->export_name, sizeof(cmd.export_name) - 1);
    }
  }

  return (uint32_t)ModFnHandleAlloc(&cmd);
}

static int32_t pm_metal_mod_fresh_close_native(wasm_exec_env_t exec_env, uint32_t fresh_h)
{
  (void)exec_env;
  pm_metal_mod_fresh_close((pm_metal_mod_fresh_h_t)fresh_h);
  return 0;
}

static uint32_t pm_metal_mod_fn_coro_native(wasm_exec_env_t exec_env, uint32_t fn_h)
{
  pm_metal_mod_cmd_t *cmd;

  (void)exec_env;
  cmd = ModFnHandleGet((pm_metal_mod_fn_h_t)fn_h);
  if (cmd == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return (uint32_t)pm_metal_mod_fn_coro(&cmd->fn);
}

static int32_t pm_metal_mod_fn_process_native(wasm_exec_env_t exec_env,
                                              uint32_t        fn_h,
                                              const char     *proc_name,
                                              uint32_t        ui_kind,
                                              uint32_t        tab,
                                              uint32_t        instance_mode,
                                              uint32_t        flags,
                                              const char     *args)
{
  pm_metal_mod_cmd_t *cmd;

  (void)exec_env;
  cmd = ModFnHandleGet((pm_metal_mod_fn_h_t)fn_h);
  if (cmd == NULL) {
    return -1;
  }

  return (int32_t)pm_metal_mod_fn_process(cmd,
                                          proc_name,
                                          (pm_metal_process_ui_kind_t)ui_kind,
                                          (pm_metal_ui_handle_t)tab,
                                          (pm_metal_mod_instance_t)instance_mode,
                                          flags,
                                          args);
}

static int32_t pm_metal_mod_func_exists_native(wasm_exec_env_t exec_env,
                                               const char     *mod_name,
                                               const char     *func_name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_func_exists(mod_name, func_name);
}

static int32_t pm_metal_mod_register_func_native(wasm_exec_env_t exec_env,
                                                 const char     *name,
                                                 const char     *export_name)
{
  (void)exec_env;
  return pm_metal_mod_register_func(name, export_name);
}

static int32_t pm_metal_mod_register_func_doc_native(wasm_exec_env_t exec_env,
                                                     const char     *name,
                                                     const char     *export_name,
                                                     const char     *summary,
                                                     const char     *sig,
                                                     const char     *body)
{
  (void)exec_env;
  return pm_metal_mod_register_func_doc(name, export_name, summary, sig, body);
}

static int32_t pm_metal_mod_register_cmd_native(wasm_exec_env_t exec_env,
                                                const char     *cmd_name,
                                                const char     *func_name,
                                                const char     *help)
{
  (void)exec_env;
  return pm_metal_mod_register_cmd(cmd_name, func_name, help);
}

static NativeSymbol g_pm_metal_mod_native_symbols[] = {
  { "pm_metal_mod_load", (void *)pm_metal_mod_load_native, "($)i", NULL },
  { "pm_metal_mod_unload", (void *)pm_metal_mod_unload_native, "($)i", NULL },
  { "pm_metal_mod_ready", (void *)pm_metal_mod_ready_native, "($)i", NULL },
  { "pm_metal_mod_reset", (void *)pm_metal_mod_reset_native, "($)i", NULL },
  { "pm_metal_mod_set_capability", (void *)pm_metal_mod_set_capability_native, "(i)i", NULL },
  { "pm_metal_mod_set_about", (void *)pm_metal_mod_set_about_native, "(i)i", NULL },
  { "pm_metal_mod_about_get", (void *)pm_metal_mod_about_get_native, "($i)i", NULL },
  { "pm_metal_mod_cmd_exists", (void *)pm_metal_mod_cmd_exists_native, "($)i", NULL },
  { "pm_metal_mod_cmd_invoke", (void *)pm_metal_mod_cmd_invoke_native, "($iiii$)i", NULL },
  { "pm_metal_mod_func_resolve", (void *)pm_metal_mod_func_resolve_native, "($$)i", NULL },
  { "pm_metal_mod_cmd_resolve", (void *)pm_metal_mod_cmd_resolve_native, "($)i", NULL },
  { "pm_metal_mod_fresh_open", (void *)pm_metal_mod_fresh_open_native, "($)i", NULL },
  { "pm_metal_mod_fresh_resolve", (void *)pm_metal_mod_fresh_resolve_native, "(i$)i", NULL },
  { "pm_metal_mod_fresh_close", (void *)pm_metal_mod_fresh_close_native, "(i)i", NULL },
  { "pm_metal_mod_fn_coro", (void *)pm_metal_mod_fn_coro_native, "(i)i", NULL },
  { "pm_metal_mod_fn_process", (void *)pm_metal_mod_fn_process_native, "(i$iiii$)i", NULL },
  { "pm_metal_mod_func_exists", (void *)pm_metal_mod_func_exists_native, "($$)i", NULL },
  { "pm_metal_mod_register_func", (void *)pm_metal_mod_register_func_native, "($$)i", NULL },
  { "pm_metal_mod_register_func_doc",
    (void *)pm_metal_mod_register_func_doc_native,
    "($$$$$)i",
    NULL },
  { "pm_metal_mod_register_cmd", (void *)pm_metal_mod_register_cmd_native, "($$$)i", NULL },
};

int pm_metal_mod_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_MOD_WASI_MODULE,
                                     g_pm_metal_mod_native_symbols,
                                     sizeof(g_pm_metal_mod_native_symbols) /
                                       sizeof(g_pm_metal_mod_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
