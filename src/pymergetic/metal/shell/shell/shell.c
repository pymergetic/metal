/** @file
  Interactive shell — rings/focus/present (shared host).
**/
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/ping.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/port.h>
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
STATIC UINT64  mLastFrameMs;
STATIC INT32   mNest;
STATIC UINT32  mPumpSleepMs;

STATIC struct {
  INT32                    live;
  CHAR8                    kind[8];
  CHAR8                    detail[64];
  pm_metal_async_handle_t  task_h;
  pm_metal_async_handle_t  coro_h;
  UINT64                   deadline_us;
} mJob;

STATIC VOID MetalShellMarkFull (VOID);

STATIC VOID
MetalShellJobFinish (
  INT32  st
  )
{
  CHAR8  msg[96];

  if (AsciiStrCmp (mJob.kind, "ping") == 0) {
    if (st != (INT32)PM_METAL_DONE) {
      pm_metal_shell_out ("ping: resolve/send failed");
    } else {
      UINT32  rtt;

      rtt = pm_metal_net_ping_rtt_ms (mJob.coro_h);
      if (rtt == 0u) {
        pm_metal_shell_out ("ping: no reply");
      } else {
        AsciiSPrint (msg, sizeof (msg), "ping %a: %u ms", mJob.detail, rtt);
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
}

int
pm_metal_shell_job_busy (
  VOID
  )
{
  return mJob.live ? 1 : 0;
}

int
pm_metal_shell_job_start (
  CONST CHAR8              *kind,
  pm_metal_async_handle_t   task_h,
  pm_metal_async_handle_t   coro_h,
  CONST CHAR8              *detail,
  UINT64                    deadline_us
  )
{
  if (kind == NULL || task_h == PM_METAL_ASYNC_HANDLE_INVALID || mJob.live) {
    return -1;
  }

  ZeroMem (&mJob, sizeof (mJob));
  AsciiStrCpyS (mJob.kind, sizeof (mJob.kind), kind);
  if (detail != NULL) {
    AsciiStrCpyS (mJob.detail, sizeof (mJob.detail), detail);
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
  if (st == (INT32)PM_METAL_PENDING) {
    if (mJob.deadline_us != 0
        && pm_metal_time_mono_us () >= mJob.deadline_us)
    {
      pm_metal_async_task_cancel (mJob.task_h);
      if (AsciiStrCmp (mJob.kind, "ping") == 0) {
        pm_metal_shell_out ("ping: timeout");
      } else {
        pm_metal_shell_out ("test: FAILED");
      }

      ZeroMem (&mJob, sizeof (mJob));
      MetalShellMarkFull ();
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

    MetalShellMarkFull ();
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
  pm_metal_wasm_set_stdout_tab (tab);
  mNest = 1;
  rc    = pm_metal_wasm_run_mod (mod);
  mNest = 0;
  if (!pm_metal_wasm_session_active ()) {
    pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
  }

  if (rc < 0) {
    AsciiSPrint (msg, sizeof (msg), "run '%a': host error", mod);
  } else if (pm_metal_wasm_session_active ()) {
    AsciiSPrint (msg, sizeof (msg), "run '%a': async session live", mod);
  } else {
    AsciiSPrint (msg, sizeof (msg), "run '%a': exited %d", mod, rc);
  }

  MetalShellEcho (msg);
  pm_metal_ui_set_status (msg);
  MetalShellMarkFull ();
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

  pm_metal_wasm_set_stdout_tab (tab);
  mNest = 1;
  rc    = pm_metal_wasm_run_mod (mod);
  mNest = 0;
  if (!pm_metal_wasm_session_active ()) {
    pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
  }

  if (rc < 0) {
    AsciiSPrint (msg, sizeof (msg), "tab '%a': host error - close when done", mod);
  } else if (pm_metal_wasm_session_active ()) {
    AsciiSPrint (msg, sizeof (msg), "tab '%a': async session live", mod);
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
  mDirty       = 1;
  mDirtyInput  = 0;
  mExitReq     = 0;
  mExitReboot  = 0;
  mLastFrameMs = 0;
  mNest        = 0;
  mPumpSleepMs = 1u;
  ZeroMem (&mJob, sizeof (mJob));

  pm_metal_ui_set_status ("shell ready");
  pm_metal_ui_input_clear ();
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
  if (ch == '\r' || ch == '\n') {
    CHAR8  nl;
    CHAR8  crlf[2];

    crlf[0] = '\r';
    crlf[1] = '\n';
    pm_metal_console_com1_write (crlf, 2);

    nl = '\n';
    (VOID)pm_metal_stream_feed_stdin (&nl, 1);
    if (pm_metal_ui_input_text (text, text_sz) < 0) {
      text[0] = '\0';
    }

    {
      CHAR8  echo[SHELL_LINE_MAX + 4];

      AsciiSPrint (echo, sizeof (echo), "> %a", text);
      MetalShellEcho (echo);
    }

    pm_metal_shell_cmd_dispatch (text);
    pm_metal_ui_input_clear ();
    MetalShellMarkFull ();
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
  mPumpSleepMs = 16u;
  if (pm_metal_ui_tick (now_ms)) {
    MetalShellMarkFull ();
  }

  /* Drain HW into rings before shell/async consumers (port-owned). */
  pm_metal_input_poll ();

  pm_metal_net_poll ();
  pm_metal_audio_poll ();
  pm_metal_console_poll ();

  /* Always pump host tasks (ping/test) — not only when a wasm session lives. */
  pm_metal_run_poll_all ();
  MetalShellJobPoll ();

  if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL && !pm_metal_input_pointer_locked ()) {
    INT32    px;
    INT32    py;
    UINT32   buttons;

    pm_metal_input_pointer_sample (&px, &py, &buttons);
    if ((buttons & 1u) != 0 && (mPrevPtrButtons & 1u) == 0) {
      if (pm_metal_ui_pointer_hit (px, py)) {
        MetalShellMarkFull ();
      }
    }

    mPrevPtrButtons = buttons;
  } else {
    mPrevPtrButtons = 0;
  }

  if (pm_metal_wasm_session_active ()) {
    INT32         st;
    INT32         pr;
    CHAR8         namebuf[64];
    CONST CHAR8  *nm;

    nm = pm_metal_wasm_session_name ();
    if (nm != NULL) {
      AsciiStrnCpyS (namebuf, sizeof (namebuf), nm, sizeof (namebuf) - 1);
    } else {
      namebuf[0] = '?';
      namebuf[1] = '\0';
    }

    pr = pm_metal_wasm_session_poll (&st);
    if (pr != 0) {
      CHAR8  msg[96];

      AsciiSPrint (
        msg,
        sizeof (msg),
        "session '%a': %a",
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
    CHAR8   ch;
    UINT32  n;

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
  } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
    MetalShellPollGuestKeys (now_ms);
  }

  /*
   * Shell focus: full chrome. Guest fullscreen (DEFAULT surface): game owns
   * FB — skip chrome. Guest windowed (non-DEFAULT draw surface): keep chrome
   * (tab strip / status / input); paint skips wiping guest content.
   */
  {
    INT32  paint_chrome;

    paint_chrome = 0;
    if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_SHELL) {
      paint_chrome = 1;
    } else if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST
               && pm_metal_gfx_draw_surface () != PM_METAL_GFX_SURFACE_DEFAULT
               && pm_metal_gfx_draw_surface () != 0)
    {
      paint_chrome = 1;
    }

    if (paint_chrome) {
      INT32  blink;

      blink = ((now_ms - mLastFrameMs) >= 250u) ? 1 : 0;
      if (mDirty || mDirtyInput || blink) {
        INT32   px;
        INT32   py;
        UINT32  buttons;
        INT32   ix;
        INT32   iy;
        INT32   iw;
        INT32   ih;

        if (mDirty) {
          (VOID)pm_metal_ui_frame ();
          if (!pm_metal_input_pointer_locked ()) {
            pm_metal_input_pointer_sample (&px, &py, &buttons);
            (VOID)buttons;
            pm_metal_ui_cursor_draw (px, py);
          }

          (VOID)pm_metal_gfx_present ();
        } else {
          (VOID)pm_metal_ui_paint_shell_input ();
          if (pm_metal_ui_shell_input_rect (&ix, &iy, &iw, &ih) == 0) {
            (VOID)pm_metal_gfx_present_rect (ix, iy, iw, ih);
          }
        }

        mLastFrameMs = now_ms;
        mDirty       = 0;
        mDirtyInput  = 0;
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
