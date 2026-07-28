/** @file
  Interactive shell — rings/focus/present (shared host).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ping/ping.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/py/py.h>
#include <runtime/time/time.h>

#define SHELL_LINE_MAX PM_METAL_SHELL_LINE_MAX

static int32_t  mDirty;       /* full chrome */
static int32_t  mDirtyInput;  /* console widget only (scrollback + composing line) */
static int32_t  mDirtyStatus; /* status tray only (clock/ifaces/FPS) */
static int32_t  mExitReq;
static int32_t  mExitReboot;
static int32_t  mExitFast;
static uint32_t mPrevPtrButtons;
static int32_t  mPrevPtrX;
static int32_t  mPrevPtrY;
static int32_t  mPrevPtrValid;
static uint64_t mLastFrameMs;
static int32_t  mNest;
static uint32_t mPumpSleepMs;

/* Bash-like command history ring (oldest dropped at PM_METAL_SHELL_HISTORY_MAX). */
static char     mHist[PM_METAL_SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static uint32_t mHistStart; /* index of oldest retained entry */
static uint32_t mHistCount;
static int32_t  mHistPos = -1; /* -1 = draft line; else 0..count-1 (0=oldest) */
static char     mHistDraft[SHELL_LINE_MAX];

static struct {
  int32_t                 live;
  int32_t                 suspended; /* Ctrl-Z park: do not poll until fg/bg */
  int32_t                 fg;        /* 1 = owns prompt until done */
  char                    kind[16];  /* "nslookup" needs 9; keep headroom */
  char                    detail[64];
  pm_metal_async_handle_t task_h;
  pm_metal_async_handle_t coro_h;
  uint64_t                deadline_us;
} mJob;

/* ASCII path: ESC [ A/B from serial/ConIn (VNC/QEMU often skips key events). */
static uint32_t mEscSeq; /* 0=norm 1=ESC 2=CSI */
static char     mLastNl; /* '\0', or '\r'/'\n' just submitted — CRLF pairing */

static void     MetalShellMarkFull(void);
static void     MetalShellMarkInput(void);
static void     MetalShellMarkStatus(void);
static uint32_t MetalShellPromptAnsi(char *out, uint32_t cap);
static void     MetalShellOfferPrompt(void);

/**
 * Full-chrome present — always flips the whole surface, never a guest's
 * left-over blit hint (pm_metal_gfx_present() would replay that instead of
 * the just-repainted desktop, e.g. a fullscreen guest's last frame rect
 * lingering after it exits, leaving a stale sliver on real screen).
 */
static void MetalShellPresentFull(void)
{
  pm_metal_gfx_surface_t *surf;

  surf = pm_metal_gfx_surface();
  if (surf != NULL) {
    (void)pm_metal_gfx_present_rect(0, 0, (int32_t)surf->width, (int32_t)surf->height);
  } else {
    (void)pm_metal_gfx_present();
  }
}

static int32_t mPromptPending = 1; /* show prompt after boot banner */

/**
 * Live fullscreen guest (`run doom`) owns the DEFAULT FB — shell must not
 * paint chrome/prompt over it. Windowed (`tab doom`) keeps the strip.
 */
static int32_t MetalShellGuestFullscreen(void)
{
  pm_metal_process_id_t pid;

  pid = pm_metal_process_current();
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    return 0;
  }

  /* Current process row is the shell-facing “live guest” signal. */
  return (pm_metal_process_ui_kind(pid) == PM_METAL_PROC_UI_FULLSCREEN) ? 1 : 0;
}

/**
 * Live guest on the active tab (windowed). Tab strip/status stay; the
 * shared input blink must not fight the guest content present.
 */
static int32_t MetalShellGuestWindowedActive(void)
{
  pm_metal_process_id_t pid;
  pm_metal_ui_handle_t  tab;

  pid = pm_metal_process_current();
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    return 0;
  }

  if (pm_metal_process_ui_kind(pid) != PM_METAL_PROC_UI_TAB) {
    return 0;
  }

  tab = pm_metal_ui_tab_active();
  return (tab != PM_METAL_UI_HANDLE_INVALID && tab == pm_metal_process_tab(pid)) ? 1 : 0;
}

static const char *MetalShellHistAt(uint32_t idx)
{
  if (idx >= mHistCount) {
    return NULL;
  }

  return mHist[(mHistStart + idx) % PM_METAL_SHELL_HISTORY_MAX];
}

void pm_metal_shell_history_add(const char *line)
{
  const char *last;
  uint32_t    slot;

  if (line == NULL || line[0] == '\0') {
    return;
  }

  last = MetalShellHistAt(mHistCount > 0u ? mHistCount - 1u : 0u);
  if (mHistCount > 0u && last != NULL && strcmp(last, line) == 0) {
    return;
  }

  if (mHistCount < PM_METAL_SHELL_HISTORY_MAX) {
    slot = (mHistStart + mHistCount) % PM_METAL_SHELL_HISTORY_MAX;
    snprintf(mHist[slot], sizeof(mHist[slot]), "%s", line);
    mHistCount++;
  } else {
    slot = mHistStart;
    snprintf(mHist[slot], sizeof(mHist[slot]), "%s", line);
    mHistStart = (mHistStart + 1u) % PM_METAL_SHELL_HISTORY_MAX;
  }

  mHistPos = -1;
}

uint32_t pm_metal_shell_history_count(void)
{
  return mHistCount;
}

int pm_metal_shell_history_get(uint32_t idx, char *out, uint32_t cap)
{
  const char *src;

  if (out == NULL || cap == 0) {
    return -1;
  }

  src = MetalShellHistAt(idx);
  if (src == NULL) {
    out[0] = '\0';
    return -1;
  }

  snprintf(out, cap, "%s", src);
  return 0;
}

/**
 * Redraw the *whole* current input line on COM1: erase-line + CR, then
 * prompt + text. Needed for history recall specifically -- every other
 * input.c mutation (typing, backspace, cursor move) is echoed byte-by-byte
 * as it happens, but pm_metal_ui_input_set() (recall's only primitive) only
 * ever touches the framebuffer UI's input-strip state, so without this a
 * serial-only session (no framebuffer/VNC in view) would see Up/Down do
 * nothing at all even though the recalled line is correctly staged and
 * would submit fine on Enter.
 */
static void MetalShellRedrawCom1(void)
{
  char     buf[SHELL_LINE_MAX + PM_METAL_HOST_NAME_MAX + 64];
  uint32_t plen;
  char     text[SHELL_LINE_MAX];

  if (pm_metal_ui_input_text(text, sizeof(text)) < 0) {
    text[0] = '\0';
  }

  plen = MetalShellPromptAnsi(buf, sizeof(buf));
  pm_metal_console_com1_write("\r\x1b[2K", 5);
  if (plen > 0u && plen + 1u < sizeof(buf)) {
    snprintf(&buf[plen], sizeof(buf) - plen, "%s", text);
    pm_metal_console_com1_write(buf, strlen(buf));
  }
}

static void MetalShellHistRecall(int32_t dir)
{
  if (mHistCount == 0u) {
    return;
  }

  if (dir < 0) {
    /* Up → older */
    if (mHistPos < 0) {
      if (pm_metal_ui_input_text(mHistDraft, sizeof(mHistDraft)) < 0) {
        mHistDraft[0] = '\0';
      }

      mHistPos = (int32_t)mHistCount - 1;
    } else if (mHistPos > 0) {
      mHistPos--;
    } else {
      return;
    }
  } else {
    /* Down → newer / draft */
    if (mHistPos < 0) {
      return;
    }

    if ((uint32_t)mHistPos + 1u < mHistCount) {
      mHistPos++;
    } else {
      mHistPos = -1;
      (void)pm_metal_ui_input_set(mHistDraft);
      MetalShellMarkInput();
      MetalShellRedrawCom1();
      return;
    }
  }

  {
    const char *line;

    line = MetalShellHistAt((uint32_t)mHistPos);
    if (line != NULL) {
      (void)pm_metal_ui_input_set(line);
      MetalShellMarkInput();
      MetalShellRedrawCom1();
    }
  }
}

/** Ctrl+Shift+Left/Right → cycle tabs (consume; works under guest focus too). */
static int32_t MetalShellTabChordFilter(const pm_metal_input_key_event_t *ev)
{
  int32_t delta;

  if (ev == NULL || ev->pressed == 0) {
    return 0;
  }

  if ((ev->code != PM_METAL_KEY_LEFT && ev->code != PM_METAL_KEY_RIGHT) ||
      (ev->mods & PM_METAL_INPUT_MOD_CTRL) == 0 || (ev->mods & PM_METAL_INPUT_MOD_SHIFT) == 0) {
    return 0;
  }

  delta = (ev->code == PM_METAL_KEY_LEFT) ? -1 : 1;
  if (pm_metal_ui_tab_cycle(delta) == 0) {
    char msg[40];

    snprintf(msg, sizeof(msg), "active tab %u", pm_metal_ui_tab_active_index());
    pm_metal_ui_set_status(msg);
    MetalShellMarkFull();
  }

  return 1;
}

/**
 * Ctrl+Alt+Home → cycle the PS/2 keyboard layout (same effect as the
 * `keyb` shell command, one step forward, wrapping). Home is on the
 * mods/nav push_key whitelist even under shell focus (see
 * src/efi|bios/.../dev/input/input_port.c), unlike plain letter keys —
 * so this chord reaches the filter without any port changes, which is
 * why Home (not a mnemonic letter) was picked as the trigger key.
 */
static int32_t MetalShellKeybChordFilter(const pm_metal_input_key_event_t *ev)
{
  pm_metal_input_keyb_t layout;
  const char           *name;
  char                  msg[40];

  if (ev == NULL || ev->pressed == 0 || ev->code != PM_METAL_KEY_HOME ||
      (ev->mods & PM_METAL_INPUT_MOD_CTRL) == 0 || (ev->mods & PM_METAL_INPUT_MOD_ALT) == 0) {
    return 0;
  }

  layout = pm_metal_input_keyb_cycle();
  name   = pm_metal_input_keyb_name(layout);
  snprintf(msg, sizeof(msg), "keyb: %s", (name != NULL) ? name : "?");
  pm_metal_ui_set_status(msg);
  MetalShellMarkStatus();
  return 1;
}

/**
 * pm_metal_input_set_filter() only has one slot — every global chord (tab
 * cycling, keyb layout cycling, ...) is dispatched from this single entry
 * point instead of fighting over the registration.
 */
static int32_t MetalShellChordFilter(const pm_metal_input_key_event_t *ev)
{
  if (MetalShellTabChordFilter(ev) != 0) {
    return 1;
  }

  return MetalShellKeybChordFilter(ev);
}

static void MetalShellJobFinish(int32_t st)
{
  char msg[96];

  if (strcmp(mJob.kind, "ping") == 0) {
    if (st != PM_METAL_DONE) {
      uint32_t err;

      err = pm_metal_net_ping_last_err();
      if (err == PM_METAL_NET_PING_ERR_RESOLVE) {
        pm_metal_shell_out("ping: resolve failed");
      } else if (err == PM_METAL_NET_PING_ERR_TIMEOUT) {
        pm_metal_shell_out("ping: no reply");
      } else if (err == PM_METAL_NET_PING_ERR_NOROUTE) {
        pm_metal_shell_out("ping: no route");
      } else if (err == PM_METAL_NET_PING_ERR_NOMEM) {
        pm_metal_shell_out("ping: out of memory");
      } else if (err == PM_METAL_NET_PING_ERR_SEND) {
        pm_metal_shell_out("ping: send failed");
      } else if (st == PM_METAL_CANCELLED) {
        pm_metal_shell_out("ping: cancelled");
      } else {
        pm_metal_shell_out("ping: failed");
      }
    } else {
      uint32_t us;

      /* DONE ⇒ echo received; don't treat sub-ms (floored 0 ms) as failure. */
      us = pm_metal_net_ping_rtt_us(mJob.coro_h);
      snprintf(msg, sizeof(msg), "ping %s: %u.%u ms", mJob.detail, us / 1000u, (us / 100u) % 10u);
      pm_metal_shell_out(msg);
    }
  } else if (strcmp(mJob.kind, "nslookup") == 0) {
    if (st != PM_METAL_DONE || (uint32_t)(uintptr_t)pm_metal_async_result_u32(mJob.coro_h) == 0u) {
      snprintf(msg, sizeof(msg), "nslookup: %s failed", mJob.detail);
      pm_metal_shell_out(msg);
    } else {
      char ip[64];

      if (pm_metal_net_ip_dns_last_ntoa(ip, sizeof(ip)) != 0) {
        pm_metal_shell_out("nslookup: no address");
      } else {
        snprintf(msg, sizeof(msg), "%s -> %s", mJob.detail, ip);
        pm_metal_shell_out(msg);
      }
    }
  } else if (strcmp(mJob.kind, "test") == 0) {
    if (st == PM_METAL_DONE && pm_metal_boot_tests_result(mJob.coro_h) == 0) {
      pm_metal_shell_out("test: ok");
    } else {
      pm_metal_shell_out("test: FAILED");
    }
  }

  memset(&mJob, 0, sizeof(mJob));
  MetalShellMarkFull();
  MetalShellOfferPrompt();
}

int pm_metal_shell_job_busy(void)
{
  return mJob.live ? 1 : 0;
}

void pm_metal_shell_prompt_dirty(void)
{
  /* Don't steal the line while a foreground job owns the next OfferPrompt. */
  if ((!mJob.live || !mJob.fg) && pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
    mPromptPending = 1;
  }
}

int pm_metal_shell_job_start(const char             *kind,
                             pm_metal_async_handle_t task_h,
                             pm_metal_async_handle_t coro_h,
                             const char             *detail,
                             uint64_t                deadline_us)
{
  if (kind == NULL || task_h == PM_METAL_ASYNC_HANDLE_INVALID || mJob.live) {
    return -1;
  }

  memset(&mJob, 0, sizeof(mJob));
  snprintf(mJob.kind, sizeof(mJob.kind), "%s", kind);
  mJob.detail[0] = '\0';
  if (detail != NULL) {
    snprintf(mJob.detail, sizeof(mJob.detail), "%.*s", (int)(sizeof(mJob.detail) - 1u), detail);
    mJob.detail[sizeof(mJob.detail) - 1u] = '\0';
  }

  mJob.task_h      = task_h;
  mJob.coro_h      = coro_h;
  mJob.deadline_us = deadline_us;
  mJob.live        = 1;
  mJob.suspended   = 0;
  mJob.fg          = 1;
  mPumpSleepMs     = 1u;
  return 0;
}

int pm_metal_shell_job_list(char *out, uint32_t cap)
{
  const char *state;

  if (out == NULL || cap == 0u) {
    return -1;
  }

  out[0] = '\0';
  if (!mJob.live) {
    return -1;
  }

  if (mJob.suspended) {
    state = "Stopped";
  } else if (mJob.fg) {
    state = "Running";
  } else {
    state = "Running";
  }

  if (mJob.detail[0] != '\0') {
    snprintf(out, cap, "[1]%c  %s\t%s %s", mJob.fg ? '+' : '-', state, mJob.kind, mJob.detail);
  } else {
    snprintf(out, cap, "[1]%c  %s\t%s", mJob.fg ? '+' : '-', state, mJob.kind);
  }

  return 0;
}

int pm_metal_shell_job_cancel(void)
{
  if (!mJob.live) {
    return -1;
  }

  pm_metal_shell_out("^C");
  pm_metal_async_task_cancel(mJob.task_h);
  MetalShellJobFinish(PM_METAL_CANCELLED);
  return 0;
}

int pm_metal_shell_job_suspend(void)
{
  char line[96];

  if (!mJob.live || mJob.suspended) {
    return -1;
  }

  mJob.suspended = 1;
  mJob.fg        = 0;
  pm_metal_shell_out("^Z");
  if (mJob.detail[0] != '\0') {
    snprintf(line, sizeof(line), "[1]+  Stopped\t%s %s", mJob.kind, mJob.detail);
  } else {
    snprintf(line, sizeof(line), "[1]+  Stopped\t%s", mJob.kind);
  }

  pm_metal_shell_out(line);
  MetalShellMarkFull();
  MetalShellOfferPrompt();
  return 0;
}

int pm_metal_shell_job_fg(void)
{
  if (!mJob.live) {
    return -1;
  }

  mJob.suspended = 0;
  mJob.fg        = 1;
  mPumpSleepMs   = 1u;
  return 0;
}

int pm_metal_shell_job_bg(void)
{
  if (!mJob.live) {
    return -1;
  }

  mJob.suspended = 0;
  mJob.fg        = 0;
  mPumpSleepMs   = 1u;
  MetalShellOfferPrompt();
  return 0;
}

static void MetalShellJobPoll(void)
{
  int32_t st;

  if (!mJob.live) {
    return;
  }

  st = pm_metal_async_task_status(mJob.task_h);
  /* Stopped jobs still notice terminal completion (runners keep pumping). */
  if (mJob.suspended) {
    if (st != PM_METAL_PENDING && st != PM_METAL_WAITING) {
      MetalShellJobFinish(st);
    }

    return;
  }

  /* WAITING = parked on sleep/DNS/I/O — still live (not terminal). */
  if (st == PM_METAL_PENDING || st == PM_METAL_WAITING) {
    if (mJob.deadline_us != 0 && pm_metal_time_mono_us() >= mJob.deadline_us) {
      pm_metal_async_task_cancel(mJob.task_h);
      if (strcmp(mJob.kind, "ping") == 0) {
        pm_metal_shell_out("ping: timeout");
      } else if (strcmp(mJob.kind, "nslookup") == 0) {
        pm_metal_shell_out("nslookup: timeout");
      } else {
        pm_metal_shell_out("test: FAILED");
      }

      memset(&mJob, 0, sizeof(mJob));
      MetalShellMarkFull();
      MetalShellOfferPrompt();
    }

    mPumpSleepMs = 1u;
    return;
  }

  MetalShellJobFinish(st);
}

static void MetalShellMarkFull(void)
{
  mDirty       = 1;
  mDirtyInput  = 0;
  mDirtyStatus = 0;
  mPumpSleepMs = 1;
}

static void MetalShellMarkInput(void)
{
  /*
   * The composing line is just the console's own trailing row(s) now (see
   * MetalUiConsoleTotalRows) -- typing never resizes the console rect, so
   * unlike the old separate input strip this never needs to escalate to
   * MetalShellMarkFull() for a layout change; it always just repaints the
   * (cheap, console-widget-scoped) dirty rect.
   */
  if (!mDirty) {
    mDirtyInput = 1;
  }

  mPumpSleepMs = 1;
}

static void MetalShellMarkStatus(void)
{
  if (!mDirty) {
    mDirtyStatus = 1;
  }

  mPumpSleepMs = 1;
}

void pm_metal_shell_serial_log(const char *line)
{
  /* Unified log owns UEFI/UART/UI sinks (incl. COM1 after UART attach). */
  pm_metal_log(line);
}

static void MetalShellPollGuestKeys(uint64_t now_ms)
{
  /* BIOS: i8042 make/break already pushed in input_poll; expire holds. */
  pm_metal_input_tick(now_ms);
}

static void MetalShellEcho(const char *line)
{
  pm_metal_stream_h out;

  /*
   * When stdio is redirected (SSH PTY/PIPE, wasm guest), command output
   * must reach that stream — shell_out is the common sink for handlers.
   */
  out = pm_metal_stdio_out();
  if (out != PM_METAL_STREAM_INVALID) {
    (void)pm_metal_stream_write_line(out, line);
  }

  /*
   * Unified log owns sinks (UEFI/UART/UI viewports). Still mirror into
   * the active guest tab when not in guest focus.
   */
  if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
    if (pm_metal_ui_tab_active_index() != 0) {
      pm_metal_ui_active_puts(line);
    }

    MetalShellMarkFull();
  }

  pm_metal_log(line);
}

void pm_metal_shell_log(const char *line)
{
  pm_metal_stream_h out;

  out = pm_metal_stdio_out();
  if (out != PM_METAL_STREAM_INVALID) {
    (void)pm_metal_stream_write_line(out, line);
    /* Keep serial for verify markers; ConOut only before EBS. */
    pm_metal_shell_serial_log(line);

    if (!MetalShellGuestFullscreen()) {
      MetalShellMarkFull();
    }

    return;
  }

  MetalShellEcho(line);
}

void pm_metal_shell_set_status(const char *text)
{
  /* Avoid chrome redraw while a guest owns the framebuffer. */
  if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  pm_metal_ui_set_status(text);
  MetalShellMarkFull();
}

void pm_metal_shell_request_exit(void)
{
  mExitReq    = 1;
  mExitReboot = 0;
  mExitFast   = 0;
}

int pm_metal_shell_exit_reboot(void)
{
  return mExitReboot ? 1 : 0;
}

int pm_metal_shell_exit_fast(void)
{
  return mExitFast ? 1 : 0;
}

static void MetalShellEchoLines(const char *text)
{
  char      line[200];
  uintptr_t li;
  uintptr_t i;

  if (text == NULL) {
    return;
  }

  li = 0;
  for (i = 0;; i++) {
    if (text[i] == '\n' || text[i] == '\0') {
      line[li] = '\0';
      if (li > 0) {
        MetalShellEcho(line);
      }

      li = 0;
      if (text[i] == '\0') {
        break;
      }
    } else if (li + 1 < sizeof(line)) {
      line[li++] = text[i];
    }
  }
}

uint32_t pm_metal_shell_prompt(char *out, uint32_t cap)
{
  const char *host;
  uintptr_t   n;

  if (out == NULL || cap < 4u) {
    return 0;
  }

  if (pm_metal_py_repl_active()) {
    n = snprintf(out, cap, "%s", pm_metal_py_repl_prompt());
    return (n >= cap) ? 0u : (uint32_t)n;
  }

  host = pm_metal_host_name_cstr();
  if (host == NULL || host[0] == '\0') {
    host = "metal";
  }

  /* bash-like: hostname:~$  (no multi-user; ~ = shell home) */
  n = snprintf(out, cap, "%s:~$ ", host);
  if (n == 0 || n >= cap) {
    snprintf(out, cap, "%s", "$ ");
    return 2;
  }

  return (uint32_t)n;
}

/**
 * Colored prompt for COM1 / UI scrollback.
 * Space is AFTER the final reset — terminals often drop a trailing space
 * that sits inside an SGR segment (showed up as "metal:~$help").
 */
static uint32_t MetalShellPromptAnsi(char *out, uint32_t cap)
{
  const char *host;
  uintptr_t   n;

  if (out == NULL || cap < 8u) {
    return 0;
  }

  if (pm_metal_py_repl_active()) {
    /* bold magenta ">>> " / "... " — visually distinct from the C shell's
     * green/blue host prompt so it's unmistakable which mode is live. */
    n = snprintf(out, cap, "\033[1;35m%s\033[0m", pm_metal_py_repl_prompt());
    return (n >= cap) ? 0u : (uint32_t)n;
  }

  host = pm_metal_host_name_cstr();
  if (host == NULL || host[0] == '\0') {
    host = "metal";
  }

  /* bold green host, bold blue :~, bold green $, reset, then space */
  n = snprintf(out, cap, "\033[1;32m%s\033[0m\033[1;34m:~\033[0m\033[1;32m$\033[0m ", host);
  if (n == 0 || n >= cap) {
    return 0;
  }

  return (uint32_t)n;
}

/**
 * Live prompt for serial (COM1) + refresh the UI input strip.
 * Scrollback only gets prompt+line when a command is committed.
 */
static void MetalShellOfferPrompt(void)
{
  char     ps[PM_METAL_HOST_NAME_MAX + 48];
  uint32_t n;

  n = MetalShellPromptAnsi(ps, sizeof(ps));
  if (n > 0u) {
    pm_metal_console_com1_write(ps, n);
  }

  mPromptPending = 0;
  MetalShellMarkInput();
}

void pm_metal_shell_out(const char *line)
{
  MetalShellEcho(line);
}

void pm_metal_shell_out_lines(const char *text)
{
  MetalShellEchoLines(text);
}

void pm_metal_shell_mark_full(void)
{
  MetalShellMarkFull();
}

void pm_metal_shell_cmd_exit(int32_t reboot, int32_t fast)
{
  mExitReboot = reboot ? 1 : 0;
  mExitFast   = fast ? 1 : 0;
  mExitReq    = 1;
}

int pm_metal_shell_run(const char *mod)
{
  return pm_metal_shell_run_args(mod, NULL);
}

int pm_metal_shell_run_args(const char *mod, const char *args)
{
  int32_t              rc;
  char                 msg[96];
  pm_metal_ui_handle_t tab;

  if (mod == NULL || mod[0] == '\0') {
    MetalShellEcho("usage: run <mod>");
    return -1;
  }

  if (mNest > 0) {
    MetalShellEcho("run: nested run refused");
    return -1;
  }

  if (!pm_metal_wasm_ready()) {
    MetalShellEcho("run: wasm runtime not ready");
    return -1;
  }

  tab   = pm_metal_ui_tab_active();
  mNest = 1;
  rc    = pm_metal_process_spawn_mod_args(mod, PM_METAL_PROC_UI_FULLSCREEN, tab, args);
  mNest = 0;

  if (rc < 0) {
    /*
     * Common miss: cold BIOS/PXE needs HTTP seed (metal-pkg: ensure / oom /
     * http / timeout on the log). QEMU usually has METAL_EXT_APPS on ESP.
     */
    snprintf(msg, sizeof(msg), "run '%s': load failed (see metal-pkg:)", mod);
  } else if (pm_metal_process_active()) {
    snprintf(
      msg, sizeof(msg), "run '%s': process %u live", mod, (uint32_t)pm_metal_process_current());
  } else {
    snprintf(msg, sizeof(msg), "run '%s': exited %d", mod, rc);
  }

  /* Serial/log only — fullscreen guest owns the FB; no chrome dirty. */
  pm_metal_log(msg);
  if (!MetalShellGuestFullscreen()) {
    MetalShellEcho(msg);
    pm_metal_ui_set_status(msg);
    MetalShellMarkFull();
  }

  return rc;
}

int pm_metal_shell_tab(const char *mod)
{
  return pm_metal_shell_tab_args(mod, NULL);
}

int pm_metal_shell_tab_args(const char *mod, const char *args)
{
  pm_metal_ui_handle_t tab;
  int32_t              rc;
  char                 msg[96];

  if (mod == NULL || mod[0] == '\0') {
    MetalShellEcho("usage: tab <mod>");
    return -1;
  }

  if (mNest > 0) {
    MetalShellEcho("tab: nested run refused");
    return -1;
  }

  if (!pm_metal_wasm_ready()) {
    MetalShellEcho("tab: wasm runtime not ready");
    return -1;
  }

  tab = pm_metal_ui_tab_open(mod, 1);
  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    MetalShellEcho("tab: failed to open");
    return -1;
  }

  mNest = 1;
  rc    = pm_metal_process_spawn_mod_args(mod, PM_METAL_PROC_UI_TAB, tab, args);
  mNest = 0;

  if (rc < 0) {
    snprintf(msg, sizeof(msg), "tab '%s': host error - close when done", mod);
  } else if (pm_metal_process_active()) {
    snprintf(
      msg, sizeof(msg), "tab '%s': process %u live", mod, (uint32_t)pm_metal_process_current());
  } else {
    snprintf(msg, sizeof(msg), "tab '%s': exited %d - close when done", mod, rc);
  }

  pm_metal_ui_tab_puts(tab, msg);
  MetalShellEcho(msg);
  pm_metal_ui_set_status(msg);
  MetalShellMarkFull();
  return rc;
}

int pm_metal_shell_init(void)
{
  mDirty          = 1;
  mDirtyInput     = 0;
  mDirtyStatus    = 0;
  mExitReq        = 0;
  mExitReboot     = 0;
  mExitFast       = 0;
  mLastFrameMs    = 0;
  mNest           = 0;
  mPumpSleepMs    = 1u;
  mPrevPtrButtons = 0;
  mPrevPtrValid   = 0;
  memset(&mJob, 0, sizeof(mJob));

  pm_metal_ui_set_status("shell ready");
  pm_metal_ui_input_clear();
  pm_metal_input_set_filter(MetalShellChordFilter);
  pm_metal_shell_cmds_install();
  (void)pm_metal_ui_frame();
  MetalShellPresentFull();
  return 0;
}

/*
 * Call syntax only -- "console()"/"quit()"/"exit()" -- never the bare
 * keyword-style word. Real Python has no bare-word statements that call
 * something, so a bare "console"/"quit"/"exit" reads as a NameError
 * lookup, not an escape command; only the f() form is accepted so this
 * doesn't quietly special-case what looks like ordinary (buggy) Python
 * source. CPython/IPython's own quit()/exit() sentinels are call syntax
 * too -- this matches that muscle memory instead of MicroPython's
 * NameError (it defines no quit/exit objects).
 */
static int32_t PyReplIsQuitCall(const char *text)
{
  return strcmp(text, "console()") == 0 || strcmp(text, "quit()") == 0 ||
         strcmp(text, "exit()") == 0;
}

static void MetalShellHandleAscii(char ch, char *text, uintptr_t text_sz)
{
  /* Metal job control (shared console / SSH viewport): Ctrl-C cancel, Ctrl-Z stop. */
  if (ch == 0x03) {
    mEscSeq = 0u;
    mLastNl = 0;
    if (pm_metal_shell_job_busy()) {
      (void)pm_metal_shell_job_cancel();
    } else {
      pm_metal_shell_out("^C");
      pm_metal_ui_input_clear();
      mHistPos = -1;
      MetalShellMarkFull();
      MetalShellOfferPrompt();
    }

    return;
  }

  if (ch == 0x1a) {
    mEscSeq = 0u;
    mLastNl = 0;
    (void)pm_metal_shell_job_suspend();
    return;
  }

  /* CSI: ESC [ A/B/C/D — history / ignore arrows (serial & many VNC paths). */
  if (mEscSeq == 1u) {
    if (ch == '[') {
      mEscSeq = 2u;
      return;
    }

    if (ch == 'O') {
      /* SS3 prefix (ESC O A) — stay in CSI-like wait for final. */
      mEscSeq = 2u;
      return;
    }

    mEscSeq = 0u;
    /* Fall through and treat this byte normally. */
  } else if (mEscSeq == 2u) {
    mEscSeq = 0u;
    if (ch == 'A') {
      if (pm_metal_ui_input_move_visual_row(-1) == 0) {
        MetalShellHistRecall(-1);
      } else {
        MetalShellMarkInput();
      }

      return;
    }

    if (ch == 'B') {
      if (pm_metal_ui_input_move_visual_row(1) == 0) {
        MetalShellHistRecall(1);
      } else {
        MetalShellMarkInput();
      }

      return;
    }

    if (ch == 'C') {
      (void)pm_metal_ui_input_move_cursor(1);
      MetalShellMarkInput();
      return;
    }

    if (ch == 'D') {
      (void)pm_metal_ui_input_move_cursor(-1);
      MetalShellMarkInput();
      return;
    }

    return;
  }

  if (ch == 0x1b) {
    mEscSeq = 1u;
    mLastNl = 0;
    return;
  }

  if (ch == '\r' || ch == '\n') {
    char nl;
    char crlf[2];

    mEscSeq = 0u;

    if (mLastNl != 0 && mLastNl != ch) {
      /* Second half of a CRLF (or LFCR) pair for the *same* Enter keypress
       * -- some terminals (raw serial clients in CRLF mode) send both
       * bytes for one Enter. ch=='\r'||ch=='\n' used to treat each byte
       * as its own complete Enter with no pairing, so one physical Enter
       * submitted the real line and then immediately phantom-submitted
       * again on an now-empty buffer ("repl: busy, try again" or a stray
       * empty command) -- swallow this half silently instead. */
      mLastNl = 0;
      return;
    }

    mLastNl = ch;
    crlf[0] = '\r';
    crlf[1] = '\n';

    /*
     * Shift+Enter (HID): soft newline in the UI buffer. UART has no Shift —
     * Enter always submits.
     */
    if ((pm_metal_input_mod_state() & PM_METAL_INPUT_MOD_SHIFT) != 0) {
      pm_metal_console_com1_write(crlf, 2);
      (void)pm_metal_ui_input_append('\n');
      MetalShellMarkInput();
      return;
    }

    pm_metal_console_com1_write(crlf, 2);

    nl = '\n';
    (void)pm_metal_stream_feed_stdin(&nl, 1);
    if (pm_metal_ui_input_text(text, text_sz) < 0) {
      text[0] = '\0';
    }

    {
      char     echo[SHELL_LINE_MAX + PM_METAL_HOST_NAME_MAX + 64];
      uint32_t plen;

      /*
       * UI scrollback gets ANSI prompt + command (paint understands SGR).
       * Serial already showed OfferPrompt + typed chars — don't log again.
       */
      plen = MetalShellPromptAnsi(echo, sizeof(echo));
      if (plen > 0u && plen + 1u < sizeof(echo)) {
        snprintf(&echo[plen], sizeof(echo) - plen, "%s", text);
        pm_metal_ui_console_puts(echo);
      } else {
        plen = pm_metal_shell_prompt(echo, sizeof(echo));
        if (plen + 1u < sizeof(echo)) {
          snprintf(&echo[plen], sizeof(echo) - plen, "%s", text);
        }

        pm_metal_ui_console_puts(echo);
      }
    }

    pm_metal_shell_history_add(text);
    /*
     * REPL active: committed lines are Python source, not shell commands
     * -- feed the line queue (py.c's PY_STEP_REPL) instead of the normal
     * dispatcher. "console()" is the reserved escape call to fall back to
     * the C command shell (matches docs/MICROPYTHON.md's "C console
     * stays reachable as fallback", never deleted); "quit()"/"exit()" are
     * accepted as aliases (CPython/IPython muscle memory for "leave this
     * REPL" -- MicroPython itself defines no quit/exit objects, so these
     * would otherwise just NameError). Call syntax only, see
     * PyReplIsQuitCall()'s comment for why the bare keyword-style word is
     * deliberately not accepted.
     *
     * defer_prompt: feed_line() only *enqueues* the line -- the REPL
     * coroutine (py.c's PY_STEP_REPL) decides ">>> " vs "... " and runs
     * any exec on its own next scheduler tick, not synchronously here.
     * Printing the prompt right now would show last tick's stale value
     * (e.g. ">>> " right after typing "def f():", before the engine has
     * had a chance to notice it needs "... "). Set mPromptPending
     * instead and let the next pm_metal_shell_poll() tick (which runs
     * after the async engine, not before it) draw the prompt the
     * feed_line() call actually produced.
     */
    {
      int32_t defer_prompt = 0;

      if (pm_metal_py_repl_active()) {
        if (PyReplIsQuitCall(text)) {
          pm_metal_py_repl_stop();
          pm_metal_shell_out("py: repl paused -- back to console (type 'py -i' to resume)");
        } else if (pm_metal_py_repl_feed_line(text, strlen(text)) != 0) {
          pm_metal_shell_out("repl: busy, try again");
        } else {
          defer_prompt = 1;
        }
      } else {
        pm_metal_shell_cmd_dispatch(text);
      }

      pm_metal_ui_input_clear();
      mHistPos = -1;
      /*
       * Fullscreen guest (`run doom`) owns the FB -- do not dirty chrome or
       * re-offer the shell prompt over it. Windowed / idle: normal prompt.
       */
      if (MetalShellGuestFullscreen()) {
        return;
      }

      MetalShellMarkFull();
      if (defer_prompt) {
        mPromptPending = 1;
      } else {
        /* Next line on COM1 + input strip (after command output). */
        MetalShellOfferPrompt();
      }
    }
    return;
  }

  if (ch == 0x7f || ch == 0x08) {
    const char bs[3] = { '\b', ' ', '\b' };

    mLastNl = 0;
    /* Empty line: do not erase the prompt on serial. */
    if (pm_metal_ui_input_backspace() == 0) {
      pm_metal_console_com1_write(bs, 3);
      MetalShellMarkInput();
    }

    return;
  }

  /*
   * Tab -> 4 spaces (indent convenience, not completion -- there is no
   * completion hook on either surface today; see docs/MICROPYTHON.md's
   * "known limitation" note on mp_hal_stdin_rx_chr). Matters most for the
   * REPL's multi-line blocks (def/if/for/...), but wiring it into the one
   * shared line editor benefits the C console too -- Tab was a pure no-op
   * there before this (fell through every special case, then failed the
   * printable-range filter below since 0x09 < 32), so there is nothing to
   * regress.
   */
  if (ch == '\t') {
    static const char spaces[4] = { ' ', ' ', ' ', ' ' };
    uint32_t          i;

    mLastNl = 0;
    pm_metal_console_com1_write(spaces, sizeof(spaces));
    for (i = 0; i < sizeof(spaces); i++) {
      (void)pm_metal_stream_feed_stdin(&spaces[i], 1);
      (void)pm_metal_ui_input_append(' ');
    }

    MetalShellMarkInput();
    return;
  }

  /*
   * Printable range: ASCII 32-126 plus the upper Latin-15/ISO-8859-15 half
   * (0x80-0xFF) that `keyb gr` emits for the umlauts/ss/paragraph/degree/
   * acute (see keyb.c) -- those bytes are >= 0x80, so comparing the signed
   * `char` directly against 127 sign-extends them negative and drops every
   * one of them here before they ever reach the input line. Only DEL (0x7F,
   * already handled above) is excluded.
   */
  if ((uint8_t)ch >= 32 && (uint8_t)ch != 0x7fu) {
    mLastNl = 0;
    pm_metal_console_com1_write(&ch, 1);
    (void)pm_metal_stream_feed_stdin(&ch, 1);
    (void)pm_metal_ui_input_append(ch);
    MetalShellMarkInput();
  }
}

int pm_metal_shell_poll(void)
{
  uint64_t now_ms;
  char     text[SHELL_LINE_MAX];

  now_ms = pm_metal_time_mono_us() / 1000u;
  /*
   * Guest frame pacing awaits sleep_until; a 16 ms host nap adds almost a
   * whole frame of wake latency on iron. Keep idle at 16 ms.
   */
  mPumpSleepMs = pm_metal_process_active() ? 1u : 16u;
  if (!MetalShellGuestFullscreen() && pm_metal_ui_tick(now_ms)) {
    /* Clock / net / FPS — dirty-rect present, not a full chrome frame. */
    MetalShellMarkStatus();
  }

  /* After boot banner: first live prompt on serial + input strip. */
  if (mPromptPending && pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
    MetalShellOfferPrompt();
  }

  /* Heal focus before drain — fullscreen guests must own HID every tick. */
  if (pm_metal_process_active()) {
    pm_metal_ui_sync_input_focus();
  }

  /* Drain HW into rings before shell/async consumers (port-owned). */
  pm_metal_input_poll();

  pm_metal_net_ip_poll();
  pm_metal_audio_poll();
  pm_metal_console_poll();

  /* Runners / process ownership — shell does not know wasm sessions. */
  pm_metal_process_pump_runners();
  MetalShellJobPoll();

  /*
   * Unlocked pointer → chrome: cursor, tab hover/click, console scroll.
   * Shell focus always; windowed guests keep the strip; fullscreen owns FB.
   */
  {
    int32_t ui_ptr;

    ui_ptr = 0;
    if (!pm_metal_input_pointer_locked() && !MetalShellGuestFullscreen()) {
      if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
        ui_ptr = 1;
      } else if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_GUEST) {
        ui_ptr = 1;
      }
    }

    if (ui_ptr) {
      int32_t                  px;
      int32_t                  py;
      uint32_t                 buttons;
      pm_metal_input_pointer_t ev;

      pm_metal_input_pointer_sample(&px, &py, &buttons);

      while (pm_metal_input_poll_pointer(&ev) != 0) {
        int32_t wx;
        int32_t wy;
        int32_t wheel;

        wx    = (ev.x >= 0) ? ev.x : px;
        wy    = (ev.y >= 0) ? ev.y : py;
        wheel = 0;
        if ((ev.flags & PM_METAL_INPUT_PTR_WHEEL) != 0) {
          wheel = ev.dy;
          if (wheel > 8) {
            wheel = 8;
          }

          if (wheel < -8) {
            wheel = -8;
          }
        }

        if (pm_metal_ui_console_pointer(wx, wy, ev.buttons, wheel, ev.flags)) {
          MetalShellMarkFull();
        }
      }

      /* Drag tracking between ring events (sample follows cursor). */
      if (pm_metal_ui_console_pointer(px, py, buttons, 0, 0)) {
        MetalShellMarkFull();
      }

      if (pm_metal_ui_pointer_hover(px, py)) {
        MetalShellMarkFull();
      }

      /* Cursor: dirty-rect only — never full chrome for pointer motion. */
      if (!mPrevPtrValid || px != mPrevPtrX || py != mPrevPtrY) {
        mPrevPtrX     = px;
        mPrevPtrY     = py;
        mPrevPtrValid = 1;
        pm_metal_ui_cursor_move(px, py);
      }

      if (pm_metal_ui_status_audio_pointer(px, py, buttons)) {
        mDirtyStatus = 1;
      }

      if ((buttons & 1u) != 0 && (mPrevPtrButtons & 1u) == 0) {
        if (pm_metal_ui_pointer_hit(px, py)) {
          MetalShellMarkFull();
        }
      }

      mPrevPtrButtons = buttons;
    } else {
      mPrevPtrButtons = 0;
      mPrevPtrValid   = 0;
      pm_metal_ui_cursor_hide();
      if (pm_metal_ui_pointer_hover(-1, -1)) {
        MetalShellMarkFull();
      }
    }
  }

  if (pm_metal_process_active()) {
    int32_t               st;
    int32_t               pr;
    char                  namebuf[64];
    const char           *nm;
    pm_metal_process_id_t pid;

    pid = pm_metal_process_current();
    nm  = pm_metal_process_name(pid);
    if (nm != NULL) {
      snprintf(namebuf, sizeof(namebuf), "%.*s", (int)(sizeof(namebuf) - 1), nm);
    } else {
      namebuf[0] = '?';
      namebuf[1] = '\0';
    }

    pr = pm_metal_process_poll(&st);
    if (pr != 0) {
      char msg[96];

      snprintf(msg,
               sizeof(msg),
               "process %u '%s': %s",
               (uint32_t)pid,
               namebuf,
               (pr > 0) ? "done" : "error");
      MetalShellEcho(msg);
      pm_metal_wasm_set_stdout_tab(PM_METAL_UI_HANDLE_INVALID);
      MetalShellMarkFull();
      (void)st;
    }
  }

  if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
    char                       ch;
    uint32_t                   n;
    pm_metal_input_key_event_t ke;

    /* Rings only — ConIn/i8042 live under input_poll. */
    for (;;) {
      n = pm_metal_console_read(&ch, 1);
      if (n == 0) {
        break;
      }

      MetalShellHandleAscii(ch, text, sizeof(text));
    }

    for (;;) {
      n = pm_metal_input_ps2_read(&ch, 1);
      if (n == 0) {
        break;
      }

      MetalShellHandleAscii(ch, text, sizeof(text));
    }

    while (pm_metal_input_poll_key_event(&ke) != 0) {
      if (ke.pressed == 0) {
        continue;
      }

      if (ke.code == PM_METAL_KEY_PAGEUP) {
        pm_metal_ui_console_scroll_page(1);
        MetalShellMarkFull();
      } else if (ke.code == PM_METAL_KEY_PAGEDOWN) {
        pm_metal_ui_console_scroll_page(-1);
        MetalShellMarkFull();
      } else if (ke.code == PM_METAL_KEY_UP) {
        if (pm_metal_ui_input_move_visual_row(-1) == 0) {
          MetalShellHistRecall(-1);
        } else {
          MetalShellMarkInput();
        }
      } else if (ke.code == PM_METAL_KEY_DOWN) {
        if (pm_metal_ui_input_move_visual_row(1) == 0) {
          MetalShellHistRecall(1);
        } else {
          MetalShellMarkInput();
        }
      } else if (ke.code == PM_METAL_KEY_LEFT) {
        (void)pm_metal_ui_input_move_cursor(-1);
        MetalShellMarkInput();
      } else if (ke.code == PM_METAL_KEY_RIGHT) {
        (void)pm_metal_ui_input_move_cursor(1);
        MetalShellMarkInput();
      } else if (ke.code == PM_METAL_KEY_HOME) {
        /* No per-row Home yet — commands are effectively one logical line. */
        (void)pm_metal_ui_input_move_cursor(-0x7fffffff);
        MetalShellMarkInput();
      } else if (ke.code == PM_METAL_KEY_END) {
        (void)pm_metal_ui_input_move_cursor(0x7fffffff);
        MetalShellMarkInput();
      } else if (ke.code == PM_METAL_KEY_DELETE) {
        if (pm_metal_ui_input_delete_fwd() == 0) {
          MetalShellMarkInput();
        }
      }
    }
  } else if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_GUEST) {
    MetalShellPollGuestKeys(now_ms);
  }

  /*
   * Shell focus: full chrome. Fullscreen guest (`run`): game owns FB — never
   * paint prompt/status over it (draw_surface alone used to flicker chrome
   * when status/net dirty landed mid-frame). Windowed guest (`tab`): keep
   * strip; paint skips wiping guest content.
   */
  {
    int32_t paint_chrome;

    paint_chrome = 0;
    if (MetalShellGuestFullscreen()) {
      /* Drop dirty; never blink/present the prompt over the game. */
      mDirty       = 0;
      mDirtyInput  = 0;
      mDirtyStatus = 0;
      paint_chrome = 0;
    } else if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_SHELL) {
      paint_chrome = 1;
    } else if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_GUEST) {
      /* Windowed guest: keep tab strip + input; cursor blink OK. */
      paint_chrome = 1;
    }

    if (paint_chrome) {
      int32_t blink;
      int32_t win_guest;

      win_guest = MetalShellGuestWindowedActive();
      blink     = ((now_ms - mLastFrameMs) >= 250u) ? 1 : 0;
      /*
       * Windowed guest: full chrome only when dirty (tab). Status tray and
       * input blink use dirty-rect presents so they don't fight the game.
       */
      if (mDirty || mDirtyStatus || (!win_guest && (mDirtyInput || blink))) {
        int32_t  px;
        int32_t  py;
        uint32_t buttons;
        int32_t  ix;
        int32_t  iy;
        int32_t  iw;
        int32_t  ih;

        if (mDirty) {
          pm_metal_ui_cursor_invalidate();
          (void)pm_metal_ui_frame();
          if (!pm_metal_input_pointer_locked()) {
            pm_metal_input_pointer_sample(&px, &py, &buttons);
            (void)buttons;
            pm_metal_ui_cursor_paint(px, py);
          }

          MetalShellPresentFull();
        } else if (mDirtyStatus) {
          pm_metal_ui_cursor_hide();
          (void)pm_metal_ui_paint_status();
          if (pm_metal_ui_status_rect(&ix, &iy, &iw, &ih) == 0) {
            (void)pm_metal_gfx_present_rect(ix, iy, iw, ih);
          }

          if (!pm_metal_input_pointer_locked()) {
            pm_metal_input_pointer_sample(&px, &py, &buttons);
            (void)buttons;
            pm_metal_ui_cursor_move(px, py);
          }
        } else {
          pm_metal_ui_cursor_hide();
          (void)pm_metal_ui_paint_shell_input();
          if (pm_metal_ui_shell_input_rect(&ix, &iy, &iw, &ih) == 0) {
            (void)pm_metal_gfx_present_rect(ix, iy, iw, ih);
          }

          if (!pm_metal_input_pointer_locked()) {
            pm_metal_input_pointer_sample(&px, &py, &buttons);
            (void)buttons;
            pm_metal_ui_cursor_move(px, py);
          }
        }

        mLastFrameMs = now_ms;
        mDirty       = 0;
        mDirtyInput  = 0;
        mDirtyStatus = 0;
      } else if (win_guest) {
        mDirtyInput = 0;
      }
    }
  }

  return mExitReq ? 1 : 0;
}

uint32_t pm_metal_shell_pump_sleep_ms(void)
{
  return mPumpSleepMs;
}

#include "wasm_export.h"

static void pm_metal_shell_log_native(wasm_exec_env_t exec_env, const char *line)
{
  wasm_module_inst_t inst;
  char               buf[256];
  uintptr_t          i;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (line == NULL || inst == NULL || !wasm_runtime_validate_native_addr(inst, (void *)line, 1)) {
    if (pm_metal_port_owned()) {
      pm_metal_shell_serial_log("metal-shell: log bad ptr");
    } else {
      pm_metal_logf("metal-shell: log bad ptr %p", line);
    }

    return;
  }

  for (i = 0; i + 1 < sizeof(buf); i++) {
    if (!wasm_runtime_validate_native_addr(inst, (void *)(line + i), 1)) {
      break;
    }

    if (line[i] == '\0') {
      break;
    }

    buf[i] = line[i];
  }

  buf[i] = '\0';
  pm_metal_shell_log(buf);
}

static void pm_metal_shell_set_status_native(wasm_exec_env_t exec_env, const char *text)
{
  (void)exec_env;
  pm_metal_shell_set_status(text);
}

static void pm_metal_shell_request_exit_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  pm_metal_shell_request_exit();
}

static int32_t pm_metal_shell_run_native(wasm_exec_env_t exec_env, const char *mod)
{
  (void)exec_env;
  return (int32_t)pm_metal_shell_run(mod);
}

static int32_t pm_metal_shell_tab_native(wasm_exec_env_t exec_env, const char *mod)
{
  (void)exec_env;
  return (int32_t)pm_metal_shell_tab(mod);
}

static NativeSymbol g_pm_metal_shell_native_symbols[] = {
  { "pm_metal_shell_log", (void *)pm_metal_shell_log_native, "($)", NULL },
  { "pm_metal_shell_set_status", (void *)pm_metal_shell_set_status_native, "($)", NULL },
  { "pm_metal_shell_request_exit", (void *)pm_metal_shell_request_exit_native, "()", NULL },
  { "pm_metal_shell_run", (void *)pm_metal_shell_run_native, "($)i", NULL },
  { "pm_metal_shell_tab", (void *)pm_metal_shell_tab_native, "($)i", NULL },
};

int pm_metal_shell_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_SHELL_WASI_MODULE,
                                     g_pm_metal_shell_native_symbols,
                                     sizeof(g_pm_metal_shell_native_symbols) /
                                       sizeof(g_pm_metal_shell_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
