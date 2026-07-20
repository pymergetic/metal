/** @file
  Interactive shell — real wasm run/tab. (impl: efi)
**/
#include <pymergetic/metal/shell.h>
#include <pymergetic/metal/ui.h>
#include <pymergetic/metal/wasm.h>
#include <pymergetic/metal/async.h>
#include <pymergetic/metal/input.h>
#include <pymergetic/metal/gfx.h>
#include <pymergetic/metal/esp.h>
#include <mem/mem.h>
#include <time/time.h>

#include <Uefi.h>
#include <Protocol/SimpleTextInEx.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>

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
STATIC UINT64  mLastFrameMs;
STATIC INT32   mNest;
STATIC INT32   mCtrlDown;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mConInEx;

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
   * ConOut/Print also paints the GOP. During a game: still Print (verify
   * greps serial) then scrub the FB; the next DG_DrawFrame repaints.
   */
  if (pm_metal_input_game_focus ()) {
    Print (L"%a\r\n", line);
    pm_metal_gfx_clear (PM_METAL_GFX_RGB (0, 0, 0));
    (VOID)pm_metal_gfx_present ();
    return;
  }

  pm_metal_ui_console_puts (line);
  if (pm_metal_ui_tab_active_index () != 0) {
    pm_metal_ui_active_puts (line);
  }

  Print (L"%a\r\n", line);
  mDirty = 1;
}

void
pm_metal_shell_log (
  CONST CHAR8  *line
  )
{
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
  MetalShellEcho ("  exit              shutdown");
  MetalShellEcho ("mods: hello, ui_hello, async_sleep, doom (ESP)");
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
  INT32  rc;

  mDirty       = 1;
  mExitReq     = 0;
  mLastFrameMs = 0;
  mNest        = 0;

  MetalShellEcho ("Metal shell - type help");
  MetalShellEcho ("mods: hello, ui_hello, async_sleep, doom");
  pm_metal_ui_set_status ("shell ready");
  pm_metal_ui_input_clear ();

  if (pm_metal_wasm_ready ()) {
    pm_metal_wasm_set_stdout_tab (pm_metal_ui_console_handle ());
    mNest = 1;
    rc    = pm_metal_wasm_run_mod ("hello");
    /* Second load proves embed bytes are not mutated in-place. */
    if (rc == 0) {
      rc = pm_metal_wasm_run_mod ("hello");
    }

    if (rc == 0) {
      MetalShellEcho ("metal-wasm: t0_hello ok");
    } else {
      MetalShellEcho ("metal-wasm: t0_hello fail");
      Print (L"metal-wasm: t0_hello fail (%d)\r\n", rc);
    }

    /* Real resume proof — guest await(sleep) parks into host runloop. */
    if (rc == 0) {
      rc = pm_metal_wasm_run_mod ("async_sleep");
      if (rc == 0 && pm_metal_wasm_session_active ()) {
        rc = pm_metal_wasm_session_await (5000);
      }

      if (rc == 0) {
        MetalShellEcho ("metal-async: sleep ok");
      } else {
        MetalShellEcho ("metal-async: sleep fail");
        Print (L"metal-async: sleep fail (%d)\r\n", rc);
      }
    }

    /* Headless verify only: ESP marker mods/apps/doom/autostart.
     * Interactive: type `run doom` / `tab doom`. */
    if (rc == 0 && pm_metal_esp_ready ()) {
      UINT8   *marker;
      UINT32   mlen;
      INT32    doom_rc;

      marker = NULL;
      mlen   = 0;
      if (pm_metal_esp_read_file ("mods/apps/doom/autostart", &marker, &mlen)
          == 0)
      {
        if (marker != NULL) {
          pm_metal_mem_free (marker);
        }

        doom_rc = pm_metal_wasm_run_mod ("doom");
        if (doom_rc == 0 && pm_metal_wasm_session_active ()) {
          MetalShellEcho ("metal-doom: session live");
        } else if (doom_rc == 0) {
          MetalShellEcho ("metal-doom: session ok");
        } else {
          MetalShellEcho ("metal-doom: fail");
          Print (L"metal-doom: fail (%d)\r\n", doom_rc);
        }
      }
    }

    mNest = 0;
    pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
  } else {
    Print (L"metal-wasm: not ready\r\n");
  }

  (VOID)pm_metal_ui_frame ();
  return 0;
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

  if (gST == NULL || gST->ConIn == NULL || gBS == NULL) {
    return -1;
  }

  now_ms = pm_metal_time_mono_us () / 1000u;
  pm_metal_ui_tick (now_ms);

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

  if (pm_metal_input_game_focus ()) {
    MetalShellPollGameKeys (now_ms);
  } else {
    Status = gBS->CheckEvent (gST->ConIn->WaitForKey);
    if (Status == EFI_SUCCESS) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (Status == EFI_SUCCESS) {
        if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
          if (pm_metal_ui_input_text (text, sizeof (text)) < 0) {
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
        } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
          (VOID)pm_metal_ui_input_backspace ();
          mDirty = 1;
        } else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
          (VOID)pm_metal_ui_input_append ((CHAR8)Key.UnicodeChar);
          mDirty = 1;
        }
      }
    }
  }

  /* Game sessions own the FB — never repaint shell chrome over them. */
  if (!pm_metal_input_game_focus ()
      && (mDirty || (now_ms - mLastFrameMs) >= 250u))
  {
    (VOID)pm_metal_ui_frame ();
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
    Print (L"metal-shell: log bad ptr %p\r\n", line);
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
