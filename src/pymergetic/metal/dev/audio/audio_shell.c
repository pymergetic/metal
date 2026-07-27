/** @file
  Shell `audio` / `vol` — status, mute, volume. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/dev/audio/audio.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>

static void AudioStatusOut(void)
{
  char     be[32];
  char     line[96];
  int32_t  ready;
  uint32_t vol;

  if (pm_metal_audio_backend(be, (uint32_t)sizeof(be)) != 0) {
    snprintf(be, sizeof(be), "%s", "?");
  }

  ready = pm_metal_audio_ready();
  vol   = pm_metal_audio_volume_get();
  snprintf(line,
           sizeof(line),
           "audio: backend=%s ready=%s mute=%s vol=%u",
           be,
           ready ? "yes" : "no",
           pm_metal_audio_muted() ? "on" : "off",
           vol);
  pm_metal_shell_out(line);
}

static void AudioUsage(void)
{
  pm_metal_shell_out("usage: audio [status] | audio mute [on|off|toggle] | audio vol [0-100]");
}

static void AudioShellCmd(int argc, char **argv)
{
  if (argc <= 1 || strcmp(argv[1], "status") == 0) {
    AudioStatusOut();
    return;
  }

  if (strcmp(argv[1], "mute") == 0) {
    if (argc < 3 || strcmp(argv[2], "toggle") == 0) {
      pm_metal_audio_mute(pm_metal_audio_muted() ? 0 : 1);
    } else if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "1") == 0) {
      pm_metal_audio_mute(1);
    } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "0") == 0) {
      pm_metal_audio_mute(0);
    } else {
      AudioUsage();
      return;
    }

    AudioStatusOut();
    return;
  }

  if (strcmp(argv[1], "vol") == 0 || strcmp(argv[1], "volume") == 0) {
    long v;

    if (argc < 3) {
      AudioStatusOut();
      return;
    }

    v = strtol(argv[2], NULL, 10);
    if (v < 0) {
      v = 0;
    }

    if (v > 100) {
      v = 100;
    }

    pm_metal_audio_volume_set((uint32_t)v);
    AudioStatusOut();
    return;
  }

  AudioUsage();
}

static void VolShellCmd(int argc, char **argv)
{
  char *nargv[4];

  /* vol [N] -> audio vol [N]; vol alone -> status */
  nargv[0] = "audio";
  nargv[1] = "vol";
  if (argc >= 2) {
    nargv[2] = argv[1];
    AudioShellCmd(3, nargv);
  } else {
    AudioShellCmd(2, nargv);
  }
}

PM_METAL_SHELL_CMDS(g_pm_metal_shell_cmds_audio) = {
  { "audio", "audio [status|mute|vol N]  backend / mute / volume 0-100", AudioShellCmd },
  { "vol", "vol [0-100]         alias for audio vol", VolShellCmd },
};
PM_METAL_SHELL_CMDS_END(g_pm_metal_shell_cmds_audio);
