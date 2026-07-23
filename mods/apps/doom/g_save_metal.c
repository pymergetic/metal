/*
 * Phase C — async save/load.
 * Serialize to memory (fmemopen); guest_step does pm_metal_fs_*_async.
 * --wrap=G_SaveGame / G_DoSaveGame / G_DoLoadGame / M_GetSaveGameDir /
 *         M_ReadSaveStrings
 */
/* fmemopen is POSIX.1-2008 — must be visible before <stdio.h>. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * POSIX.1-2008. WASI/musl declare it under _POSIX_C_SOURCE; Metal's
 * freestanding host_stubs <stdio.h> (injected by global clangd -I) does not.
 * Compatible redeclaration when the real libc header already provides it.
 */
FILE *fmemopen(void *buf, size_t size, const char *mode);

#include "metal_doom.h"

#include "../../../external/doomgeneric/doomgeneric/doomdef.h"
#include "../../../external/doomgeneric/doomgeneric/doomstat.h"
#include "../../../external/doomgeneric/doomgeneric/doomtype.h"
#include "../../../external/doomgeneric/doomgeneric/d_englsh.h"
#include "../../../external/doomgeneric/doomgeneric/deh_str.h"
#include "../../../external/doomgeneric/doomgeneric/d_main.h"
#include "../../../external/doomgeneric/doomgeneric/g_game.h"
#include "../../../external/doomgeneric/doomgeneric/i_system.h"
#include "../../../external/doomgeneric/doomgeneric/m_misc.h"
#include "../../../external/doomgeneric/doomgeneric/p_saveg.h"

void __real_G_SaveGame(int slot, char *description);
void __real_G_DoSaveGame(void);
void __real_G_DoLoadGame(void);
char *__real_M_GetSaveGameDir(char *iwadname);
void __real_M_ReadSaveStrings(void);

extern char savename[256];
extern boolean setsizeneeded;
void R_ExecuteSetViewSize(void);
void R_FillBackScreen(void);

/* m_menu.c — not in public headers */
extern char savegamestrings[10][SAVESTRINGSIZE];
typedef struct {
	short status;
	char name[10];
	void (*routine)(int choice);
	char alphaKey;
} metal_menuitem_t;
extern metal_menuitem_t LoadMenu[];

#define SAVEGAMESIZE METAL_DOOM_SAVEGAME_SIZE
#define LOAD_END METAL_DOOM_SAVE_SLOTS

static metal_doom_io_kind_t s_io_kind;
static char s_io_path[256];
static uint8_t *s_io_buf;
static uint32_t s_io_len;

static uint8_t *s_read_buf;
static uint32_t s_read_len;
static char s_read_path[256];

static char s_slot_str[METAL_DOOM_SAVE_SLOTS][METAL_DOOM_SAVE_STRING_SIZE];
static uint8_t s_slot_used[METAL_DOOM_SAVE_SLOTS];
static char s_save_dir[] = METAL_DOOM_SAVE_DIR;

static int s_save_slot;
static char s_save_desc[32];

metal_doom_io_kind_t
metal_doom_io_pending(void)
{
	return s_io_kind;
}

const char *
metal_doom_io_path(void)
{
	return s_io_path;
}

uint8_t *
metal_doom_io_buf(void)
{
	return s_io_buf;
}

uint32_t
metal_doom_io_len(void)
{
	return s_io_len;
}

void
metal_doom_io_clear(void)
{
	if (s_io_kind == METAL_DOOM_IO_SAVE_WRITE && s_io_buf != NULL) {
		free(s_io_buf);
	}

	s_io_kind = METAL_DOOM_IO_NONE;
	s_io_buf  = NULL;
	s_io_len  = 0;
	s_io_path[0] = '\0';
}

void
metal_doom_io_request_write(const char *path, uint8_t *buf, uint32_t len)
{
	if (s_io_kind == METAL_DOOM_IO_SAVE_WRITE && s_io_buf != NULL) {
		free(s_io_buf);
	}

	s_io_kind = METAL_DOOM_IO_SAVE_WRITE;
	s_io_buf  = buf;
	s_io_len  = len;
	M_StringCopy(s_io_path, path, sizeof(s_io_path));
}

void
metal_doom_io_request_read(const char *path)
{
	s_io_kind = METAL_DOOM_IO_SAVE_READ;
	s_io_buf  = NULL;
	s_io_len  = 0;
	M_StringCopy(s_io_path, path, sizeof(s_io_path));
}

void
metal_doom_io_install_read(uint8_t *buf, uint32_t len)
{
	if (s_read_buf != NULL) {
		free(s_read_buf);
	}

	s_read_buf = buf;
	s_read_len = len;
	M_StringCopy(s_read_path, s_io_path, sizeof(s_read_path));
	s_io_kind = METAL_DOOM_IO_NONE;
	s_io_path[0] = '\0';
}

int
metal_doom_io_read_ready(const char *path)
{
	if (path == NULL || s_read_buf == NULL || s_read_len == 0) {
		return 0;
	}

	return strcmp(path, s_read_path) == 0;
}

void
metal_doom_io_abort_load(void)
{
	metal_doom_io_clear();
	gameaction = ga_nothing;
}

void
metal_doom_save_string_set(int slot, const char *s)
{
	if (slot < 0 || slot >= METAL_DOOM_SAVE_SLOTS || s == NULL) {
		return;
	}

	memset(s_slot_str[slot], 0, METAL_DOOM_SAVE_STRING_SIZE);
	strncpy(s_slot_str[slot], s, METAL_DOOM_SAVE_STRING_SIZE - 1);
	s_slot_used[slot] = 1;
}

const char *
metal_doom_save_string_get(int slot)
{
	if (slot < 0 || slot >= METAL_DOOM_SAVE_SLOTS || !s_slot_used[slot]) {
		return EMPTYSTRING;
	}

	return s_slot_str[slot];
}

int
metal_doom_save_slot_used(int slot)
{
	if (slot < 0 || slot >= METAL_DOOM_SAVE_SLOTS) {
		return 0;
	}

	return s_slot_used[slot] ? 1 : 0;
}

char *
__wrap_M_GetSaveGameDir(char *iwadname)
{
	(void)iwadname;
	(void)__real_M_GetSaveGameDir;
	return M_StringDuplicate(s_save_dir);
}

void
__wrap_M_ReadSaveStrings(void)
{
	int i;

	(void)__real_M_ReadSaveStrings;
	for (i = 0; i < LOAD_END; i++) {
		if (s_slot_used[i]) {
			M_StringCopy(savegamestrings[i], s_slot_str[i],
				     SAVESTRINGSIZE);
			LoadMenu[i].status = 1;
		} else {
			M_StringCopy(savegamestrings[i], EMPTYSTRING,
				     SAVESTRINGSIZE);
			LoadMenu[i].status = 0;
		}
	}
}

void
__wrap_G_SaveGame(int slot, char *description)
{
	s_save_slot = slot;
	if (description != NULL) {
		M_StringCopy(s_save_desc, description, sizeof(s_save_desc));
	} else {
		s_save_desc[0] = '\0';
	}

	__real_G_SaveGame(slot, description);
}

void
__wrap_G_DoSaveGame(void)
{
	uint8_t *buf;
	FILE *mem;
	long n;
	char *savegame_file;

	(void)__real_G_DoSaveGame;

	buf = (uint8_t *)malloc(SAVEGAMESIZE);
	if (buf == NULL) {
		I_Error("metal-doom: save malloc fail");
		return;
	}

	memset(buf, 0, SAVEGAMESIZE);
	mem = fmemopen(buf, SAVEGAMESIZE, "w+b");
	if (mem == NULL) {
		free(buf);
		I_Error("metal-doom: save fmemopen fail");
		return;
	}

	save_stream = mem;
	savegame_error = false;

	P_WriteSaveGameHeader(s_save_desc);
	P_ArchivePlayers();
	P_ArchiveWorld();
	P_ArchiveThinkers();
	P_ArchiveSpecials();
	P_WriteSaveGameEOF();

	n = ftell(mem);
	if (vanilla_savegame_limit && n > (long)SAVEGAMESIZE) {
		fclose(mem);
		free(buf);
		I_Error("Savegame buffer overrun");
		return;
	}

	fflush(mem);
	fclose(mem);
	save_stream = NULL;

	if (n < 0) {
		n = 0;
	}

	savegame_file = P_SaveGameFile(s_save_slot);
	metal_doom_io_request_write(savegame_file, buf, (uint32_t)n);
	metal_doom_save_string_set(s_save_slot, s_save_desc);

	gameaction = ga_nothing;
	s_save_desc[0] = '\0';
	players[consoleplayer].message = DEH_String(GGSAVED);
	R_FillBackScreen();
}

void
__wrap_G_DoLoadGame(void)
{
	int savedleveltime;

	(void)__real_G_DoLoadGame;

	if (!metal_doom_io_read_ready(savename)) {
		metal_doom_io_request_read(savename);
		gameaction = ga_loadgame;
		return;
	}

	gameaction = ga_nothing;

	save_stream = fmemopen(s_read_buf, s_read_len, "rb");
	if (save_stream == NULL) {
		return;
	}

	savegame_error = false;

	if (!P_ReadSaveGameHeader()) {
		fclose(save_stream);
		save_stream = NULL;
		return;
	}

	savedleveltime = leveltime;
	G_InitNew(gameskill, gameepisode, gamemap);
	leveltime = savedleveltime;

	P_UnArchivePlayers();
	P_UnArchiveWorld();
	P_UnArchiveThinkers();
	P_UnArchiveSpecials();

	if (!P_ReadSaveGameEOF()) {
		I_Error("Bad savegame");
	}

	fclose(save_stream);
	save_stream = NULL;

	free(s_read_buf);
	s_read_buf = NULL;
	s_read_len = 0;
	s_read_path[0] = '\0';

	if (setsizeneeded) {
		R_ExecuteSetViewSize();
	}

	R_FillBackScreen();
}
