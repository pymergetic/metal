/** @file
  UI WASI native import bridge.
**/
#include "priv.h"

#include "wasm_export.h"

static uint32_t
pm_metal_ui_console_handle_native (
  wasm_exec_env_t  exec_env
  )
{
  (void)exec_env;
  return (uint32_t)pm_metal_ui_console_handle ();
}

static uint32_t
pm_metal_ui_tab_open_native (
  wasm_exec_env_t  exec_env,
  char            *title,
  int32_t          activate
  )
{
  (void)exec_env;
  return (uint32_t)pm_metal_ui_tab_open (title, (int)activate);
}

static int32_t
pm_metal_ui_tab_close_native (
  wasm_exec_env_t  exec_env,
  uint32_t         h
  )
{
  (void)exec_env;
  return (int32_t)pm_metal_ui_tab_close ((pm_metal_ui_handle_t)h);
}

static int32_t
pm_metal_ui_tab_activate_native (
  wasm_exec_env_t  exec_env,
  uint32_t         h
  )
{
  (void)exec_env;
  return (int32_t)pm_metal_ui_tab_activate ((pm_metal_ui_handle_t)h);
}

static uint32_t
pm_metal_ui_tab_count_native (
  wasm_exec_env_t  exec_env
  )
{
  (void)exec_env;
  return (uint32_t)pm_metal_ui_tab_count ();
}

static uint32_t
pm_metal_ui_tab_active_native (
  wasm_exec_env_t  exec_env
  )
{
  (void)exec_env;
  return (uint32_t)pm_metal_ui_tab_active ();
}

static void
pm_metal_ui_tab_puts_native (
  wasm_exec_env_t  exec_env,
  uint32_t         h,
  char            *line
  )
{
  (void)exec_env;
  pm_metal_ui_tab_puts ((pm_metal_ui_handle_t)h, line);
}

static void
pm_metal_ui_console_puts_native (
  wasm_exec_env_t  exec_env,
  char            *line
  )
{
  (void)exec_env;
  pm_metal_ui_console_puts (line);
}

static void
pm_metal_ui_active_puts_native (
  wasm_exec_env_t  exec_env,
  char            *line
  )
{
  (void)exec_env;
  pm_metal_ui_active_puts (line);
}

static void
pm_metal_ui_set_status_native (
  wasm_exec_env_t  exec_env,
  char            *text
  )
{
  (void)exec_env;
  pm_metal_ui_set_status (text);
}

static void
pm_metal_ui_input_clear_native (
  wasm_exec_env_t  exec_env
  )
{
  (void)exec_env;
  pm_metal_ui_input_clear ();
}

static int32_t
pm_metal_ui_input_append_native (
  wasm_exec_env_t  exec_env,
  int32_t          ch
  )
{
  (void)exec_env;
  return (int32_t)pm_metal_ui_input_append ((char)ch);
}

static int32_t
pm_metal_ui_input_backspace_native (
  wasm_exec_env_t  exec_env
  )
{
  (void)exec_env;
  return (int32_t)pm_metal_ui_input_backspace ();
}

static int32_t
pm_metal_ui_input_text_native (
  wasm_exec_env_t  exec_env,
  char            *out,
  uint32_t         cap
  )
{
  (void)exec_env;
  return (int32_t)pm_metal_ui_input_text (out, cap);
}

STATIC UINT32
pm_metal_ui_tab_surface_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_ui_tab_surface ((pm_metal_ui_handle_t)h);
}

static NativeSymbol g_pm_metal_ui_native_symbols[] = {
  { "pm_metal_ui_console_handle", (void *)pm_metal_ui_console_handle_native, "()i", NULL },
  { "pm_metal_ui_tab_open", (void *)pm_metal_ui_tab_open_native, "($i)i", NULL },
  { "pm_metal_ui_tab_close", (void *)pm_metal_ui_tab_close_native, "(i)i", NULL },
  { "pm_metal_ui_tab_activate", (void *)pm_metal_ui_tab_activate_native, "(i)i", NULL },
  { "pm_metal_ui_tab_count", (void *)pm_metal_ui_tab_count_native, "()i", NULL },
  { "pm_metal_ui_tab_active", (void *)pm_metal_ui_tab_active_native, "()i", NULL },
  { "pm_metal_ui_tab_puts", (void *)pm_metal_ui_tab_puts_native, "(i$)", NULL },
  { "pm_metal_ui_tab_surface", (void *)pm_metal_ui_tab_surface_native, "(i)i", NULL },
  { "pm_metal_ui_console_puts", (void *)pm_metal_ui_console_puts_native, "($)", NULL },
  { "pm_metal_ui_active_puts", (void *)pm_metal_ui_active_puts_native, "($)", NULL },
  { "pm_metal_ui_set_status", (void *)pm_metal_ui_set_status_native, "($)", NULL },
  { "pm_metal_ui_input_clear", (void *)pm_metal_ui_input_clear_native, "()", NULL },
  { "pm_metal_ui_input_append", (void *)pm_metal_ui_input_append_native, "(i)i", NULL },
  { "pm_metal_ui_input_backspace", (void *)pm_metal_ui_input_backspace_native, "()i", NULL },
  { "pm_metal_ui_input_text", (void *)pm_metal_ui_input_text_native, "(*~)i", NULL },
};

int
pm_metal_ui_native_register (
  void
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_UI_WASI_MODULE,
         g_pm_metal_ui_native_symbols,
         sizeof (g_pm_metal_ui_native_symbols)
           / sizeof (g_pm_metal_ui_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
