/** @file
  UI — IRIX chrome, multi-tab, console + input. Handle ABI. (impl: efi)
**/
#include <pymergetic/metal/ui/ui.h>
#include <pymergetic/metal/gfx/gfx.h>
#include <pymergetic/metal/lifecycle/lifecycle.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

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

/* Internal widget kinds — not part of the public ABI. */
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
  pm_metal_ui_handle_t       handle; /* TAB only; 0 = none */
  pm_metal_gfx_surface_h     surface; /* content present target; 0 none */
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

/*
 * Handle table: index == handle. Slot 0 unused (invalid).
 * After console_shell(), console tab occupies handle 1.
 */
STATIC metal_ui_widget_t  *mByHandle[MAX_TABS + 1];
STATIC metal_ui_widget_t  *mShellRoot;
STATIC metal_ui_widget_t  *mTabs;
STATIC metal_ui_widget_t  *mSysConsole;
STATIC metal_ui_widget_t  *mStatus;
STATIC pm_metal_ui_handle_t  mConsoleHandle;

STATIC
metal_ui_widget_t *
MetalUiAlloc (
  metal_ui_kind_t  kind
  )
{
  metal_ui_widget_t  *w;

  w = (metal_ui_widget_t *)pm_metal_mem_alloc (
                             sizeof (*w),
                             PM_METAL_MEM_HEAP,
                             PM_METAL_MEM_ID_NONE
                             );
  if (w == NULL) {
    return NULL;
  }

  ZeroMem (w, sizeof (*w));
  w->kind = kind;
  return w;
}

STATIC
VOID
MetalUiAttach (
  metal_ui_widget_t  *parent,
  metal_ui_widget_t  *child
  )
{
  metal_ui_widget_t  *p;

  if (parent == NULL || child == NULL) {
    return;
  }

  child->parent = parent;
  if (parent->child == NULL) {
    parent->child = child;
    return;
  }

  p = parent->child;
  while (p->next != NULL) {
    p = p->next;
  }

  p->next = child;
}

STATIC
VOID
MetalUiDetach (
  metal_ui_widget_t  *parent,
  metal_ui_widget_t  *child
  )
{
  metal_ui_widget_t  *p;
  metal_ui_widget_t  *prev;

  if (parent == NULL || child == NULL) {
    return;
  }

  prev = NULL;
  p    = parent->child;
  while (p != NULL) {
    if (p == child) {
      if (prev == NULL) {
        parent->child = p->next;
      } else {
        prev->next = p->next;
      }

      child->parent = NULL;
      child->next   = NULL;
      return;
    }

    prev = p;
    p    = p->next;
  }
}

STATIC
VOID
MetalUiDestroyTree (
  metal_ui_widget_t  *w
  )
{
  metal_ui_widget_t  *c;
  metal_ui_widget_t  *n;

  if (w == NULL) {
    return;
  }

  c = w->child;
  while (c != NULL) {
    n = c->next;
    MetalUiDestroyTree (c);
    c = n;
  }

  if (w->kind == METAL_UI_KIND_TAB && w->surface != PM_METAL_GFX_SURFACE_INVALID) {
    pm_metal_gfx_surface_free (w->surface);
    w->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  pm_metal_mem_free (w);
}

STATIC
pm_metal_ui_handle_t
MetalUiHandleAlloc (
  metal_ui_widget_t  *tab
  )
{
  UINT32  i;

  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  for (i = 1; i <= MAX_TABS; i++) {
    if (mByHandle[i] == NULL) {
      mByHandle[i] = tab;
      tab->handle  = (pm_metal_ui_handle_t)i;
      return tab->handle;
    }
  }

  return PM_METAL_UI_HANDLE_INVALID;
}

STATIC
VOID
MetalUiHandleFree (
  pm_metal_ui_handle_t  h
  )
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return;
  }

  if (mByHandle[h] != NULL) {
    mByHandle[h]->handle = PM_METAL_UI_HANDLE_INVALID;
    mByHandle[h]         = NULL;
  }
}

STATIC
metal_ui_widget_t *
MetalUiTabFromHandle (
  pm_metal_ui_handle_t  h
  )
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return NULL;
  }

  return mByHandle[h];
}

STATIC
INT32
MetalUiTabIndex (
  metal_ui_widget_t  *tab
  )
{
  UINT32  i;

  if (mTabs == NULL || tab == NULL) {
    return -1;
  }

  for (i = 0; i < mTabs->u.tabs.n; i++) {
    if (mTabs->u.tabs.tabs[i] == tab) {
      return (INT32)i;
    }
  }

  return -1;
}

STATIC
metal_ui_widget_t *
MetalUiTabConsole (
  metal_ui_widget_t  *tab
  )
{
  metal_ui_widget_t  *frame;

  if (tab == NULL || tab->child == NULL) {
    return NULL;
  }

  frame = tab->child;
  return frame->child;
}

STATIC
metal_ui_widget_t *
MetalUiActiveConsole (
  VOID
  )
{
  metal_ui_widget_t  *tab;

  if (mTabs == NULL || mTabs->u.tabs.n == 0) {
    return mSysConsole;
  }

  tab = mTabs->u.tabs.tabs[mTabs->u.tabs.active];
  return MetalUiTabConsole (tab);
}

STATIC
VOID
MetalUiConsolePuts (
  metal_ui_widget_t  *con,
  CONST CHAR8        *line
  )
{
  UINT32  i;
  UINT32  n;

  if (con == NULL || line == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  n = 0;
  while (line[n] != '\0' && n < CONSOLE_COLS - 1) {
    n++;
  }

  i = con->u.console.head;
  ZeroMem (con->u.console.lines[i], CONSOLE_COLS);
  CopyMem (con->u.console.lines[i], line, n);
  con->u.console.lines[i][n] = '\0';
  con->u.console.head = (i + 1u) % CONSOLE_LINES;
  if (con->u.console.count < CONSOLE_LINES) {
    con->u.console.count++;
  }
}

STATIC
metal_ui_widget_t *
MetalUiMakeTabBody (
  CONST CHAR8  *title,
  INT32         closable,
  INT32         show_input
  )
{
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;
  metal_ui_widget_t  *con;

  tab   = MetalUiAlloc (METAL_UI_KIND_TAB);
  frame = MetalUiAlloc (METAL_UI_KIND_FRAME);
  con   = MetalUiAlloc (METAL_UI_KIND_CONSOLE);
  if (tab == NULL || frame == NULL || con == NULL) {
    MetalUiDestroyTree (tab);
    MetalUiDestroyTree (frame);
    MetalUiDestroyTree (con);
    return NULL;
  }

  AsciiStrCpyS (tab->title, sizeof (tab->title), title);
  tab->closable             = closable;
  tab->surface              = pm_metal_gfx_surface_alloc ();
  con->u.console.show_input = show_input;
  con->u.console.cursor_on  = 1;
  MetalUiAttach (tab, frame);
  MetalUiAttach (frame, con);
  return tab;
}

STATIC
VOID
MetalUiLayoutWindow (
  metal_ui_widget_t  *win,
  INT32               sw,
  INT32               sh
  )
{
  metal_ui_widget_t  *tabs;
  metal_ui_widget_t  *st;
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;
  metal_ui_widget_t  *con;
  UINT32              i;
  INT32               x;
  INT32               y;
  INT32               w;
  INT32               h;
  INT32               body_y;
  INT32               body_h;
  INT32               input_h;
  UINT32              fh;

  x = UI_MARGIN;
  y = UI_MARGIN;
  w = sw - 2 * UI_MARGIN;
  h = sh - 2 * UI_MARGIN;
  if (w < 160 || h < 120 || mTabs == NULL || mStatus == NULL) {
    return;
  }

  win->x = x;
  win->y = y;
  win->w = w;
  win->h = h;

  tabs = mTabs;
  st   = mStatus;

  tabs->x = x + 4;
  tabs->y = y + UI_TITLE_H;
  tabs->w = w - 8;
  tabs->h = UI_TAB_H;

  st->x = x + 4;
  st->y = y + h - UI_STATUS_H - 4;
  st->w = w - 8;
  st->h = UI_STATUS_H;

  body_y = tabs->y + tabs->h + 2;
  body_h = st->y - body_y - 2;
  fh     = pm_metal_gfx_font_height ();

  for (i = 0; i < tabs->u.tabs.n; i++) {
    tab = tabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tab->x = tabs->x;
    tab->y = body_y;
    tab->w = tabs->w;
    tab->h = body_h;

    frame = tab->child;
    if (frame == NULL) {
      continue;
    }

    frame->x = tab->x;
    frame->y = tab->y;
    frame->w = tab->w;
    frame->h = tab->h;

    con = frame->child;
    if (con == NULL) {
      continue;
    }

    /* Shell input is shared — reserve a row on every tab so focus
       switches don't jump, and guests can still type close/use/…. */
    input_h = (INT32)fh * UI_INPUT_ROWS + 4;

    con->x = frame->x + UI_FRAME_PAD;
    con->y = frame->y + UI_FRAME_PAD;
    con->w = frame->w - 2 * UI_FRAME_PAD;
    con->h = frame->h - 2 * UI_FRAME_PAD - input_h;

    if (tab->surface != PM_METAL_GFX_SURFACE_INVALID) {
      /* Guest content = frame interior (chrome stays outside). */
      pm_metal_gfx_surface_set_rect (
        tab->surface,
        frame->x,
        frame->y,
        frame->w,
        frame->h
        );
    }
  }
}

STATIC
VOID
MetalUiPaintConsole (
  metal_ui_widget_t  *con
  )
{
  UINT32  fw;
  UINT32  fh;
  UINT32  cols;
  UINT32  rows;
  UINT32  visible;
  UINT32  start;
  UINT32  i;
  INT32   ty;
  CHAR8   prompt[INPUT_CHARS + 8];

  fw = pm_metal_gfx_font_width ();
  fh = pm_metal_gfx_font_height ();
  if (fw == 0 || fh == 0 || con->w < (INT32)fw || con->h < (INT32)fh) {
    return;
  }

  pm_metal_gfx_fill_rect (con->x, con->y, con->w, con->h, COL_CONSOLE_BG);

  cols = (UINT32)con->w / fw;
  rows = (UINT32)con->h / fh;
  if (cols == 0 || rows == 0) {
    return;
  }

  if (cols > CONSOLE_COLS - 1) {
    cols = CONSOLE_COLS - 1;
  }

  visible = con->u.console.count;
  if (visible > rows) {
    visible = rows;
  }

  if (con->u.console.count <= rows) {
    start = 0;
  } else {
    start = con->u.console.count - rows;
  }

  ty = con->y;
  for (i = 0; i < visible; i++) {
    UINT32       idx;
    CONST CHAR8  *line;
    CHAR8         buf[CONSOLE_COLS];
    UINT32        len;

    idx = (con->u.console.head + CONSOLE_LINES - con->u.console.count + start + i)
          % CONSOLE_LINES;
    line = con->u.console.lines[idx];
    len  = 0;
    while (line[len] != '\0' && len < cols) {
      buf[len] = line[len];
      len++;
    }

    buf[len] = '\0';
    pm_metal_gfx_draw_text (con->x + 2, ty, buf, COL_CONSOLE_FG, COL_CONSOLE_BG, 0);
    ty += (INT32)fh;
  }

  /* Shared shell line lives on the system console; paint it on the
     active tab so guest tabs stay interactive. */
  if (con == MetalUiActiveConsole () && mSysConsole != NULL) {
    INT32              iy;
    UINT32             plen;
    metal_ui_widget_t  *src;

    src = mSysConsole;
    iy  = con->y + con->h + 2;
    pm_metal_gfx_fill_rect (
      con->x,
      iy,
      con->w,
      (INT32)fh + 2,
      COL_CONSOLE_BG
      );

    prompt[0] = '>';
    prompt[1] = ' ';
    plen     = 2;
    CopyMem (
      &prompt[plen],
      src->u.console.input,
      src->u.console.input_len
      );
    plen += src->u.console.input_len;
    if (src->u.console.cursor_on) {
      prompt[plen++] = 0xDB; /* block cursor (VGA CP437) */
    }

    prompt[plen] = '\0';
    pm_metal_gfx_draw_text (con->x + 2, iy, prompt, COL_INPUT_FG, COL_CONSOLE_BG, 0);
  }
}

STATIC
VOID
MetalUiPaintTabsStrip (
  metal_ui_widget_t  *tabs
  )
{
  UINT32  i;
  INT32   x;
  UINT32  fw;

  pm_metal_gfx_fill_rect (tabs->x, tabs->y, tabs->w, tabs->h, COL_TAB);
  fw = pm_metal_gfx_font_width ();
  x  = tabs->x + 2;

  for (i = 0; i < tabs->u.tabs.n; i++) {
    metal_ui_widget_t  *tab;
    INT32               tw;
    UINT32              tlen;
    pm_metal_gfx_color_t  face;

    tab = tabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tlen = 0;
    while (tab->title[tlen] != '\0') {
      tlen++;
    }

    tw = (INT32)((tlen + 2) * fw) + 16;
    if (tw < 64) {
      tw = 64;
    }

    if (x + tw > tabs->x + tabs->w - 4) {
      break;
    }

    face = (i == tabs->u.tabs.active) ? COL_TAB_ON : COL_TAB_OFF;
    pm_metal_gfx_fill_rect (x, tabs->y + 2, tw, tabs->h - 2, face);
    pm_metal_gfx_bevel_rect (
      x,
      tabs->y + 2,
      tw,
      tabs->h - 2,
      (i == tabs->u.tabs.active) ? 1 : 0,
      COL_BEVEL_HI,
      COL_BEVEL_LO
      );
    pm_metal_gfx_draw_text (
      x + 8,
      tabs->y + 6,
      tab->title,
      COL_TAB_TXT,
      face,
      1
      );
    x += tw + 4;
  }
}

STATIC
VOID
MetalUiPaintWidget (
  metal_ui_widget_t  *w
  )
{
  metal_ui_widget_t  *c;

  if (w == NULL) {
    return;
  }

  switch (w->kind) {
    case METAL_UI_KIND_WINDOW:
      pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_WINDOW);
      pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 1, COL_BEVEL_HI, COL_BEVEL_LO);
      pm_metal_gfx_fill_rect (w->x + 4, w->y + 4, w->w - 8, UI_TITLE_H - 4, COL_TITLE);
      pm_metal_gfx_draw_text (w->x + 12, w->y + 8, w->title, COL_TITLE_TXT, COL_TITLE, 1);
      break;

    case METAL_UI_KIND_TABS:
      MetalUiPaintTabsStrip (w);
      if (w->u.tabs.n > 0 && w->u.tabs.active < w->u.tabs.n) {
        MetalUiPaintWidget (w->u.tabs.tabs[w->u.tabs.active]);
      }

      return;

    case METAL_UI_KIND_FRAME:
      pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_FRAME_FACE);
      pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);
      break;

    case METAL_UI_KIND_CONSOLE:
      MetalUiPaintConsole (w);
      return;

    case METAL_UI_KIND_STATUS_BAR:
      pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_STATUS);
      pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);
      pm_metal_gfx_draw_text (
        w->x + 8,
        w->y + 4,
        w->u.status.text,
        COL_STATUS_TXT,
        COL_STATUS,
        1
        );
      return;

    default:
      break;
  }

  for (c = w->child; c != NULL; c = c->next) {
    if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_TABS) {
      MetalUiPaintWidget (c);
    } else if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_STATUS_BAR) {
      MetalUiPaintWidget (c);
    } else if (w->kind != METAL_UI_KIND_WINDOW) {
      MetalUiPaintWidget (c);
    }
  }
}

STATIC
VOID
MetalUiLayout (
  VOID
  )
{
  pm_metal_gfx_surface_t  *surf;

  surf = pm_metal_gfx_surface ();
  if (mShellRoot == NULL || surf == NULL) {
    return;
  }

  MetalUiLayoutWindow (mShellRoot, (INT32)surf->width, (INT32)surf->height);
}

STATIC
VOID
MetalUiPaint (
  VOID
  )
{
  if (mShellRoot == NULL) {
    return;
  }

  pm_metal_gfx_clear (COL_DESKTOP);
  MetalUiPaintWidget (mShellRoot);
}

int
pm_metal_ui_console_shell (
  void
  )
{
  pm_metal_gfx_surface_t  *surf;
  metal_ui_widget_t       *win;
  metal_ui_widget_t       *tabs;
  metal_ui_widget_t       *tab;
  metal_ui_widget_t       *st;
  pm_metal_ui_handle_t     h;

  if (!pm_metal_gfx_ready ()) {
    return -1;
  }

  surf = pm_metal_gfx_surface ();
  if (surf == NULL) {
    return -1;
  }

  if (mShellRoot != NULL) {
    return 0;
  }

  win  = MetalUiAlloc (METAL_UI_KIND_WINDOW);
  tabs = MetalUiAlloc (METAL_UI_KIND_TABS);
  st   = MetalUiAlloc (METAL_UI_KIND_STATUS_BAR);
  tab  = MetalUiMakeTabBody ("console", 0, 1);
  if (win == NULL || tabs == NULL || st == NULL || tab == NULL) {
    MetalUiDestroyTree (win);
    MetalUiDestroyTree (tabs);
    MetalUiDestroyTree (st);
    MetalUiDestroyTree (tab);
    return -1;
  }

  h = MetalUiHandleAlloc (tab);
  if (h == PM_METAL_UI_HANDLE_INVALID) {
    MetalUiDestroyTree (win);
    MetalUiDestroyTree (tabs);
    MetalUiDestroyTree (st);
    MetalUiDestroyTree (tab);
    return -1;
  }

  AsciiStrCpyS (win->title, sizeof (win->title), "Metal - pymergetic");
  AsciiStrCpyS (st->u.status.text, sizeof (st->u.status.text), "ready");

  tabs->u.tabs.tabs[0] = tab;
  tabs->u.tabs.n       = 1;
  tabs->u.tabs.active  = 0;
  MetalUiAttach (tabs, tab);
  MetalUiAttach (win, tabs);
  MetalUiAttach (win, st);

  mShellRoot      = win;
  mTabs           = tabs;
  mSysConsole     = MetalUiTabConsole (tab);
  mStatus         = st;
  mConsoleHandle  = h; /* first free slot → 1 */

  pm_metal_gfx_clear (COL_DESKTOP);
  MetalUiLayout ();
  MetalUiPaint ();
  (VOID)pm_metal_gfx_present ();
  return 0;
}

void
pm_metal_ui_fini (
  void
  )
{
  UINT32  i;

  if (mShellRoot != NULL) {
    MetalUiDestroyTree (mShellRoot);
  }

  for (i = 0; i <= MAX_TABS; i++) {
    mByHandle[i] = NULL;
  }

  mShellRoot     = NULL;
  mTabs          = NULL;
  mSysConsole    = NULL;
  mStatus        = NULL;
  mConsoleHandle = PM_METAL_UI_HANDLE_INVALID;
}

int
pm_metal_ui_frame (
  void
  )
{
  if (mShellRoot == NULL) {
    return -1;
  }

  /* Chrome always paints in screen space (DEFAULT surface). */
  pm_metal_gfx_set_surface (PM_METAL_GFX_SURFACE_DEFAULT);
  MetalUiLayout ();
  MetalUiPaint ();
  return pm_metal_gfx_present ();
}

void
pm_metal_ui_tick (
  uint64_t  now_ms
  )
{
  INT32  on;

  if (mSysConsole == NULL) {
    return;
  }

  on = ((now_ms / 500u) & 1u) ? 1 : 0;
  if (on != mSysConsole->u.console.cursor_on) {
    mSysConsole->u.console.cursor_on = on;
  }
}

pm_metal_ui_handle_t
pm_metal_ui_console_handle (
  void
  )
{
  return mConsoleHandle;
}

pm_metal_ui_handle_t
pm_metal_ui_tab_open (
  const char  *title,
  int          activate
  )
{
  metal_ui_widget_t     *tab;
  pm_metal_ui_handle_t   h;

  if (mTabs == NULL || title == NULL || mTabs->u.tabs.n >= MAX_TABS) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  tab = MetalUiMakeTabBody (title, 1, 0);
  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  h = MetalUiHandleAlloc (tab);
  if (h == PM_METAL_UI_HANDLE_INVALID) {
    MetalUiDestroyTree (tab);
    return PM_METAL_UI_HANDLE_INVALID;
  }

  mTabs->u.tabs.tabs[mTabs->u.tabs.n] = tab;
  if (activate) {
    mTabs->u.tabs.active = mTabs->u.tabs.n;
    pm_metal_lifecycle_set (
      tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
      PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
      );
  }

  mTabs->u.tabs.n++;
  MetalUiAttach (mTabs, tab);
  return h;
}

int
pm_metal_ui_tab_close (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;
  UINT32              i;
  UINT32              j;
  UINT32              was;

  tab = MetalUiTabFromHandle (h);
  if (mTabs == NULL || tab == NULL || !tab->closable) {
    return -1;
  }

  for (i = 0; i < mTabs->u.tabs.n; i++) {
    if (mTabs->u.tabs.tabs[i] != tab) {
      continue;
    }

    was = mTabs->u.tabs.active;
    if (was == i) {
      pm_metal_lifecycle_blur ();
    }

    MetalUiHandleFree (h);
    MetalUiDetach (mTabs, tab);
    MetalUiDestroyTree (tab);
    for (j = i; j + 1 < mTabs->u.tabs.n; j++) {
      mTabs->u.tabs.tabs[j] = mTabs->u.tabs.tabs[j + 1];
    }

    mTabs->u.tabs.n--;
    mTabs->u.tabs.tabs[mTabs->u.tabs.n] = NULL;

    if (mTabs->u.tabs.n == 0) {
      mTabs->u.tabs.active = 0;
    } else if (was == i) {
      /* Closed the focused tab — land on console. */
      mTabs->u.tabs.active = 0;
      {
        metal_ui_widget_t  *next;

        next = mTabs->u.tabs.tabs[0];
        if (next != NULL) {
          pm_metal_lifecycle_set (
            next->surface != 0 ? next->surface : PM_METAL_GFX_SURFACE_DEFAULT,
            PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
            );
        }
      }
    } else if (was > i) {
      mTabs->u.tabs.active = was - 1;
    }

    return 0;
  }

  return -1;
}

int
pm_metal_ui_tab_activate (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;
  INT32               idx;

  tab = MetalUiTabFromHandle (h);
  idx = MetalUiTabIndex (tab);
  if (idx < 0) {
    return -1;
  }

  mTabs->u.tabs.active = (UINT32)idx;
  pm_metal_lifecycle_set (
    tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
    PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
    );
  return 0;
}

unsigned
pm_metal_ui_tab_count (
  void
  )
{
  return (mTabs != NULL) ? mTabs->u.tabs.n : 0;
}

pm_metal_ui_handle_t
pm_metal_ui_tab_active (
  void
  )
{
  metal_ui_widget_t  *tab;

  if (mTabs == NULL || mTabs->u.tabs.n == 0) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  tab = mTabs->u.tabs.tabs[mTabs->u.tabs.active];
  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  return tab->handle;
}

void
pm_metal_ui_tab_puts (
  pm_metal_ui_handle_t  h,
  const char           *line
  )
{
  metal_ui_widget_t  *tab;

  tab = MetalUiTabFromHandle (h);
  MetalUiConsolePuts (MetalUiTabConsole (tab), line);
}

void
pm_metal_ui_console_puts (
  const char  *line
  )
{
  MetalUiConsolePuts (mSysConsole, line);
}

void
pm_metal_ui_active_puts (
  const char  *line
  )
{
  MetalUiConsolePuts (MetalUiActiveConsole (), line);
}

void
pm_metal_ui_set_status (
  const char  *text
  )
{
  if (mStatus == NULL || text == NULL) {
    return;
  }

  AsciiStrCpyS (mStatus->u.status.text, sizeof (mStatus->u.status.text), text);
}

void
pm_metal_ui_input_clear (
  void
  )
{
  if (mSysConsole == NULL) {
    return;
  }

  ZeroMem (mSysConsole->u.console.input, sizeof (mSysConsole->u.console.input));
  mSysConsole->u.console.input_len = 0;
}

int
pm_metal_ui_input_append (
  char  ch
  )
{
  if (mSysConsole == NULL || ch < 32) {
    return -1;
  }

  if (mSysConsole->u.console.input_len + 1 >= INPUT_CHARS) {
    return -1;
  }

  mSysConsole->u.console.input[mSysConsole->u.console.input_len++] = ch;
  mSysConsole->u.console.input[mSysConsole->u.console.input_len]   = '\0';
  return 0;
}

int
pm_metal_ui_input_backspace (
  void
  )
{
  if (mSysConsole == NULL || mSysConsole->u.console.input_len == 0) {
    return -1;
  }

  mSysConsole->u.console.input_len--;
  mSysConsole->u.console.input[mSysConsole->u.console.input_len] = '\0';
  return 0;
}

int
pm_metal_ui_input_text (
  char      *out,
  uint32_t   cap
  )
{
  UINT32  n;
  UINT32  i;

  if (out == NULL || cap == 0) {
    return -1;
  }

  if (mSysConsole == NULL) {
    out[0] = '\0';
    return 0;
  }

  n = mSysConsole->u.console.input_len;
  if (n + 1 > cap) {
    n = cap - 1;
  }

  for (i = 0; i < n; i++) {
    out[i] = mSysConsole->u.console.input[i];
  }

  out[n] = '\0';
  return (int)n;
}

int
pm_metal_ui_tab_activate_index (
  unsigned  index
  )
{
  metal_ui_widget_t  *tab;

  if (mTabs == NULL || index >= mTabs->u.tabs.n) {
    return -1;
  }

  mTabs->u.tabs.active = index;
  tab = mTabs->u.tabs.tabs[index];
  if (tab != NULL) {
    pm_metal_lifecycle_set (
      tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
      PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
      );
  }

  return 0;
}

unsigned
pm_metal_ui_tab_active_index (
  void
  )
{
  return (mTabs != NULL) ? mTabs->u.tabs.active : 0;
}

int
pm_metal_ui_tab_close_active (
  void
  )
{
  metal_ui_widget_t  *tab;

  if (mTabs == NULL || mTabs->u.tabs.n == 0) {
    return -1;
  }

  tab = mTabs->u.tabs.tabs[mTabs->u.tabs.active];
  if (tab == NULL) {
    return -1;
  }

  return pm_metal_ui_tab_close (tab->handle);
}

pm_metal_gfx_surface_h
pm_metal_ui_tab_surface (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;

  tab = MetalUiTabFromHandle (h);
  if (tab == NULL) {
    return PM_METAL_GFX_SURFACE_INVALID;
  }

  return tab->surface;
}

int
pm_metal_ui_tab_content_rect (
  pm_metal_ui_handle_t  tab_h,
  int32_t              *ox,
  int32_t              *oy,
  int32_t              *ow,
  int32_t              *oh
  )
{
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;

  tab = MetalUiTabFromHandle (tab_h);
  if (tab == NULL || tab->child == NULL) {
    return -1;
  }

  frame = tab->child;
  if (ox != NULL) {
    *ox = frame->x;
  }

  if (oy != NULL) {
    *oy = frame->y;
  }

  if (ow != NULL) {
    *ow = frame->w;
  }

  if (oh != NULL) {
    *oh = frame->h;
  }

  return 0;
}

int
pm_metal_ui_pointer_hit (
  int32_t  x,
  int32_t  y
  )
{
  UINT32  i;
  INT32   tx;
  UINT32  fw;

  if (mTabs == NULL) {
    return 0;
  }

  if (y < mTabs->y || y >= mTabs->y + mTabs->h
      || x < mTabs->x || x >= mTabs->x + mTabs->w)
  {
    return 0;
  }

  fw = pm_metal_gfx_font_width ();
  tx = mTabs->x + 2;
  for (i = 0; i < mTabs->u.tabs.n; i++) {
    metal_ui_widget_t  *tab;
    INT32               tw;
    UINT32              tlen;

    tab = mTabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tlen = 0;
    while (tab->title[tlen] != '\0') {
      tlen++;
    }

    tw = (INT32)((tlen + 2) * fw) + 16;
    if (tw < 64) {
      tw = 64;
    }

    if (tx + tw > mTabs->x + mTabs->w - 4) {
      break;
    }

    if (x >= tx && x < tx + tw) {
      mTabs->u.tabs.active = i;
      return 1;
    }

    tx += tw + 4;
  }

  return 0;
}

void
pm_metal_ui_cursor_draw (
  int32_t  x,
  int32_t  y
  )
{
  INT32  i;

  /* Simple arrow: vertical stem + tip. */
  for (i = 0; i < 14; i++) {
    pm_metal_gfx_fill_rect (x, y + i, (i < 10) ? 2 : (14 - i), 1,
                            PM_METAL_GFX_RGB (0xff, 0xff, 0xff));
    pm_metal_gfx_fill_rect (x + 1, y + i + 1, 1, 1,
                            PM_METAL_GFX_RGB (0x10, 0x10, 0x10));
  }

  pm_metal_gfx_fill_rect (x, y, 1, 14, PM_METAL_GFX_RGB (0x10, 0x10, 0x10));
}

/*
 * wasi-style import bridge — see util/size.c / util/log.c for signature
 * string rules. '*'/'~' and '$' params arrive already translated.
 */
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
