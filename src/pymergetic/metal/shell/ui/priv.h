/** @file
  Private UI chrome — widget tree, layout/paint, shell globals.
  Not part of the public guest/host ABI (see include/.../shell/ui/ui.h).
**/
#ifndef PYMERGETIC_METAL_SHELL_UI_PRIV_H_
#define PYMERGETIC_METAL_SHELL_UI_PRIV_H_

#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/dev/gfx/gfx.h>

#include <Uefi.h>

#define COL_DESKTOP     PM_METAL_GFX_RGB (0x4a, 0x4a, 0x4a)
#define COL_WINDOW      PM_METAL_GFX_RGB (0xaa, 0xaa, 0xaa)
#define COL_TITLE       PM_METAL_GFX_RGB (0x5c, 0x4e, 0x8c)
#define COL_TITLE_TXT   PM_METAL_GFX_RGB (0xff, 0xff, 0xff)
#define COL_TAB         PM_METAL_GFX_RGB (0x8a, 0x8a, 0x8a)
#define COL_TAB_ON      PM_METAL_GFX_RGB (0xc0, 0xc0, 0xc0)
#define COL_TAB_OFF     PM_METAL_GFX_RGB (0x7a, 0x7a, 0x7a)
#define COL_TAB_TXT     PM_METAL_GFX_RGB (0x10, 0x10, 0x10)
#define COL_BEVEL_HI    PM_METAL_GFX_RGB (0xe8, 0xe8, 0xe8)
#define COL_BEVEL_LO    PM_METAL_GFX_RGB (0x40, 0x40, 0x40)
#define COL_FRAME_FACE  PM_METAL_GFX_RGB (0x9a, 0x9a, 0x9a)
#define COL_CONSOLE_BG  PM_METAL_GFX_RGB (0x1a, 0x1a, 0x22)
#define COL_CONSOLE_FG  PM_METAL_GFX_RGB (0xc8, 0xe6, 0xc8)
#define COL_INPUT_FG    PM_METAL_GFX_RGB (0xff, 0xff, 0xcc)
#define COL_STATUS      PM_METAL_GFX_RGB (0x6a, 0x6a, 0x6a)
#define COL_STATUS_TXT  PM_METAL_GFX_RGB (0xf0, 0xf0, 0xf0)
#define COL_STATUS_CLK  PM_METAL_GFX_RGB (0x55, 0x55, 0x55)
#define COL_NET_UP      PM_METAL_GFX_RGB (0x50, 0xe0, 0x70)
#define COL_NET_DOWN    PM_METAL_GFX_RGB (0xd0, 0x55, 0x55)

#define UI_FONT_W       8
#define UI_CLOCK_CHARS  5

#define UI_MARGIN       10
#define UI_TITLE_H      28
#define UI_TAB_H        26
#define UI_STATUS_H     24
#define UI_FRAME_PAD    6
#define UI_INPUT_ROWS   1

#define CONSOLE_LINES   512
#define CONSOLE_COLS    160
#define STATUS_CHARS    128
#define TITLE_CHARS     48
#define INPUT_CHARS     120
#define MAX_TABS        16

typedef enum {
  METAL_UI_KIND_WINDOW = 0,
  METAL_UI_KIND_TABS,
  METAL_UI_KIND_TAB,
  METAL_UI_KIND_FRAME,
  METAL_UI_KIND_CONSOLE,
  METAL_UI_KIND_STATUS_BAR,
} metal_ui_kind_t;

typedef struct metal_ui_widget metal_ui_widget_t;

struct metal_ui_widget {
  metal_ui_kind_t            kind;
  INT32                      x;
  INT32                      y;
  INT32                      w;
  INT32                      h;
  CHAR8                      title[TITLE_CHARS];
  INT32                      closable;
  pm_metal_ui_handle_t       handle;
  pm_metal_gfx_surface_h     surface;
  metal_ui_widget_t         *parent;
  metal_ui_widget_t         *child;
  metal_ui_widget_t         *next;
  union {
    struct {
      CHAR8   lines[CONSOLE_LINES][CONSOLE_COLS];
      UINT32  count;
      UINT32  head;
      CHAR8   input[INPUT_CHARS];
      UINT32  input_len;
      INT32   show_input;
      INT32   cursor_on;
    } console;
    struct {
      UINT32              active;
      metal_ui_widget_t  *tabs[MAX_TABS];
      UINT32              n;
    } tabs;
    struct {
      CHAR8  text[STATUS_CHARS];
    } status;
  } u;
};

/* Shell singleton (owned by shell.c, used across modules). */
extern metal_ui_widget_t    *gMetalUiByHandle[MAX_TABS + 1];
extern metal_ui_widget_t    *gMetalUiShellRoot;
extern metal_ui_widget_t    *gMetalUiTabs;
extern metal_ui_widget_t    *gMetalUiSysConsole;
extern metal_ui_widget_t    *gMetalUiStatus;
extern pm_metal_ui_handle_t  gMetalUiConsoleHandle;

/* widget.c */
metal_ui_widget_t *MetalUiAlloc (metal_ui_kind_t kind);
VOID MetalUiAttach (metal_ui_widget_t *parent, metal_ui_widget_t *child);
VOID MetalUiDetach (metal_ui_widget_t *parent, metal_ui_widget_t *child);
VOID MetalUiDestroyTree (metal_ui_widget_t *w);
pm_metal_ui_handle_t MetalUiHandleAlloc (metal_ui_widget_t *tab);
VOID MetalUiHandleFree (pm_metal_ui_handle_t h);
metal_ui_widget_t *MetalUiTabFromHandle (pm_metal_ui_handle_t h);
INT32 MetalUiTabIndex (metal_ui_widget_t *tab);
metal_ui_widget_t *MetalUiTabConsole (metal_ui_widget_t *tab);
metal_ui_widget_t *MetalUiActiveConsole (VOID);
VOID MetalUiConsolePuts (metal_ui_widget_t *con, CONST CHAR8 *line);
metal_ui_widget_t *MetalUiMakeTabBody (
  CONST CHAR8  *title,
  INT32         closable,
  INT32         show_input
  );

/* paint.c */
VOID MetalUiLayout (VOID);
VOID MetalUiPaint (VOID);
VOID MetalUiPaintShellInputLine (VOID);
INT32 MetalUiShellInputGeom (INT32 *x, INT32 *y, INT32 *w, INT32 *h);
/** 1 if clock/systray content differs from last paint. */
INT32 MetalUiStatusNeedsRefresh (VOID);

#endif /* PYMERGETIC_METAL_SHELL_UI_PRIV_H_ */
