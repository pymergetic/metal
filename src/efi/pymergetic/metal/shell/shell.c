/** @file
  Interactive shell — real wasm run/tab. (impl: efi)
**/
#include <pymergetic/metal/shell/shell.h>
#include <pymergetic/metal/ui/ui.h>
#include <pymergetic/metal/wasm/wasm.h>
#include <pymergetic/metal/async/async.h>
#include <pymergetic/metal/input/input.h>
#include <pymergetic/metal/gfx/gfx.h>
#include <pymergetic/metal/stream/stream.h>
#include <pymergetic/metal/net/net_ops.h>
#include <pymergetic/metal/net/net_cfg.h>
#include <pymergetic/metal/audio/audio_ops.h>
#include <pymergetic/metal/console/console.h>
#include <pymergetic/metal/efi/efi_run.h>
#include <pymergetic/metal/efi/boot.h>
#include <pymergetic/metal/log/log.h>
#include <time/time.h>

#include <Uefi.h>
#include <Protocol/SimpleTextInEx.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#define SHELL_LINE_MAX  120

/* doomkeys.h values used by doomgeneric defaults (host-side mapping). */
#define DK_ENTER    0x0d
#define DK_ESCAPE   0x1b
#define DK_TAB      0x09
#define DK_LEFT     0xac
#define DK_UP       0xad
#define DK_RIGHT    0xae
#define DK_DOWN     0xaf
#define DK_STRAFE_L 0xa0
#define DK_STRAFE_R 0xa1
#define DK_USE      0xa2
#define DK_FIRE     0xa3
#define DK_RSHIFT   (0x80 + 0x36)
#define DK_RALT     (0x80 + 0x38)
#define DK_F1       (0x80 + 0x3b)

STATIC INT32   mDirty;
STATIC INT32   mExitReq;
STATIC UINT32  mPrevPtrButtons;
STATIC UINT64  mLastFrameMs;
STATIC INT32   mNest;
STATIC INT32   mCtrlDown;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mConInEx;

void
pm_metal_shell_serial_log (
  CONST CHAR8  *line
  )
{
  /* Unified log owns UEFI/UART/UI sinks (incl. COM1 after UART attach). */
  pm_metal_log (line);
}

STATIC
unsigned char
MetalShellMapDoomKey (
  CONST EFI_INPUT_KEY  *Key
  )
{
  UINT16  scan;
  CHAR16  ch;

  if (Key == NULL) {
    return 0;
  }

  scan = Key->ScanCode;
  ch   = Key->UnicodeChar;

  if (ch == CHAR_CARRIAGE_RETURN || ch == CHAR_LINEFEED) {
    return DK_ENTER;
  }

  if (ch == 0x1b || scan == SCAN_ESC) {
    return DK_ESCAPE;
  }

  if (ch == CHAR_TAB) {
    return DK_TAB;
  }

  /* Space → use (open doors); doom default key_use is KEY_USE not ' '. */
  if (ch == L' ') {
    return DK_USE;
  }

  if (ch >= 32 && ch < 127) {
    unsigned char  dk;

    dk = (unsigned char)ch;
    if (dk >= 'A' && dk <= 'Z') {
      dk = (unsigned char)(dk - 'A' + 'a');
    }

    /* VNC often eats Ctrl — z/f/x/c also fire; ,/. strafe. */
    if (dk == 'z' || dk == 'f' || dk == 'x' || dk == 'c') {
      return DK_FIRE;
    }

    if (dk == ',') {
      return DK_STRAFE_L;
    }

    if (dk == '.') {
      return DK_STRAFE_R;
    }

    return dk;
  }

  switch (scan) {
    case SCAN_UP:
      return DK_UP;
    case SCAN_DOWN:
      return DK_DOWN;
    case SCAN_LEFT:
      return DK_LEFT;
    case SCAN_RIGHT:
      return DK_RIGHT;
    case SCAN_F1:
    case SCAN_F2:
    case SCAN_F3:
    case SCAN_F4:
    case SCAN_F5:
    case SCAN_F6:
    case SCAN_F7:
    case SCAN_F8:
    case SCAN_F9:
    case SCAN_F10:
      return (unsigned char)(DK_F1 + (scan - SCAN_F1));
    default:
      return 0;
  }
}

STATIC
VOID
MetalShellSyncMods (
  UINT32  ShiftState,
  UINT64  now_ms
  )
{
  INT32  ctrl;
  INT32  shift;
  INT32  alt;

  if ((ShiftState & EFI_SHIFT_STATE_VALID) == 0) {
    return;
  }

  ctrl  = (ShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED))
          != 0;
  shift = (ShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED))
          != 0;
  alt   = (ShiftState & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) != 0;

  /*
   * Only drive FIRE from Ctrl edge — never force-release on every non-Ctrl
   * keystroke (that cancelled z/f fire and felt like Ctrl "didn't work").
   */
  if (ctrl) {
    pm_metal_input_set_held (DK_FIRE, 1, now_ms);
  } else if (mCtrlDown) {
    pm_metal_input_set_held (DK_FIRE, 0, now_ms);
  }

  mCtrlDown = ctrl;

  if (shift) {
    pm_metal_input_set_held (DK_RSHIFT, 1, now_ms);
  }

  if (alt) {
    pm_metal_input_set_held (DK_RALT, 1, now_ms);
  }
}

STATIC
VOID
MetalShellPollGameKeys (
  UINT64  now_ms
  )
{
  EFI_STATUS    Status;
  unsigned char dk;

  if (mConInEx == NULL && gST->ConsoleInHandle != NULL) {
    (VOID)gBS->HandleProtocol (
                 gST->ConsoleInHandle,
                 &gEfiSimpleTextInputExProtocolGuid,
                 (VOID **)&mConInEx
                 );
  }

  if (mConInEx != NULL) {
    for (;;) {
      EFI_KEY_DATA  Kd;

      Status = mConInEx->ReadKeyStrokeEx (mConInEx, &Kd);
      if (EFI_ERROR (Status)) {
        break;
      }

      MetalShellSyncMods (Kd.KeyState.KeyShiftState, now_ms);
      dk = MetalShellMapDoomKey (&Kd.Key);
      if (dk != 0) {
        pm_metal_input_note_key (dk, now_ms);
      }
    }
  } else {
    for (;;) {
      EFI_INPUT_KEY  Key;

      Status = gBS->CheckEvent (gST->ConIn->WaitForKey);
      if (Status != EFI_SUCCESS) {
        break;
      }

      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (EFI_ERROR (Status)) {
        break;
      }

      dk = MetalShellMapDoomKey (&Key);
      if (dk != 0) {
        pm_metal_input_note_key (dk, now_ms);
      }
    }
  }

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
   * the active guest tab when not in game focus.
   */
  if (!pm_metal_input_game_focus ()) {
    if (pm_metal_ui_tab_active_index () != 0) {
      pm_metal_ui_active_puts (line);
    }

    mDirty = 1;
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

    mDirty = 1;
    return;
  }

  MetalShellEcho (line);
}

void
pm_metal_shell_set_status (
  CONST CHAR8  *text
  )
{
  /* Avoid chrome redraw while a game owns the framebuffer. */
  if (pm_metal_input_game_focus ()) {
    return;
  }

  pm_metal_ui_set_status (text);
  mDirty = 1;
}

void
pm_metal_shell_request_exit (
  VOID
  )
{
  mExitReq = 1;
}

STATIC
VOID
MetalShellHelp (
  VOID
  )
{
  MetalShellEcho ("commands:");
  MetalShellEcho ("  help              this text");
  MetalShellEcho ("  echo <text>       print text");
  MetalShellEcho ("  run <mod>         run wasm mod in current tab");
  MetalShellEcho ("  tab <mod>         run wasm mod in a new tab");
  MetalShellEcho ("  tabs              list tabs");
  MetalShellEcho ("  use <n>           activate tab index");
  MetalShellEcho ("  close [n]         close tab n, or active/last guest");
  MetalShellEcho ("  test              run bring-up proof suite");
  MetalShellEcho ("  net [status]      show iface (ip/mask/gw/dns)");
  MetalShellEcho ("  net set <ip> <mask> <gw> [dns]");
  MetalShellEcho ("  exit|quit         shutdown (reverse fini)");
}

STATIC
VOID
MetalShellNet (
  CONST CHAR8  *arg
  )
{
  CHAR8   buf[160];
  CHAR8   ip[16];
  CHAR8   mask[16];
  CHAR8   gw[16];
  CHAR8   dns[16];
  UINTN   i;
  UINTN   n;
  CONST CHAR8  *p;
  CHAR8        *outs[4];
  CHAR8        *cur;
  UINTN         oi;

  if (arg == NULL || arg[0] == '\0'
      || AsciiStrCmp (arg, "status") == 0)
  {
    if (pm_metal_net_if_status (buf, sizeof (buf)) != 0) {
      MetalShellEcho ("net: unavailable");
    } else {
      MetalShellEcho (buf);
    }

    return;
  }

  if (AsciiStrnCmp (arg, "set ", 4) != 0) {
    MetalShellEcho ("usage: net [status] | net set <ip> <mask> <gw> [dns]");
    return;
  }

  p = arg + 4;
  while (*p == ' ') {
    p++;
  }

  outs[0] = ip;
  outs[1] = mask;
  outs[2] = gw;
  outs[3] = dns;
  ip[0] = mask[0] = gw[0] = dns[0] = '\0';
  oi  = 0;
  cur = outs[0];
  n   = 0;
  for (i = 0; p[i] != '\0' && oi < 4; i++) {
    if (p[i] == ' ') {
      cur[n] = '\0';
      oi++;
      if (oi >= 4) {
        break;
      }

      cur = outs[oi];
      n   = 0;
      while (p[i + 1] == ' ') {
        i++;
      }

      continue;
    }

    if (n + 1 < 16) {
      cur[n++] = p[i];
    }
  }

  cur[n] = '\0';
  if (ip[0] == '\0' || mask[0] == '\0' || gw[0] == '\0') {
    MetalShellEcho ("usage: net set <ip> <mask> <gw> [dns]");
    return;
  }

  if (pm_metal_net_if_set (ip, mask, gw, dns[0] != '\0' ? dns : NULL) != 0) {
    MetalShellEcho ("net set: failed");
  } else {
    MetalShellEcho ("net set: ok");
    if (pm_metal_net_if_status (buf, sizeof (buf)) == 0) {
      MetalShellEcho (buf);
    }
  }
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
  mDirty = 1;
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
  mDirty = 1;
  return rc;
}

STATIC
VOID
MetalShellTabs (
  VOID
  )
{
  UINT32  n;
  UINT32  i;
  UINT32  a;
  CHAR8   line[80];

  n = pm_metal_ui_tab_count ();
  a = pm_metal_ui_tab_active_index ();
  AsciiSPrint (line, sizeof (line), "tabs: %u  active: %u", n, a);
  MetalShellEcho (line);
  for (i = 0; i < n; i++) {
    AsciiSPrint (line, sizeof (line), "  %u%a", i, (i == a) ? " *" : "");
    MetalShellEcho (line);
  }
}

STATIC
VOID
MetalShellDispatch (
  CONST CHAR8  *line
  )
{
  CHAR8   buf[SHELL_LINE_MAX];
  CHAR8  *cmd;
  CHAR8  *arg;
  UINTN   i;

  while (*line == ' ') {
    line++;
  }

  if (*line == '\0') {
    return;
  }

  AsciiStrCpyS (buf, sizeof (buf), line);
  cmd = buf;
  arg = buf;
  while (*arg != '\0' && *arg != ' ') {
    arg++;
  }

  if (*arg == ' ') {
    *arg = '\0';
    arg++;
    while (*arg == ' ') {
      arg++;
    }
  }

  if (AsciiStrCmp (cmd, "help") == 0) {
    MetalShellHelp ();
  } else if (AsciiStrCmp (cmd, "echo") == 0) {
    MetalShellEcho (arg);
  } else if (AsciiStrCmp (cmd, "test") == 0) {
    if (pm_metal_boot_run_tests () != 0) {
      MetalShellEcho ("test: FAILED");
    } else {
      MetalShellEcho ("test: ok");
    }
  } else if (AsciiStrCmp (cmd, "net") == 0) {
    MetalShellNet (arg);
  } else if (AsciiStrCmp (cmd, "run") == 0) {
    (VOID)pm_metal_shell_run (arg);
  } else if (AsciiStrCmp (cmd, "tab") == 0) {
    (VOID)pm_metal_shell_tab (arg);
  } else if (AsciiStrCmp (cmd, "tabs") == 0) {
    MetalShellTabs ();
  } else if (AsciiStrCmp (cmd, "use") == 0) {
    i = AsciiStrDecimalToUintn (arg);
    if (pm_metal_ui_tab_activate_index ((UINT32)i) != 0) {
      MetalShellEcho ("use: bad index");
    } else {
      CHAR8  msg[40];

      AsciiSPrint (msg, sizeof (msg), "active tab %u", (UINT32)i);
      pm_metal_ui_set_status (msg);
      mDirty = 1;
    }
  } else if (AsciiStrCmp (cmd, "close") == 0) {
    UINT32  idx;
    UINT32  n;
    UINT32  a;

    n = pm_metal_ui_tab_count ();
    a = pm_metal_ui_tab_active_index ();
    if (arg[0] != '\0') {
      idx = (UINT32)AsciiStrDecimalToUintn (arg);
    } else if (a != 0) {
      idx = a;
    } else if (n > 1) {
      idx = n - 1;
    } else {
      MetalShellEcho ("close: no guest tab");
      return;
    }

    if (idx == 0) {
      MetalShellEcho ("close: cannot close console");
    } else if (pm_metal_ui_tab_activate_index (idx) != 0
               || pm_metal_ui_tab_close_active () != 0)
    {
      MetalShellEcho ("close: failed");
    } else {
      (VOID)pm_metal_ui_tab_activate_index (0);
      pm_metal_ui_set_status ("tab closed");
      MetalShellEcho ("tab closed");
      mDirty = 1;
    }
  } else if (AsciiStrCmp (cmd, "exit") == 0
             || AsciiStrCmp (cmd, "quit") == 0
             || AsciiStrCmp (cmd, "shutdown") == 0)
  {
    MetalShellEcho ("shutdown requested");
    mExitReq = 1;
  } else {
    CHAR8  msg[96];

    AsciiSPrint (msg, sizeof (msg), "unknown: %a  (try help)", cmd);
    MetalShellEcho (msg);
  }
}

int
pm_metal_shell_init (
  VOID
  )
{
  mDirty       = 1;
  mExitReq     = 0;
  mLastFrameMs = 0;
  mNest        = 0;

  pm_metal_ui_set_status ("shell ready");
  pm_metal_ui_input_clear ();
  (VOID)pm_metal_ui_frame ();
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

    MetalShellDispatch (text);
    pm_metal_ui_input_clear ();
    mDirty = 1;
    return;
  }

  if (ch == 0x7f || ch == 0x08) {
    (VOID)pm_metal_ui_input_backspace ();
    mDirty = 1;
    return;
  }

  if (ch >= 32 && ch < 127) {
    (VOID)pm_metal_stream_feed_stdin (&ch, 1);
    (VOID)pm_metal_ui_input_append (ch);
    mDirty = 1;
  }
}

int
pm_metal_shell_poll (
  VOID
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;
  UINT64         now_ms;
  CHAR8          text[SHELL_LINE_MAX];

  if (gST == NULL) {
    return -1;
  }

  if (!pm_metal_efi_owned () && (gST->ConIn == NULL || gBS == NULL)) {
    return -1;
  }

  now_ms = pm_metal_time_mono_us () / 1000u;
  pm_metal_ui_tick (now_ms);
  if (!pm_metal_efi_owned ()) {
    pm_metal_input_hw_poll ();
  }

  pm_metal_net_poll ();
  pm_metal_audio_poll ();
  pm_metal_console_poll ();

  if (!pm_metal_input_game_focus () && !pm_metal_input_pointer_locked ()) {
    INT32    px;
    INT32    py;
    UINT32   buttons;

    pm_metal_input_pointer_sample (&px, &py, &buttons);
    if ((buttons & 1u) != 0 && (mPrevPtrButtons & 1u) == 0) {
      if (pm_metal_ui_pointer_hit (px, py)) {
        mDirty = 1;
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
      mDirty = 1;
      (VOID)st;
    }
  }

  if (pm_metal_efi_owned ()) {
    CHAR8   ch;
    UINT32  n;

    /*
     * Post-EBS: ConIn is gone. VNC/QEMU keys hit i8042; COM1 is only
     * for -serial stdio. Drain both into the UI line.
     */
    if (!pm_metal_input_game_focus ()) {
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
    }
  } else if (pm_metal_input_game_focus ()) {
    MetalShellPollGameKeys (now_ms);
  } else {
    Status = gBS->CheckEvent (gST->ConIn->WaitForKey);
    if (Status == EFI_SUCCESS) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (Status == EFI_SUCCESS) {
        if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
          MetalShellHandleAscii ('\r', text, sizeof (text));
        } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
          MetalShellHandleAscii (0x08, text, sizeof (text));
        } else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
          MetalShellHandleAscii ((CHAR8)Key.UnicodeChar, text, sizeof (text));
        }
      }
    }
  }

  /* Game sessions own the FB — never repaint shell chrome over them. */
  if (!pm_metal_input_game_focus ()
      && (mDirty || (now_ms - mLastFrameMs) >= 250u))
  {
    INT32   px;
    INT32   py;
    UINT32  buttons;

    (VOID)pm_metal_ui_frame ();
    if (!pm_metal_input_pointer_locked ()) {
      pm_metal_input_pointer_sample (&px, &py, &buttons);
      (VOID)buttons;
      pm_metal_ui_cursor_draw (px, py);
      (VOID)pm_metal_gfx_present ();
    }

    mLastFrameMs = now_ms;
    mDirty       = 0;
  }

  return mExitReq ? 1 : 0;
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
    if (pm_metal_efi_owned ()) {
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
