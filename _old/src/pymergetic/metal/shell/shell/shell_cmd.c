/** @file
  Shell command registry + argv dispatch. (impl: efi|bios)

  Command tables live in linker section `.pm_metal_shell_cmds.*` (see
  PM_METAL_SHELL_CMD / PM_METAL_SHELL_CMDS). Bounds come from the port
  linker script (PROVIDE_HIDDEN __pm_metal_shell_cmds_{start,end}).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>

extern const pm_metal_shell_cmd_table_t __pm_metal_shell_cmds_start[];
extern const pm_metal_shell_cmd_table_t __pm_metal_shell_cmds_end[];

static const pm_metal_shell_cmd_t *mCmds[PM_METAL_SHELL_CMD_MAX];
static uint32_t                    mCmdCount;

void pm_metal_shell_cmd_register(const pm_metal_shell_cmd_t *cmd)
{
  if (cmd == NULL || cmd->name == NULL || cmd->fn == NULL) {
    return;
  }

  if (mCmdCount >= PM_METAL_SHELL_CMD_MAX) {
    return;
  }

  mCmds[mCmdCount++] = cmd;
}

uint32_t pm_metal_shell_cmd_count(void)
{
  return mCmdCount;
}

const pm_metal_shell_cmd_t *pm_metal_shell_cmd_at(uint32_t i)
{
  if (i >= mCmdCount) {
    return NULL;
  }

  return mCmds[i];
}

const pm_metal_shell_cmd_t *pm_metal_shell_cmd_find(const char *name)
{
  uint32_t i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < mCmdCount; i++) {
    if (strcmp(mCmds[i]->name, name) == 0) {
      return mCmds[i];
    }
  }

  return NULL;
}

void pm_metal_shell_cmd_help(void)
{
  uint32_t i;

  pm_metal_shell_out("commands:");
  for (i = 0; i < mCmdCount; i++) {
    char      line[160];
    char      namepad[13];
    uintptr_t n;
    uintptr_t p;

    /* PrintLib has no %-width for %s — pad the name by hand. */
    n = strlen(mCmds[i]->name);
    if (n > 12u) {
      n = 12u;
    }

    memcpy(namepad, mCmds[i]->name, n);
    for (p = n; p < 12u; p++) {
      namepad[p] = ' ';
    }

    namepad[12] = '\0';
    if (mCmds[i]->help != NULL) {
      snprintf(line, sizeof(line), "  %s %s", namepad, mCmds[i]->help);
    } else {
      snprintf(line, sizeof(line), "  %s", namepad);
    }

    pm_metal_shell_out(line);
  }
}

static int32_t ShellIsSpace(char c)
{
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 1 : 0;
}

/**
 * Split @a line into argv in-place (NUL-terminates tokens in @a buf).
 * Supports "double quotes" (with \\ \" escapes inside) and 'single quotes'
 * (fully literal, no escapes — POSIX shell convention: single quotes have
 * no special character to escape, so a backslash inside them is literal too).
 * Returns argc, or -1 on overflow / unmatched quote.
 */
static int32_t ShellSplitArgv(char *buf, char **argv, uint32_t argv_max)
{
  char    *p;
  char    *out;
  uint32_t argc;

  if (buf == NULL || argv == NULL || argv_max == 0) {
    return -1;
  }

  p    = buf;
  argc = 0;
  while (*p != '\0') {
    while (ShellIsSpace(*p)) {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    if (argc >= argv_max) {
      return -1;
    }

    argv[argc++] = p;
    out          = p;

    if (*p == '"') {
      p++;
      while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
          p++;
          *out++ = *p++;
          continue;
        }

        *out++ = *p++;
      }

      if (*p != '"') {
        return -1;
      }

      p++;
      *out = '\0';
      continue;
    }

    if (*p == '\'') {
      p++;
      while (*p != '\0' && *p != '\'') {
        *out++ = *p++;
      }

      if (*p != '\'') {
        return -1;
      }

      p++;
      *out = '\0';
      continue;
    }

    while (*p != '\0' && !ShellIsSpace(*p)) {
      *out++ = *p++;
    }

    if (ShellIsSpace(*p)) {
      *out = '\0';
      p++;
    } else {
      *out = '\0';
    }
  }

  return (int32_t)argc;
}

void pm_metal_shell_cmd_dispatch(const char *line)
{
  char      buf[PM_METAL_SHELL_LINE_MAX];
  char     *argv[PM_METAL_SHELL_ARGV_MAX];
  int32_t   argc;
  uintptr_t i;

  if (line == NULL) {
    return;
  }

  {
    int32_t n = snprintf(buf, sizeof(buf), "%s", line);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
      pm_metal_shell_out("line too long");
      return;
    }
  }

  argc = ShellSplitArgv(buf, argv, PM_METAL_SHELL_ARGV_MAX);
  if (argc < 0) {
    pm_metal_shell_out("parse error (quote/too many args)");
    return;
  }

  if (argc == 0) {
    return;
  }

  for (i = 0; i < mCmdCount; i++) {
    if (strcmp(argv[0], mCmds[i]->name) == 0) {
      mCmds[i]->fn(argc, argv);
      return;
    }
  }

  {
    char msg[96];

    snprintf(msg, sizeof(msg), "unknown: %s  (try help)", argv[0]);
    pm_metal_shell_out(msg);
  }
}

/* Insertion sort — table is small (PM_METAL_SHELL_CMD_MAX); keeps docs/help
 * listings alphabetical without relying on host qsort. */
static void SortCmdsByName(void)
{
  uint32_t i;

  for (i = 1u; i < mCmdCount; i++) {
    const pm_metal_shell_cmd_t *key = mCmds[i];
    uint32_t                    j   = i;

    while (j > 0u && strcmp(mCmds[j - 1u]->name, key->name) > 0) {
      mCmds[j] = mCmds[j - 1u];
      j--;
    }
    mCmds[j] = key;
  }
}

void pm_metal_shell_cmds_install(void)
{
  const pm_metal_shell_cmd_table_t *t;

  mCmdCount = 0;
  for (t = __pm_metal_shell_cmds_start; t < __pm_metal_shell_cmds_end; t++) {
    uint32_t i;

    if (t->cmds == NULL || t->count == 0) {
      continue;
    }

    for (i = 0; i < t->count; i++) {
      pm_metal_shell_cmd_register(&t->cmds[i]);
    }
  }
  SortCmdsByName();
}
