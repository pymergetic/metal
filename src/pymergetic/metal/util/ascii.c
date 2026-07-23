/*
 * pm_metal_util_ascii_* — impl: common (see util/ascii.h; wasm32 mods
 * reach this via wasi-style import registration at the bottom).
 *
 * Classic FIGlet "small" letterforms (Glenn Chappell / FIGlet fonts,
 * freely redistributable) — not solid '#' blobs from the VGA pixel
 * font. ~5 rows tall (~1/3 of the old 16-row raster banners).
 */
#include "pymergetic/metal/util/ascii.h"

#include <string.h>

#include "pymergetic/metal/log/log.h"

#include "ascii_fig_small.inc.c"

#define PM_METAL_ASCII_MAX_W  160
#define PM_METAL_ASCII_MAX_H  32

static const pm_metal_ascii_fig_glyph_t *
metal_ascii_glyph(unsigned char ch)
{
	if (ch < PM_METAL_ASCII_FIG_FIRST
	    || ch >= PM_METAL_ASCII_FIG_FIRST + PM_METAL_ASCII_FIG_COUNT)
	{
		ch = (unsigned char)'?';
	}
	return &mFigFont[ch - PM_METAL_ASCII_FIG_FIRST];
}

static int metal_ascii_glyph_width(const pm_metal_ascii_fig_glyph_t *g)
{
	int w = 0;
	int r;
	int c;

	for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
		c = (int)strlen(g->row[r]);
		while (c > 0 && g->row[r][c - 1] == ' ') {
			c--;
		}
		if (c > w) {
			w = c;
		}
	}
	return w;
}

size_t pm_metal_util_ascii_bound(size_t text_len)
{
	size_t w;
	size_t h;

	if (text_len == 0) {
		return 1;
	}

	/* worst case: each char at max glyph width + newlines */
	w = text_len * (size_t)PM_METAL_ASCII_FIG_MAX_W;
	h = (size_t)PM_METAL_ASCII_FIG_H * (text_len + 1u);
	if (w > (size_t)PM_METAL_ASCII_MAX_W) {
		w = (size_t)PM_METAL_ASCII_MAX_W;
	}
	if (h > (size_t)PM_METAL_ASCII_MAX_H) {
		h = (size_t)PM_METAL_ASCII_MAX_H;
	}
	return h * (w + 1u) + 1u;
}

int pm_metal_util_ascii_render(const char *text, char ink, char *out, size_t out_cap)
{
	static char lines[PM_METAL_ASCII_FIG_H][PM_METAL_ASCII_MAX_W + 1];
	int lens[PM_METAL_ASCII_FIG_H];
	int r;
	int c;
	int end;
	size_t o;
	const char *p;

	(void)ink; /* FIGlet glyphs already carry their own ink chars */

	if (!text || !out || out_cap == 0) {
		return -1;
	}

	for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
		lines[r][0] = '\0';
		lens[r] = 0;
	}

	for (p = text; *p != '\0'; p++) {
		const pm_metal_ascii_fig_glyph_t *g;
		int gw;

		if (*p == '\n') {
			/* only single-line banners for now — ignore extra */
			continue;
		}

		g = metal_ascii_glyph((unsigned char)*p);
		gw = metal_ascii_glyph_width(g);
		for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
			if (lens[r] + gw > PM_METAL_ASCII_MAX_W) {
				return -1;
			}
			for (c = 0; c < gw; c++) {
				char ch = g->row[r][c];

				lines[r][lens[r] + c] = (ch != '\0') ? ch : ' ';
			}
			lens[r] += gw;
			lines[r][lens[r]] = '\0';
		}
	}

	/* trim trailing blank fig rows (small font often pads last line) */
	end = PM_METAL_ASCII_FIG_H - 1;
	while (end >= 0) {
		c = lens[end];
		while (c > 0 && lines[end][c - 1] == ' ') {
			c--;
		}
		if (c > 0) {
			break;
		}
		end--;
	}

	o = 0;
	for (r = 0; r <= end; r++) {
		c = lens[r];
		while (c > 0 && lines[r][c - 1] == ' ') {
			c--;
		}
		if (o + (size_t)c + 1u >= out_cap) {
			return -1;
		}
		memcpy(out + o, lines[r], (size_t)c);
		o += (size_t)c;
		if (r < end) {
			out[o++] = '\n';
		}
	}

	if (o >= out_cap) {
		return -1;
	}
	out[o] = '\0';
	return (int)o;
}

void pm_metal_util_ascii_log_styled(pm_metal_log_style_t style, const char *text)
{
	char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
	int n;
	int i;
	int start;

	if (!text) {
		return;
	}

	n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
	if (n < 0) {
		return;
	}

	start = 0;
	for (i = 0; i <= n; i++) {
		if (i == n || out[i] == '\n') {
			out[i] = '\0';
			pm_metal_log_styled(style, out + start);
			start = i + 1;
		}
	}
}

void pm_metal_util_ascii_log(const char *text)
{
	pm_metal_util_ascii_log_styled(PM_METAL_LOG_STYLE_DEFAULT, text);
}

#include "wasm_export.h"

static int32_t pm_metal_util_ascii_bound_native(wasm_exec_env_t exec_env, uint32_t text_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_ascii_bound((size_t)text_len);
}

static int32_t pm_metal_util_ascii_render_native(wasm_exec_env_t exec_env, const char *text,
						   int32_t ink, char *out, uint32_t out_cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_ascii_render(text, (char)ink, out, (size_t)out_cap);
}

static void pm_metal_util_ascii_log_native(wasm_exec_env_t exec_env, const char *text)
{
	(void)exec_env;
	pm_metal_util_ascii_log(text);
}

static NativeSymbol g_pm_metal_util_ascii_native_symbols[] = {
	{"pm_metal_util_ascii_bound", (void *)pm_metal_util_ascii_bound_native, "(i)i", NULL},
	{"pm_metal_util_ascii_render", (void *)pm_metal_util_ascii_render_native, "($i*~)i", NULL},
	{"pm_metal_util_ascii_log", (void *)pm_metal_util_ascii_log_native, "($)", NULL},
};

int pm_metal_util_ascii_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_ASCII_WASI_MODULE,
					    g_pm_metal_util_ascii_native_symbols,
					    sizeof(g_pm_metal_util_ascii_native_symbols)
						    / sizeof(g_pm_metal_util_ascii_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
