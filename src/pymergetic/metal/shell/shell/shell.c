/** @file
  Interactive shell — rings/focus/present (shared host).
**/
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/ping.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/time/time.h>
#include <runtime/run/run.h>

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#define SHELL_LINE_MAX  120

STATIC INT32   mDirty;      /* full chrome */
STATIC INT32   mDirtyInput; /* shell input line only */
STATIC INT32   mExitReq;
STATIC INT32   mExitReboot;
STATIC UINT32  mPrevPtrButtons;
STATIC INT32   mPrevPtrX;
STATIC INT32   mPrevPtrY;
STATIC INT32   mPrevPtrValid;
STATIC UINT64  mLastFrameMs;
STATIC INT32   mNest;
STATIC UINT32  mPumpSleepMs;

/* Bash-like command history ring (oldest dropped at PM_METAL_SHELL_HISTORY_MAX). */
STATIC CHAR8   mHist[PM_METAL_SHELL_HISTORY_MAX][SHELL_LINE_MAX];
STATIC UINT32  mHistStart;   /* index of oldest retained entry */
STATIC UINT32  mHistCount;
STATIC INT32   mHistPos = -1; /* -1 = draft line; else 0..count-1 (0=oldest) */
STATIC CHAR8   mHistDraft[SHELL_LINE_MAX];

STATIC struct {
  INT32                    live;
  CHAR8                    kind[16]; /* "nslookup" needs 9; keep headroom */
  CHAR8                    detail[64];
  pm_metal_async_handle_t  task_h;
  pm_metal_async_handle_t  coro_h;
  UINT64                   deadline_us;
} mJob;

/* ASCII path: ESC [ A/B from serial/ConIn (VNC/QEMU often skips key events). */
STATIC UINT32  mEscSeq; /* 0=norm 1=ESC 2=CSI */

STATIC VOID MetalShellMarkFull (VOID);
STATIC VOID MetalShellMarkInput (VOID);
STATIC VOID MetalShellOfferPrompt (VOID);

STATIC INT32  mPromptPending = 1; /* show prompt after boot banner */

/**
 * Live fullscreen guest (`run doom`) owns the DEFAULT FB — shell must not
 * paint chrome/prompt over it. Windowed (`tab doom`) keeps the strip.
 */
STATIC
INT32
MetalShellGuestFullscreen (
  VOID
  )
{
  pm_metal_process_id_t  pid;

  pid = pm_metal_process_current ();
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    return 0;
  }

  if (!pm_metal_async_session_active ()) {
    return 0;
  }

  return (pm_metal_process_ui_kind (pid) == PM_METAL_PROC_UI_FULLSCREEN) ? 1 : 0;
}

/**
 * Live guest on the active tab (windowed). Tab strip/status stay; the
 * shared input blink must not fight the guest content present.
 */
STATIC
INT32
MetalShellGuestWindowedActive (
  VOID
  )
{
  pm_metal_process_id_t  pid;
  pm_metal_ui_handle_t   tab;

  pid = pm_metal_process_current ();
  if (pid == PM_METAL_PROCESS_ID_INVALID
      || !pm_metal_async_session_active ())
  {
    return 0;
  }

  if (pm_metal_process_ui_kind (pid) != PM_METAL_PROC_UI_TAB) {
    return 0;
  }

  tab = pm_metal_ui_tab_active ();
  return (tab != PM_METAL_UI_HANDLE_INVALID
          && tab == pm_metal_process_tab (pid)) ? 1 : 0;
}

STATIC
CONST CHAR8 *
MetalShellHistAt (
  UINT32  idx
  )
{
  if (idx >= mHistCount) {
    return NULL;
  }

  return mHist[(mHistStart + idx) % PM_METAL_SHELL_HISTORY_MAX];
}

void
pm_metal_shell_history_add (
  CONST CHAR8  *line
  )
{
  CONST CHAR8  *last;
  UINT32        slot;

  if (line == NULL || line[0] == '\0') {
    return;
  }

  last = MetalShellHistAt (mHistCount > 0u ? mHistCount - 1u : 0u);
  if (mHistCount > 0u && last != NULL && AsciiStrCmp (last, line) == 0) {
    return;
  }

  if (mHistCount < PM_METAL_SHELL_HISTORY_MAX) {
    slot = (mHistStart + mHistCount) % PM_METAL_SHELL_HISTORY_MAX;
    AsciiStrCpyS (mHist[slot], sizeof (mHist[slot]), line);
    mHistCount++;
  } else {
    slot = mHistStart;
    AsciiStrCpyS (mHist[slot], sizeof (mHist[slot]), line);
    mHistStart = (mHistStart + 1u) % PM_METAL_SHELL_HISTORY_MAX;
  }

  mHistPos = -1;
}

UINT32
pm_metal_shell_history_count (
  VOID
  )
{
  return mHistCount;
}

int
pm_metal_shell_history_get (
  UINT32  idx,
  CHAR8  *out,
  UINT32  cap
  )
{
  CONST CHAR8  *src;

  if (out == NULL || cap == 0) {
    return -1;
  }

  src = MetalShellHistAt (idx);
  if (src == NULL) {
    out[0] = '\0';
    return -1;
  }

  AsciiStrCpyS (out, cap, src);
  return 0;
}

STATIC
VOID
MetalShellHistRecall (
  INT32  dir
  )
{
  if (mHistCount == 0u) {
    return;
  }

  if (dir < 0) {
    /* Up → older */
    if (mHistPos < 0) {
      if (pm_metal_ui_input_text (mHistDraft, sizeof (mHistDraft)) < 0) {
        mHistDraft[0] = '\0';
      }

      mHistPos = (INT32)mHistCount - 1;
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

    if ((UINT32)mHistPos + 1u < mHistCount) {
      mHistPos++;
    } else {
      mHistPos = -1;
      (VOID)pm_metal_ui_input_set (mHistDraft);
      MetalShellMarkInput ();
      return;
    }
  }

  {
    CONST CHAR8  *line;

    line = MetalShellHistAt ((UINT32)mHistPos);
    if (line != NULL) {
      (VOID)pm_metal_ui_input_set (line);
      MetalShellMarkInput ();
    }
  }
}

/** Ctrl+Shift+Left/Right → cycle tabs (consume; works under guest focus too). */
STATIC
INT32
MetalShellTabChordFilter (
  CONST pm_metal_input_key_event_t  *ev
  )
{
  INT32  delta;

  if (ev == NULL || ev->pressed == 0) {
    return 0;
  }

  if ((ev->code != PM_METAL_KEY_LEFT && ev->code != PM_METAL_KEY_RIGHT)
      || (ev->mods & PM_METAL_INPUT_MOD_CTRL) == 0
      || (ev->mods & PM_METAL_INPUT_MOD_SHIFT) == 0)
  {
    return 0;
  }

  delta = (ev->code == PM_METAL_KEY_LEFT) ? -1 : 1;
  if (pm_metal_ui_tab_cycle (delta) == 0) {
    CHAR8  msg[40];

    AsciiSPrint (
      msg,
      sizeof (msg),
      "active tab %u",
      pm_metal_ui_tab_active_index ()
      );
    pm_metal_ui_set_status (msg);
    MetalShellMarkFull ();
  }

  return 1;
}

STATIC VOID
MetalShellJobFinish (
  INT32  st
  )
{
  CHAR8  msg[96];

  if (AsciiStrCmp (mJob.kind, "ping") == 0) {
    if (st != (INT32)PM_METAL_DONE) {
      UINT32  err;

      err = pm_metal_net_ping_last_err ();
      if (err == PM_METAL_NET_PING_ERR_RESOLVE) {
        pm_metal_shell_out ("ping: resolve failed");
      } else if (err == PM_METAL_NET_PING_ERR_TIMEOUT) {
        pm_metal_shell_out ("ping: no reply");
      } else if (err == PM_METAL_NET_PING_ERR_NOROUTE) {
        pm_metal_shell_out ("ping: no route");
      } else if (err == PM_METAL_NET_PING_ERR_NOMEM) {
        pm_metal_shell_out ("ping: out of memory");
      } else if (err == PM_METAL_NET_PING_ERR_SEND) {
        pm_metal_shell_out ("ping: send failed");
      } else if (st == (INT32)PM_METAL_CANCELLED) {
        pm_metal_shell_out ("ping: cancelled");
      } else {
        pm_metal_shell_out ("ping: failed");
      }
    } else {
      UINT32  us;

      /* DONE ⇒ echo received; don't treat sub-ms (floored 0 ms) as failure. */
      us = pm_metal_net_ping_rtt_us (mJob.coro_h);
      AsciiSPrint (
        msg,
        sizeof (msg),
        "ping %a: %u.%u ms",
        mJob.detail,
        us / 1000u,
        (us / 100u) % 10u
        );
      pm_metal_shell_out (msg);
    }
  } else if (AsciiStrCmp (mJob.kind, "nslookup") == 0) {
    if (st != (INT32)PM_METAL_DONE
        || (UINT32)(UINTN)pm_metal_async_result_u32 (mJob.coro_h) == 0u)
    {
      AsciiSPrint (msg, sizeof (msg), "nslookup: %a failed", mJob.detail);
      pm_metal_shell_out (msg);
    } else {
      CHAR8  ip[64];

      if (pm_metal_net_dns_last_ntoa (ip, sizeof (ip)) != 0) {
        pm_metal_shell_out ("nslookup: no address");
      } else {
        AsciiSPrint (msg, sizeof (msg), "%a -> %a", mJob.detail, ip);
        pm_metal_shell_out (msg);
      }
    }
  } else if (AsciiStrCmp (mJob.kind, "test") == 0) {
    if (st == (INT32)PM_METAL_DONE && pm_metal_boot_tests_result (mJob.coro_h) == 0) {
      pm_metal_shell_out ("test: ok");
    } else {
      pm_metal_shell_out ("test: FAILED");
    }
  }

  ZeroMem (&mJob, sizeof (mJob));
  MetalShellMarkFull ();
  MetalShellOfferPrompt ();
}

int
pm_metal_shell_job_busy (
  VOID
  )
{
  return mJob.live ? 1 : 0;
}

void
pm_metal_shell_prompt_dirty (
  VOID
  )
{
  /* Don't steal the line while a job owns the next OfferPrompt. */
  if (!mJob.live
      && pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL)
  {
    mPromptPending = 1;
  }
}

int
pm_metal_shell_job_start (
  CONST CHAR8              *kind,
  pm_metal_async_handle_t   task_h,
  pm_metal_async_handle_t   coro_h,
  CONST CHAR8              *detail,
  uint64_t                  deadline_us
  )
{
  if (kind == NULL || task_h == PM_METAL_ASYNC_HANDLE_INVALID || mJob.live) {
    return -1;
  }

  ZeroMem (&mJob, sizeof (mJob));
  AsciiStrCpyS (mJob.kind, sizeof (mJob.kind), kind);
  mJob.detail[0] = '\0';
  if (detail != NULL) {
    AsciiStrnCpyS (
      mJob.detail,
      sizeof (mJob.detail),
      detail,
      sizeof (mJob.detail) - 1u
      );
    mJob.detail[sizeof (mJob.detail) - 1u] = '\0';
  }

  mJob.task_h      = task_h;
  mJob.coro_h      = coro_h;
  mJob.deadline_us = deadline_us;
  mJob.live        = 1;
  mPumpSleepMs     = 1u;
  return 0;
}

STATIC VOID
MetalShellJobPoll (
  VOID
  )
{
  INT32  st;

  if (!mJob.live) {
    return;
  }

  st = pm_metal_async_task_status (mJob.task_h);
  /* WAITING = parked on sleep/DNS/I/O — still live (not terminal). */
  if (st == (INT32)PM_METAL_PENDING || st == (INT32)PM_METAL_WAITING) {
    if (mJob.deadline_us != 0
        && pm_metal_time_mono_us () >= mJob.deadline_us)
    {
      pm_metal_async_task_cancel (mJob.task_h);
      if (AsciiStrCmp (mJob.kind, "ping") == 0) {
        pm_metal_shell_out ("ping: timeout");
      } else if (AsciiStrCmp (mJob.kind, "nslookup") == 0) {
        pm_metal_shell_out ("nslookup: timeout");
      } else {
        pm_metal_shell_out ("test: FAILED");
      }

      ZeroMem (&mJob, sizeof (mJob));
      MetalShellMarkFull ();
      MetalShellOfferPrompt ();
    }

    mPumpSleepMs = 1u;
    return;
  }

  MetalShellJobFinish (st);
}

STATIC VOID
MetalShellMarkFull (
  VOID
  )
{
  mDirty       = 1;
  mDirtyInput  = 0;
  mPumpSleepMs = 1;
}

STATIC VOID
MetalShellMarkInput (
  VOID
  )
{
  if (!mDirty) {
    mDirtyInput = 1;
  }

  mPumpSleepMs = 1;
}

void
pm_metal_shell_serial_log (
  CONST CHAR8  *line
  )
{
  /* Unified log owns UEFI/UART/UI sinks (incl. COM1 after UART attach). */
  pm_metal_log (line);
}

STATIC
VOID
MetalShellPollGuestKeys (
  UINT64  now_ms
  )
{
  /* BIOS: i8042 make/break already pushed in input_poll; expire holds. */
  pm_metal_input_tick (now_ms);
}

STATIC
VOID
MetalShellEcho (
  CONST CHAR8  *line
  )
{
  /*
   * Unified log owns sinks (UEFI/UART/UI viewports). Still mirror into
   * the active guest tab when not in guest focus.
   */
  if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL) {
    if (pm_metal_ui_tab_active_index () != 0) {
      pm_metal_ui_active_puts (line);
    }

    MetalShellMarkFull ();
  }

  pm_metal_log (line);
}

void
pm_metal_shell_log (
  CONST CHAR8  *line
  )
{
  pm_metal_stream_h  out;

  out = pm_metal_stdio_out ();
  if (out != PM_METAL_STREAM_INVALID) {
    (VOID)pm_metal_stream_write_line (out, line);
    /* Keep serial for verify markers; ConOut only before EBS. */
    pm_metal_shell_serial_log (line);

    if (!MetalShellGuestFullscreen ()) {
      MetalShellMarkFull ();
    }

    return;
  }

  MetalShellEcho (line);
}

void
pm_metal_shell_set_status (
  CONST CHAR8  *text
  )
{
  /* Avoid chrome redraw while a guest owns the framebuffer. */
  if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  pm_metal_ui_set_status (text);
  MetalShellMarkFull ();
}

void
pm_metal_shell_request_exit (
  VOID
  )
{
  mExitReq    = 1;
  mExitReboot = 0;
}

int
pm_metal_shell_exit_reboot (
  VOID
  )
{
  return mExitReboot ? 1 : 0;
}

STATIC
VOID
MetalShellEchoLines (
  CONST CHAR8  *text
  )
{
  CHAR8  line[200];
  UINTN  li;
  UINTN  i;

  if (text == NULL) {
    return;
  }

  li = 0;
  for (i = 0; ; i++) {
    if (text[i] == '\n' || text[i] == '\0') {
      line[li] = '\0';
      if (li > 0) {
        MetalShellEcho (line);
      }

      li = 0;
      if (text[i] == '\0') {
        break;
      }
    } else if (li + 1 < sizeof (line)) {
      line[li++] = text[i];
    }
  }
}

UINT32
pm_metal_shell_prompt (
  CHAR8   *out,
  UINT32   cap
  )
{
  CONST CHAR8  *host;
  UINTN         n;

  if (out == NULL || cap < 4u) {
    return 0;
  }

  host = pm_metal_host_name_cstr ();
  if (host == NULL || host[0] == '\0') {
    host = "metal";
  }

  /* bash-like: hostname:~$  (no multi-user; ~ = shell home) */
  n = AsciiSPrint (out, cap, "%a:~$ ", host);
  if (n == 0 || n >= cap) {
    AsciiStrCpyS (out, cap, "$ ");
    return 2;
  }

  return (UINT32)n;
}

/**
 * Colored prompt for COM1 / UI scrollback.
 * Space is AFTER the final reset — terminals often drop a trailing space
 * that sits inside an SGR segment (showed up as "metal:~$help").
 */
STATIC
UINT32
MetalShellPromptAnsi (
  CHAR8   *out,
  UINT32   cap
  )
{
  CONST CHAR8  *host;
  UINTN         n;

  if (out == NULL || cap < 8u) {
    return 0;
  }

  host = pm_metal_host_name_cstr ();
  if (host == NULL || host[0] == '\0') {
    host = "metal";
  }

  /* bold green host, bold blue :~, bold green $, reset, then space */
  n = AsciiSPrint (
        out,
        cap,
        "\033[1;32m%a\033[0m\033[1;34m:~\033[0m\033[1;32m$\033[0m ",
        host
        );
  if (n == 0 || n >= cap) {
    return 0;
  }

  return (UINT32)n;
}

/**
 * Live prompt for serial (COM1) + refresh the UI input strip.
 * Scrollback only gets prompt+line when a command is committed.
 */
STATIC
VOID
MetalShellOfferPrompt (
  VOID
  )
{
  CHAR8   ps[PM_METAL_HOST_NAME_MAX + 48];
  UINT32  n;

  n = MetalShellPromptAnsi (ps, sizeof (ps));
  if (n > 0u) {
    pm_metal_console_com1_write (ps, n);
  }

  mPromptPending = 0;
  MetalShellMarkInput ();
}

void
pm_metal_shell_out (
  CONST CHAR8  *line
  )
{
  MetalShellEcho (line);
}

void
pm_metal_shell_out_lines (
  CONST CHAR8  *text
  )
{
  MetalShellEchoLines (text);
}

void
pm_metal_shell_mark_full (
  VOID
  )
{
  MetalShellMarkFull ();
}

void
pm_metal_shell_cmd_exit (
  INT32  reboot
  )
{
  mExitReboot = reboot ? 1 : 0;
  mExitReq    = 1;
}

int
pm_metal_shell_run (
  CONST CHAR8  *mod
  )
{
  INT32                  rc;
  CHAR8                  msg[96];
  pm_metal_ui_handle_t   tab;

  if (mod == NULL || mod[0] == '\0') {
    MetalShellEcho ("usage: run <mod>");
    return -1;
  }

  if (mNest > 0) {
    MetalShellEcho ("run: nested run refused");
    return -1;
  }

  if (!pm_metal_wasm_ready ()) {
    MetalShellEcho ("run: wasm runtime not ready");
    return -1;
  }

  tab = pm_metal_ui_tab_active ();
  mNest = 1;
  rc    = pm_metal_process_spawn_mod (mod, PM_METAL_PROC_UI_FULLSCREEN, tab);
  mNest = 0;

  if (rc < 0) {
    /*
     * Common miss: ESP package not preloaded (rebuild/restage with
     * METAL_DOOM_BUILD=1). Detailed reason already on the log ring.
     */
    AsciiSPrint (msg, sizeof (msg), "run '%a': not found / load failed", mod);
  } else if (pm_metal_process_active ()) {
    AsciiSPrint (
      msg,
      sizeof (msg),
      "run '%a': process %u live",
      mod,
      (UINT32)pm_metal_process_current ()
      );
  } else {
    AsciiSPrint (msg, sizeof (msg), "run '%a': exited %d", mod, rc);
  }

  /* Serial/log only — fullscreen guest owns the FB; no chrome dirty. */
  pm_metal_log (msg);
  if (!MetalShellGuestFullscreen ()) {
    MetalShellEcho (msg);
    pm_metal_ui_set_status (msg);
    MetalShellMarkFull ();
  }

  return rc;
}

int
pm_metal_shell_tab (
  CONST CHAR8  *mod
  )
{
  pm_metal_ui_handle_t  tab;
  INT32                 rc;
  CHAR8                 msg[96];

  if (mod == NULL || mod[0] == '\0') {
    MetalShellEcho ("usage: tab <mod>");
    return -1;
  }

  if (mNest > 0) {
    MetalShellEcho ("tab: nested run refused");
    return -1;
  }

  if (!pm_metal_wasm_ready ()) {
    MetalShellEcho ("tab: wasm runtime not ready");
    return -1;
  }

  tab = pm_metal_ui_tab_open (mod, 1);
  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    MetalShellEcho ("tab: failed to open");
    return -1;
  }

  mNest = 1;
  rc    = pm_metal_process_spawn_mod (mod, PM_METAL_PROC_UI_TAB, tab);
  mNest = 0;

  if (rc < 0) {
    AsciiSPrint (msg, sizeof (msg), "tab '%a': host error - close when done", mod);
  } else if (pm_metal_process_active ()) {
    AsciiSPrint (
      msg,
      sizeof (msg),
      "tab '%a': process %u live",
      mod,
      (UINT32)pm_metal_process_current ()
      );
  } else {
    AsciiSPrint (msg, sizeof (msg), "tab '%a': exited %d - close when done", mod, rc);
  }

  pm_metal_ui_tab_puts (tab, msg);
  MetalShellEcho (msg);
  pm_metal_ui_set_status (msg);
  MetalShellMarkFull ();
  return rc;
}

int
pm_metal_shell_init (
  VOID
  )
{
  mDirty          = 1;
  mDirtyInput     = 0;
  mExitReq        = 0;
  mExitReboot     = 0;
  mLastFrameMs    = 0;
  mNest           = 0;
  mPumpSleepMs    = 1u;
  mPrevPtrButtons = 0;
  mPrevPtrValid   = 0;
  ZeroMem (&mJob, sizeof (mJob));

  pm_metal_ui_set_status ("shell ready");
  pm_metal_ui_input_clear ();
  pm_metal_input_set_filter (MetalShellTabChordFilter);
  pm_metal_shell_cmds_install ();
  (VOID)pm_metal_ui_frame ();
  (VOID)pm_metal_gfx_present ();
  return 0;
}

STATIC
VOID
MetalShellHandleAscii (
  CHAR8  ch,
  CHAR8  *text,
  UINTN  text_sz
  )
{
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
      MetalShellHistRecall (-1);
      return;
    }

    if (ch == 'B') {
      MetalShellHistRecall (1);
      return;
    }

    /* C/D = right/left — no cursor move yet; swallow. */
    if (ch == 'C' || ch == 'D') {
      return;
    }

    return;
  }

  if (ch == 0x1b) {
    mEscSeq = 1u;
    return;
  }

  if (ch == '\r' || ch == '\n') {
    CHAR8  nl;
    CHAR8  crlf[2];

    mEscSeq = 0u;
    crlf[0] = '\r';
    crlf[1] = '\n';
    pm_metal_console_com1_write (crlf, 2);

    nl = '\n';
    (VOID)pm_metal_stream_feed_stdin (&nl, 1);
    if (pm_metal_ui_input_text (text, text_sz) < 0) {
      text[0] = '\0';
    }

    {
      CHAR8   echo[SHELL_LINE_MAX + PM_METAL_HOST_NAME_MAX + 64];
      UINT32  plen;

      /*
       * UI scrollback gets ANSI prompt + command (paint understands SGR).
       * Serial already showed OfferPrompt + typed chars — don't log again.
       */
      plen = MetalShellPromptAnsi (echo, sizeof (echo));
      if (plen > 0u && plen + 1u < sizeof (echo)) {
        AsciiStrCpyS (&echo[plen], sizeof (echo) - plen, text);
        pm_metal_ui_console_puts (echo);
      } else {
        plen = pm_metal_shell_prompt (echo, sizeof (echo));
        if (plen + 1u < sizeof (echo)) {
          AsciiStrCpyS (&echo[plen], sizeof (echo) - plen, text);
        }

        pm_metal_ui_console_puts (echo);
      }
    }

    pm_metal_shell_history_add (text);
    pm_metal_shell_cmd_dispatch (text);
    pm_metal_ui_input_clear ();
    mHistPos = -1;
    /*
     * Fullscreen guest (`run doom`) owns the FB — do not dirty chrome or
     * re-offer the shell prompt over it. Windowed / idle: normal prompt.
     */
    if (MetalShellGuestFullscreen ()) {
      return;
    }

    MetalShellMarkFull ();
    /* Next line on COM1 + input strip (after command output). */
    MetalShellOfferPrompt ();
    return;
  }

  if (ch == 0x7f || ch == 0x08) {
    CONST CHAR8  bs[3] = { '\b', ' ', '\b' };

    pm_metal_console_com1_write (bs, 3);
    (VOID)pm_metal_ui_input_backspace ();
    MetalShellMarkInput ();
    return;
  }

  if (ch >= 32 && ch < 127) {
    pm_metal_console_com1_write (&ch, 1);
    (VOID)pm_metal_stream_feed_stdin (&ch, 1);
    (VOID)pm_metal_ui_input_append (ch);
    MetalShellMarkInput ();
  }
}

int
pm_metal_shell_poll (
  VOID
  )
{
  UINT64  now_ms;
  CHAR8   text[SHELL_LINE_MAX];

  now_ms = pm_metal_time_mono_us () / 1000u;
  /*
   * Guest frame pacing awaits sleep_until; a 16 ms host nap adds almost a
   * whole frame of wake latency on iron. Keep idle at 16 ms.
   */
  mPumpSleepMs = pm_metal_process_active () ? 1u : 16u;
  if (!MetalShellGuestFullscreen () && pm_metal_ui_tick (now_ms)) {
    MetalShellMarkFull ();
  }

  /* After boot banner: first live prompt on serial + input strip. */
  if (mPromptPending
      && pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL)
  {
    MetalShellOfferPrompt ();
  }

  /* Drain HW into rings before shell/async consumers (port-owned). */
  pm_metal_input_poll ();

  pm_metal_net_poll ();
  pm_metal_audio_poll ();
  pm_metal_console_poll ();

  /* Always pump host tasks (ping/test) — not only when a wasm session lives. */
  pm_metal_run_poll_all ();
  MetalShellJobPoll ();

  /*
   * Unlocked pointer → chrome: cursor, tab hover/click, console scroll.
   * Shell focus always; windowed guests keep the strip; fullscreen owns FB.
   */
  {
    INT32  ui_ptr;

    ui_ptr = 0;
    if (!pm_metal_input_pointer_locked () && !MetalShellGuestFullscreen ()) {
      if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL) {
        ui_ptr = 1;
      } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
        ui_ptr = 1;
      }
    }

    if (ui_ptr) {
      INT32                     px;
      INT32                     py;
      UINT32                    buttons;
      pm_metal_input_pointer_t  ev;

      pm_metal_input_pointer_sample (&px, &py, &buttons);

      while (pm_metal_input_poll_pointer (&ev) != 0) {
        INT32   wx;
        INT32   wy;
        INT32   wheel;

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

        if (pm_metal_ui_console_pointer (wx, wy, ev.buttons, wheel, ev.flags)) {
          MetalShellMarkFull ();
        }
      }

      /* Drag tracking between ring events (sample follows cursor). */
      if (pm_metal_ui_console_pointer (px, py, buttons, 0, 0)) {
        MetalShellMarkFull ();
      }

      if (pm_metal_ui_pointer_hover (px, py)) {
        MetalShellMarkFull ();
      }

      /* Cursor: dirty-rect only — never full chrome for pointer motion. */
      if (!mPrevPtrValid || px != mPrevPtrX || py != mPrevPtrY) {
        mPrevPtrX     = px;
        mPrevPtrY     = py;
        mPrevPtrValid = 1;
        pm_metal_ui_cursor_move (px, py);
      }

      if ((buttons & 1u) != 0 && (mPrevPtrButtons & 1u) == 0) {
        if (pm_metal_ui_pointer_hit (px, py)) {
          MetalShellMarkFull ();
        }
      }

      mPrevPtrButtons = buttons;
    } else {
      mPrevPtrButtons = 0;
      mPrevPtrValid   = 0;
      pm_metal_ui_cursor_hide ();
      if (pm_metal_ui_pointer_hover (-1, -1)) {
        MetalShellMarkFull ();
      }
    }
  }

  if (pm_metal_process_active () || pm_metal_wasm_session_active ()) {
    INT32                   st;
    INT32                   pr;
    CHAR8                   namebuf[64];
    CONST CHAR8            *nm;
    pm_metal_process_id_t   pid;

    pid = pm_metal_process_current ();
    nm  = pm_metal_process_name (pid);
    if (nm == NULL) {
      nm = pm_metal_wasm_session_name ();
    }

    if (nm != NULL) {
      AsciiStrnCpyS (namebuf, sizeof (namebuf), nm, sizeof (namebuf) - 1);
    } else {
      namebuf[0] = '?';
      namebuf[1] = '\0';
    }

    pr = pm_metal_process_poll (&st);
    if (pr != 0) {
      CHAR8  msg[96];

      AsciiSPrint (
        msg,
        sizeof (msg),
        "process %u '%a': %a",
        (UINT32)pid,
        namebuf,
        (pr > 0) ? "done" : "error"
        );
      MetalShellEcho (msg);
      pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
      MetalShellMarkFull ();
      (VOID)st;
    }
  }

  if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL) {
    CHAR8                      ch;
    UINT32                     n;
    pm_metal_input_key_event_t ke;

    /* Rings only — ConIn/i8042 live under input_poll. */
    for (;;) {
      n = pm_metal_console_read (&ch, 1);
      if (n == 0) {
        break;
      }

      MetalShellHandleAscii (ch, text, sizeof (text));
    }

    for (;;) {
      n = pm_metal_input_ps2_read (&ch, 1);
      if (n == 0) {
        break;
      }

      MetalShellHandleAscii (ch, text, sizeof (text));
    }

    while (pm_metal_input_poll_key_event (&ke) != 0) {
      if (ke.pressed == 0) {
        continue;
      }

      if (ke.code == PM_METAL_KEY_PAGEUP) {
        pm_metal_ui_console_scroll_page (1);
        MetalShellMarkFull ();
      } else if (ke.code == PM_METAL_KEY_PAGEDOWN) {
        pm_metal_ui_console_scroll_page (-1);
        MetalShellMarkFull ();
      } else if (ke.code == PM_METAL_KEY_UP) {
        MetalShellHistRecall (-1);
      } else if (ke.code == PM_METAL_KEY_DOWN) {
        MetalShellHistRecall (1);
      }
    }
  } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
    MetalShellPollGuestKeys (now_ms);
  }

  /*
   * Shell focus: full chrome. Fullscreen guest (`run`): game owns FB — never
   * paint prompt/status over it (draw_surface alone used to flicker chrome
   * when status/net dirty landed mid-frame). Windowed guest (`tab`): keep
   * strip; paint skips wiping guest content.
   */
  {
    INT32  paint_chrome;

    paint_chrome = 0;
    if (MetalShellGuestFullscreen ()) {
      /* Drop dirty; never blink/present the prompt over the game. */
      mDirty      = 0;
      mDirtyInput = 0;
      paint_chrome = 0;
    } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL) {
      paint_chrome = 1;
    } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
      /* Windowed guest: keep tab strip + input; cursor blink OK. */
      paint_chrome = 1;
    }

    if (paint_chrome) {
      INT32  blink;
      INT32  win_guest;

      win_guest = MetalShellGuestWindowedActive ();
      blink     = ((now_ms - mLastFrameMs) >= 250u) ? 1 : 0;
      /*
       * Windowed guest: full chrome only when dirty (tab/status). Skip the
       * 250 ms input-cursor blink present — it fought the game present and
       * flashed the prompt through the content.
       */
      if (mDirty || (!win_guest && (mDirtyInput || blink))) {
        INT32   px;
        INT32   py;
        UINT32  buttons;
        INT32   ix;
        INT32   iy;
        INT32   iw;
        INT32   ih;

        if (mDirty) {
          pm_metal_ui_cursor_invalidate ();
          (VOID)pm_metal_ui_frame ();
          if (!pm_metal_input_pointer_locked ()) {
            pm_metal_input_pointer_sample (&px, &py, &buttons);
            (VOID)buttons;
            pm_metal_ui_cursor_paint (px, py);
          }

          (VOID)pm_metal_gfx_present ();
        } else {
          pm_metal_ui_cursor_hide ();
          (VOID)pm_metal_ui_paint_shell_input ();
          if (pm_metal_ui_shell_input_rect (&ix, &iy, &iw, &ih) == 0) {
            (VOID)pm_metal_gfx_present_rect (ix, iy, iw, ih);
          }

          if (!pm_metal_input_pointer_locked ()) {
            pm_metal_input_pointer_sample (&px, &py, &buttons);
            (VOID)buttons;
            pm_metal_ui_cursor_move (px, py);
          }
        }

        mLastFrameMs = now_ms;
        mDirty       = 0;
        mDirtyInput  = 0;
      } else if (win_guest) {
        mDirtyInput = 0;
      }
    }
  }

  return mExitReq ? 1 : 0;
}

uint32_t
pm_metal_shell_pump_sleep_ms (
  VOID
  )
{
  return mPumpSleepMs;
}

#include "wasm_export.h"

STATIC VOID
pm_metal_shell_log_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *line
  )
{
  wasm_module_inst_t  inst;
  CHAR8               buf[256];
  UINTN               i;

  inst = wasm_runtime_get_module_inst (exec_env);
  if (line == NULL || inst == NULL
      || !wasm_runtime_validate_native_addr (inst, (VOID *)line, 1))
  {
    if (pm_metal_port_owned ()) {
      pm_metal_shell_serial_log ("metal-shell: log bad ptr");
    } else {
      Print (L"metal-shell: log bad ptr %p\r\n", line);
    }

    return;
  }

  for (i = 0; i + 1 < sizeof (buf); i++) {
    if (!wasm_runtime_validate_native_addr (inst, (VOID *)(line + i), 1)) {
      break;
    }

    if (line[i] == '\0') {
      break;
    }

    buf[i] = line[i];
  }

  buf[i] = '\0';
  pm_metal_shell_log (buf);
}

STATIC VOID
pm_metal_shell_set_status_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *text
  )
{
  (VOID)exec_env;
  pm_metal_shell_set_status (text);
}

STATIC VOID
pm_metal_shell_request_exit_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  pm_metal_shell_request_exit ();
}

STATIC INT32
pm_metal_shell_run_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *mod
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_shell_run (mod);
}

STATIC INT32
pm_metal_shell_tab_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *mod
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_shell_tab (mod);
}

STATIC NativeSymbol g_pm_metal_shell_native_symbols[] = {
  { "pm_metal_shell_log", (VOID *)pm_metal_shell_log_native, "($)", NULL },
  { "pm_metal_shell_set_status", (VOID *)pm_metal_shell_set_status_native, "($)", NULL },
  { "pm_metal_shell_request_exit", (VOID *)pm_metal_shell_request_exit_native, "()", NULL },
  { "pm_metal_shell_run", (VOID *)pm_metal_shell_run_native, "($)i", NULL },
  { "pm_metal_shell_tab", (VOID *)pm_metal_shell_tab_native, "($)i", NULL },
};

int
pm_metal_shell_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_SHELL_WASI_MODULE,
         g_pm_metal_shell_native_symbols,
         sizeof (g_pm_metal_shell_native_symbols)
           / sizeof (g_pm_metal_shell_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
