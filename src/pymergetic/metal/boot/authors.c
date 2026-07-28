/** @file
  Metal's own "about" record (impl: common) + the `about` shell command +
  pm_metal_about_get() (kernel or a mod, by name, one entry point).

  `about` with no argument prints the kernel's own version/desc/authors
  from the static table below; `about <mod>` prints that mod's declared
  pm_metal_mod_about_t instead (guest/mod/mod.c's registry — 0 authors if
  that mod never called pm_metal_mod_set_about()). Same command either
  way, no reserved mod name, no fake registry row for "the kernel" —
  pm_metal_about_get() below is that exact branch, factored out so a
  caller (host or wasm guest) gets one function instead of two.
**/
#include <pymergetic/metal/boot/authors.h>
#include <pymergetic/metal/guest/mod/mod_lifecycle.h> /* pm_metal_mod_about_get, author_role_name */
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/version.h>

#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

static const pm_metal_mod_about_t g_pm_metal_kernel_about = {
  .version      = PM_METAL_VERSION,
  .desc         = "Metal. Wasm. Async REPL. Minimalism first.",
  .url          = "https://github.com/pymergetic/metal",
  .author_count = 1,
  .authors      = {
    { "Rouven Raudzus", "raudzus@pymergetic.com", PM_METAL_MOD_AUTHOR_ROLE_AUTHOR },
  },
};

const pm_metal_mod_about_t *pm_metal_kernel_about(void)
{
  return &g_pm_metal_kernel_about;
}

int32_t pm_metal_about_get(const char *name, pm_metal_mod_about_t *out)
{
  if (out == NULL) {
    return -1;
  }

  if (name == NULL || name[0] == '\0') {
    *out = g_pm_metal_kernel_about;
    return 0;
  }

  return pm_metal_mod_about_get(name, out);
}

#if !defined(__wasm__)

static void AboutPrint(const char *label, const pm_metal_mod_about_t *about)
{
  char     line[160];
  uint32_t i;

  snprintf(line,
           sizeof(line),
           "about: %s %s",
           label,
           (about->version[0] != '\0') ? about->version : "(no version declared)");
  pm_metal_shell_out(line);

  if (about->desc[0] != '\0') {
    pm_metal_shell_out_lines(about->desc); /* desc may be multi-line ('\n'-separated) */
  }

  if (about->url[0] != '\0') {
    snprintf(line, sizeof(line), "  %s", about->url);
    pm_metal_shell_out(line);
  }

  for (i = 0; i < about->author_count; i++) {
    snprintf(line,
             sizeof(line),
             "  %s <%s>  (%s)",
             about->authors[i].name,
             about->authors[i].email,
             pm_metal_mod_author_role_name(about->authors[i].role));
    pm_metal_shell_out(line);
  }

  if (about->author_count == 0u) {
    pm_metal_shell_out("  (no authorship declared)");
  }
}

static void AboutShellCmd(int argc, char **argv)
{
  const char           *mod_name;
  pm_metal_mod_about_t *about;
  char                  line[96];

  mod_name = (argc >= 2) ? argv[1] : NULL;

  /* Heap temp, not a stack local — pm_metal_mod_about_t is ~2.7 KB
   * (mostly desc), see mod_types.h. */
  about = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
    sizeof(*about), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (about == NULL) {
    pm_metal_shell_out("about: out of memory");
    return;
  }

  if (pm_metal_about_get(mod_name, about) != 0) {
    snprintf(line, sizeof(line), "about: %s: not loaded/found", mod_name);
    pm_metal_shell_out(line);
    pm_metal_mem_free(about);
    return;
  }

  AboutPrint((mod_name != NULL) ? mod_name : "metal", about);
  pm_metal_mem_free(about);
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_about,
                   "about",
                   "about [mod]       version, desc, url, authors (metal or loaded mod)",
                   AboutShellCmd);

static int32_t pm_metal_about_get_native(wasm_exec_env_t exec_env, const char *name, uint32_t out)
{
  wasm_module_inst_t    inst;
  void                 *native;
  pm_metal_mod_about_t *tmp;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(*tmp))) {
    return -1;
  }

  tmp = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
    sizeof(*tmp), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (tmp == NULL) {
    return -1;
  }

  if (pm_metal_about_get(name, tmp) != 0) {
    pm_metal_mem_free(tmp);
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    pm_metal_mem_free(tmp);
    return -1;
  }

  memcpy(native, tmp, sizeof(*tmp));
  pm_metal_mem_free(tmp);
  return 0;
}

int32_t pm_metal_mod_set_about_kernel(void)
{
  return pm_metal_mod_set_about(&g_pm_metal_kernel_about);
}

static int32_t pm_metal_mod_set_about_kernel_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_mod_set_about_kernel();
}

static NativeSymbol g_pm_metal_authors_native_symbols[] = {
  { "pm_metal_about_get", (void *)pm_metal_about_get_native, "($i)i", NULL },
  { "pm_metal_mod_set_about_kernel", (void *)pm_metal_mod_set_about_kernel_native, "()i", NULL },
};

int pm_metal_authors_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_AUTHORS_WASI_MODULE,
                                     g_pm_metal_authors_native_symbols,
                                     sizeof(g_pm_metal_authors_native_symbols) /
                                       sizeof(g_pm_metal_authors_native_symbols[0]))) {
    return -1;
  }

  return 0;
}

#endif /* !__wasm__ */
