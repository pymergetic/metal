/*
 * Shared Metal doom guest glue (not part of vanilla doomgeneric).
 */
#ifndef METAL_DOOM_H_
#define METAL_DOOM_H_

#include <stdint.h>

#ifndef METAL_DOOM_IWAD
#define METAL_DOOM_IWAD "/mods/apps/doom/doom1.wad"
#endif

#ifndef METAL_DOOM_SAVE_DIR
#define METAL_DOOM_SAVE_DIR "/mods/apps/doom/saves/"
#endif

#define METAL_DOOM_SAVE_SLOTS 6
#define METAL_DOOM_SAVE_STRING_SIZE 24
#define METAL_DOOM_SAVEGAME_SIZE 0x2c000u

/* Quit / error */
void metal_doom_request_quit(int code);
int metal_doom_quit_requested(void);
int metal_doom_quit_code(void);

/* Wipe clock + coop wipe flag (one melt frame / Tick). */
void metal_doom_sleep_bump_ms(uint32_t ms);
uint32_t metal_doom_fake_ms(void);
int metal_doom_wipe_active(void);
void metal_doom_wipe_set_active(int active);

/* Async IWAD preload → memory wad. */
int metal_doom_wad_install(uint8_t *buf, uint32_t len);
int metal_doom_wad_ready(void);

/* Present fence after DG_DrawFrame (blit marks dirty; await async_frame). */
uint32_t metal_doom_present_surface(void);
void metal_doom_clear_present(void);

/* Phase C — pending save I/O for guest_step. */
typedef enum {
	METAL_DOOM_IO_NONE = 0,
	METAL_DOOM_IO_SAVE_WRITE,
	METAL_DOOM_IO_SAVE_READ
} metal_doom_io_kind_t;

metal_doom_io_kind_t metal_doom_io_pending(void);
const char *metal_doom_io_path(void);
uint8_t *metal_doom_io_buf(void);
uint32_t metal_doom_io_len(void);
void metal_doom_io_clear(void);
void metal_doom_io_request_write(const char *path, uint8_t *buf, uint32_t len);
void metal_doom_io_request_read(const char *path);
void metal_doom_io_install_read(uint8_t *buf, uint32_t len);
int metal_doom_io_read_ready(const char *path);
void metal_doom_io_abort_load(void);

/* Slot title cache for M_ReadSaveStrings (no sync fopen). */
void metal_doom_save_string_set(int slot, const char *s);
const char *metal_doom_save_string_get(int slot);
int metal_doom_save_slot_used(int slot);

/* Phase D — audio drain backpressure for stem. */
int metal_doom_audio_drain_pending(void);
uint32_t metal_doom_audio_drain_stream(void);
uint32_t metal_doom_audio_drain_nbytes(void);
void metal_doom_audio_drain_clear(void);
void metal_doom_audio_request_drain(uint32_t stream, uint32_t nbytes);

#endif /* METAL_DOOM_H_ */
