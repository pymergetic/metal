/** @file
  Private UI chrome — widget tree, layout/paint, shell globals.
  Not part of the public guest/host ABI (see include/.../shell/ui/ui.h).
**/
#ifndef PYMERGETIC_METAL_SHELL_UI_PRIV_H_
#define PYMERGETIC_METAL_SHELL_UI_PRIV_H_

#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/dev/gfx/gfx.h>

#include <stdint.h>

#define COL_DESKTOP     PM_METAL_GFX_RGB(0x4a, 0x4a, 0x4a)
#define COL_WINDOW      PM_METAL_GFX_RGB(0xaa, 0xaa, 0xaa)
#define COL_TITLE       PM_METAL_GFX_RGB(0x5c, 0x4e, 0x8c)
#define COL_TITLE_TXT   PM_METAL_GFX_RGB(0xff, 0xff, 0xff)
#define COL_TAB         PM_METAL_GFX_RGB(0x8a, 0x8a, 0x8a)
#define COL_TAB_ON      PM_METAL_GFX_RGB(0xc0, 0xc0, 0xc0)
#define COL_TAB_HOVER   PM_METAL_GFX_RGB(0xa8, 0xa8, 0xa8)
#define COL_TAB_OFF     PM_METAL_GFX_RGB(0x7a, 0x7a, 0x7a)
#define COL_TAB_TXT     PM_METAL_GFX_RGB(0x10, 0x10, 0x10)
#define COL_BEVEL_HI    PM_METAL_GFX_RGB(0xe8, 0xe8, 0xe8)
#define COL_BEVEL_LO    PM_METAL_GFX_RGB(0x40, 0x40, 0x40)
#define COL_FRAME_FACE  PM_METAL_GFX_RGB(0x9a, 0x9a, 0x9a)
#define COL_CONSOLE_BG  PM_METAL_GFX_RGB(0x1a, 0x1a, 0x22)
#define COL_CONSOLE_FG  PM_METAL_GFX_RGB(0xc8, 0xe6, 0xc8)
#define COL_LOG_DIM     PM_METAL_GFX_RGB(0x70, 0x80, 0x70)
#define COL_LOG_OK      PM_METAL_GFX_RGB(0x50, 0xe0, 0x70)
#define COL_LOG_WARN    PM_METAL_GFX_RGB(0xe0, 0xc0, 0x40)
#define COL_LOG_FAIL    PM_METAL_GFX_RGB(0xd0, 0x55, 0x55)
#define COL_LOG_ACCENT  PM_METAL_GFX_RGB(0x60, 0xd0, 0xe8)
#define COL_PROMPT_PATH PM_METAL_GFX_RGB(0x55, 0xa0, 0xff) /* :~ — match ANSI 34 */
#define COL_REPL_PROMPT PM_METAL_GFX_RGB(0xe0, 0x60, 0xf0) /* >>> / ... — match ANSI 35 */
#define COL_INPUT_FG    PM_METAL_GFX_RGB(0xff, 0xff, 0xcc)
#define COL_STATUS      PM_METAL_GFX_RGB(0x6a, 0x6a, 0x6a)
#define COL_STATUS_TXT  PM_METAL_GFX_RGB(0xf0, 0xf0, 0xf0)
#define COL_STATUS_CLK  PM_METAL_GFX_RGB(0x55, 0x55, 0x55)
#define COL_NET_UP      PM_METAL_GFX_RGB(0x50, 0xe0, 0x70)
#define COL_NET_DOWN    PM_METAL_GFX_RGB(0xd0, 0x55, 0x55)

#define UI_FONT_W      8
#define UI_CLOCK_CHARS 5
#define UI_FPS_CHARS   6 /* e.g. "60fps" / "999fps" */
#define UI_KEYB_CHARS  2 /* layout ids are always 2 letters: "us" / "de" / ... */

#define UI_MARGIN    0 /* shell window fills the scanout */
#define UI_TITLE_H   28
#define UI_TAB_H     26
#define UI_STATUS_H  24
#define UI_FRAME_PAD 6
#define UI_SCROLL_W  10

#define CONSOLE_LINES 1024
/*
 * Bumped from 160: the boot-time ASCII banner is now colorized with one
 * 24-bit truecolor SGR escape ("\033[38;2;r;g;bm", ~19 bytes) per hue
 * change, and each stored console line is this many bytes *total*
 * (escapes included, see MetalUiConsolePutsStyled) -- 160 bytes was only
 * ~8 color changes' worth of headroom for a ~45-char banner row. 400
 * gives a real fluid-looking gradient (many hue steps) without truncating
 * escapes mid-sequence (which would leak raw "\033[38;2;...m" text onto
 * the screen instead of color).
 */
#define CONSOLE_COLS      400
#define CONSOLE_BYTES_MAX (CONSOLE_LINES * CONSOLE_COLS)
#define STATUS_CHARS      128
#define TITLE_CHARS       48
#define INPUT_CHARS       512
#define MAX_TABS          16

#define COL_SCROLL_TRACK PM_METAL_GFX_RGB(0x28, 0x28, 0x30)
#define COL_SCROLL_THUMB PM_METAL_GFX_RGB(0x70, 0x80, 0x70)
#define COL_SCROLL_EDGE  PM_METAL_GFX_RGB(0x40, 0x40, 0x48)

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
  metal_ui_kind_t        kind;
  int32_t                x;
  int32_t                y;
  int32_t                w;
  int32_t                h;
  char                   title[TITLE_CHARS];
  int32_t                closable;
  pm_metal_ui_handle_t   handle;
  pm_metal_gfx_surface_h surface;
  metal_ui_widget_t     *parent;
  metal_ui_widget_t     *child;
  metal_ui_widget_t     *next;
  union {
    struct {
      char     lines[CONSOLE_LINES][CONSOLE_COLS];
      uint8_t  styles[CONSOLE_LINES]; /* pm_metal_log_style_t */
      uint32_t count;
      uint32_t head;
      uint32_t view_off; /* lines scrolled up from bottom; 0 = stick */
      char     input[INPUT_CHARS];
      uint32_t input_len;
      uint32_t input_cursor; /* byte index 0..input_len */
      int32_t  show_input;
      int32_t  cursor_on;
    } console;
    struct {
      uint32_t           active;
      int32_t            hover; /* -1 = none */
      metal_ui_widget_t *tabs[MAX_TABS];
      uint32_t           n;
    } tabs;
    struct {
      char text[STATUS_CHARS];
    } status;
  } u;
};

/* Shell singleton (owned by shell.c, used across modules). */
extern metal_ui_widget_t   *gMetalUiByHandle[MAX_TABS + 1];
extern metal_ui_widget_t   *gMetalUiShellRoot;
extern metal_ui_widget_t   *gMetalUiTabs;
extern metal_ui_widget_t   *gMetalUiSysConsole;
extern metal_ui_widget_t   *gMetalUiStatus;
extern pm_metal_ui_handle_t gMetalUiConsoleHandle;

/* widget.c */
metal_ui_widget_t   *MetalUiAlloc(metal_ui_kind_t kind);
void                 MetalUiAttach(metal_ui_widget_t *parent, metal_ui_widget_t *child);
void                 MetalUiDetach(metal_ui_widget_t *parent, metal_ui_widget_t *child);
void                 MetalUiDestroyTree(metal_ui_widget_t *w);
pm_metal_ui_handle_t MetalUiHandleAlloc(metal_ui_widget_t *tab);
void                 MetalUiHandleFree(pm_metal_ui_handle_t h);
metal_ui_widget_t   *MetalUiTabFromHandle(pm_metal_ui_handle_t h);
int32_t              MetalUiTabIndex(metal_ui_widget_t *tab);
/** Tab strip hit-test; -1 if (x,y) is not on a tab label. */
int32_t            MetalUiTabIndexAt(int32_t x, int32_t y);
metal_ui_widget_t *MetalUiTabConsole(metal_ui_widget_t *tab);
metal_ui_widget_t *MetalUiActiveConsole(void);
void               MetalUiConsolePuts(metal_ui_widget_t *con, const char *line);
void MetalUiConsolePutsStyled(metal_ui_widget_t *con, const char *line, pm_metal_log_style_t style);
uint32_t MetalUiConsoleVisibleRows(metal_ui_widget_t *con);
/**
 * count + live composing-line rows, when @a con is the one console that
 * ever shows a live input line (see MetalUiConsoleLiveInputRows, paint.c)
 * and that line isn't currently suppressed by a fullscreen/windowed guest.
 * The single scroll region (history AND the line you're typing) is sized
 * off this, not off count alone -- "whole tab is one scrollable console,
 * no separate input strip" (see docs/... rationale in paint.c).
 */
uint32_t MetalUiConsoleTotalRows(metal_ui_widget_t *con);
uint32_t MetalUiConsoleMaxOff(metal_ui_widget_t *con);
void     MetalUiConsoleClampView(metal_ui_widget_t *con);
void     MetalUiConsoleScrollBy(metal_ui_widget_t *con, int32_t delta_lines);
void     MetalUiConsoleScrollTo(metal_ui_widget_t *con, uint32_t view_off);
/**
 * Scrollbar geometry in screen coords. Returns 1 if a thumb is shown.
 */
int32_t            MetalUiConsoleScrollBarGeom(metal_ui_widget_t *con,
                                               int32_t           *track_x,
                                               int32_t           *track_y,
                                               int32_t           *track_w,
                                               int32_t           *track_h,
                                               int32_t           *thumb_y,
                                               int32_t           *thumb_h);
metal_ui_widget_t *MetalUiMakeTabBody(const char *title, int32_t closable, int32_t show_input);

/* input.c — multiline metrics (used by layout/paint); view-scroll for the
 * composing line lives on the console's own view_off now (see
 * MetalUiConsoleTotalRows above) -- no separate input-side scroll state. */
uint32_t MetalUiInputWrapColsFromWidth(int32_t width_px);
uint32_t MetalUiInputCurrentWrap(void);
uint32_t MetalUiInputVisualRows(uint32_t wrap_cols);
int32_t  MetalUiInputMoveCursor(int32_t delta);
/** Move by visual line; 1=moved, 0=at edge (caller may history-recall). */
int32_t  MetalUiInputMoveVisualRow(int32_t delta_rows, uint32_t wrap_cols);
uint32_t MetalUiInputCaretRow(uint32_t wrap_cols);
void     MetalUiInputCaretCell(uint32_t wrap_cols, uint32_t *row, uint32_t *col);
/** Copy glyphs for visual row into out (no NUL in middle); returns length. */
uint32_t MetalUiInputVisualRowText(uint32_t wrap_cols,
                                   uint32_t visual_row,
                                   char    *out,
                                   uint32_t out_cap);

/* paint.c */
void MetalUiLayout(void);
void MetalUiPaint(void);
/**
 * Repaint just the shared console widget (scrollback + the live composing
 * line folded into its last row(s) -- see MetalUiPaintConsole). Used for
 * the cheap per-keystroke dirty-rect present path (shell.c's MarkInput),
 * as opposed to MetalUiPaint()'s full chrome (tabs strip + status bar too).
 */
void MetalUiPaintSysConsole(void);
void MetalUiPaintStatusBarOnly(void);
/**
 * True + suppresses the live composing line for this frame: a fullscreen
 * guest owns the whole screen, or a windowed guest owns this tab's content
 * rect (see MetalUiConsoleLiveInputRows, paint.c) -- same rationale as
 * MetalUiTabGuestOwnsContent.
 */
uint32_t MetalUiConsoleLiveInputRows(metal_ui_widget_t *con);
int32_t  MetalUiStatusGeom(int32_t *x, int32_t *y, int32_t *w, int32_t *h);
/** 1 if clock/systray/FPS content differs from last paint. */
int32_t MetalUiStatusNeedsRefresh(void);

#endif /* PYMERGETIC_METAL_SHELL_UI_PRIV_H_ */
