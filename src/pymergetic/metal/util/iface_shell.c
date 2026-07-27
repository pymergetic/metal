/** @file
  Shell `iface` — browse registered header packs + the native sym table
  (docs/DOC_IFACE_PLAN.md Part II-D). Lives with the other util/ shell
  commands' pattern (see net/ip/ip_shell.c's own subcommand dispatch).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/util/iface.h>

static void IfaceUsage(void)
{
  pm_metal_shell_out("usage: iface | iface ls [pkg] | iface cat <pkg> <path> | iface sym [module "
                     "[name]]");
}

static void IfacePrintPkg(const pm_metal_iface_pkg_info_t *info)
{
  char line[160];

  snprintf(line,
           sizeof(line),
           "  %-16s %-8s files=%-4u blob=%-8u %s",
           info->name,
           info->kind == PM_METAL_IFACE_PKG_HEADERS ? "headers" : "sysroot",
           info->nfiles,
           info->blob_len,
           info->version[0] != '\0' ? info->version : "-");
  pm_metal_shell_out(line);
}

static void IfaceCmdInfo(void)
{
  int32_t n = pm_metal_iface_pkg_count();
  int32_t i;
  char    line[64];

  snprintf(line, sizeof(line), "iface: %d package(s)", n);
  pm_metal_shell_out(line);
  for (i = 0; i < n; i++) {
    pm_metal_iface_pkg_info_t info;

    if (pm_metal_iface_pkg_at((uint32_t)i, &info) == 0) {
      IfacePrintPkg(&info);
    }
  }
}

static void IfaceCmdLs(int32_t argc, char **argv)
{
  if (argc < 3) {
    IfaceCmdInfo();
    return;
  }

  {
    const char *pkg   = argv[2];
    int32_t     n     = pm_metal_iface_file_count(pkg);
    int32_t     i;
    char        line[PM_METAL_IFACE_PATH_MAX + 4u];

    if (n < 0) {
      snprintf(line, sizeof(line), "iface ls: unknown package '%s'", pkg);
      pm_metal_shell_out(line);
      return;
    }

    for (i = 0; i < n; i++) {
      char path[PM_METAL_IFACE_PATH_MAX];

      if (pm_metal_iface_file_at(pkg, (uint32_t)i, path, sizeof(path)) == 0) {
        snprintf(line, sizeof(line), "  %s", path);
        pm_metal_shell_out(line);
      }
    }
  }
}

static void IfaceCmdCat(int32_t argc, char **argv)
{
  const uint8_t *data;
  uint32_t       len;
  char           line[PM_METAL_IFACE_PATH_MAX + 32u];

  if (argc < 4) {
    IfaceUsage();
    return;
  }

  if (pm_metal_iface_file_open(argv[2], argv[3], &data, &len) != 0) {
    snprintf(line, sizeof(line), "iface cat: not found: %s %s", argv[2], argv[3]);
    pm_metal_shell_out(line);
    return;
  }

  pm_metal_shell_out_lines((const char *)data);
  (void)len;
}

static void IfaceCmdSym(int32_t argc, char **argv)
{
  const char *module = (argc >= 3) ? argv[2] : NULL;
  const char *name    = (argc >= 4) ? argv[3] : NULL;
  int32_t     n       = pm_metal_iface_sym_count();
  int32_t     i;
  char        line[200];

  if (module != NULL && name != NULL) {
    pm_metal_iface_sym_t sym;

    if (pm_metal_iface_sym_lookup(module, name, &sym) != 0) {
      pm_metal_shell_out("iface sym: not found");
      return;
    }

    snprintf(line, sizeof(line), "%s %s %s", sym.module, sym.name, sym.sig);
    pm_metal_shell_out(line);
    if (sym.doc_key[0] != '\0') {
      snprintf(line, sizeof(line), "  doc_key: %s", sym.doc_key);
      pm_metal_shell_out(line);
    }
    return;
  }

  for (i = 0; i < n; i++) {
    pm_metal_iface_sym_t sym;

    if (pm_metal_iface_sym_at((uint32_t)i, &sym) != 0) {
      continue;
    }
    if (module != NULL && strcmp(sym.module, module) != 0) {
      continue;
    }

    snprintf(line, sizeof(line), "  %s %s %s", sym.module, sym.name, sym.sig);
    pm_metal_shell_out(line);
  }
}

static void IfaceShellCmd(int32_t argc, char **argv)
{
  if (argc <= 1) {
    IfaceCmdInfo();
    return;
  }

  if (strcmp(argv[1], "ls") == 0) {
    IfaceCmdLs(argc, argv);
    return;
  }
  if (strcmp(argv[1], "cat") == 0) {
    IfaceCmdCat(argc, argv);
    return;
  }
  if (strcmp(argv[1], "sym") == 0) {
    IfaceCmdSym(argc, argv);
    return;
  }

  IfaceUsage();
}

PM_METAL_SHELL_CMD_DOC(g_pm_metal_shell_cmd_iface,
                      "iface",
                      "iface [ls|cat|sym ...] header pack + sym table browser",
                      "iface | iface ls [pkg] | iface cat <pkg> <path> | iface sym [module [name]]",
                      "Browse registered header packs (default metal.guest, see docs/IFACE.md) "
                      "and the build-scraped native sym table. `iface` alone lists packages; "
                      "`iface sym` with no args lists every native; doc_key (if set) points "
                      "into `doc.lookup_key()`.",
                      IfaceShellCmd);
