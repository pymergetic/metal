/*
 * T3 — exercises util/{size,log,arena}.h's wasi-style imports: on
 * wasm32-wasip1 none of these have a local body in this .wasm at all (see
 * their own headers) — every call below is resolved against each host
 * module's own native registration (src/common/pymergetic/metal/util/
 * {size,arena,log}.c, each under its own PM_METAL_UTIL_{SIZE,ARENA,LOG}_
 * WASI_MODULE import name, see util/wasi.h) at instantiate() time.
 * `arena_init()` is called over this mod's *own* stack buffer to prove
 * the host really is reading/writing this guest's linear memory through
 * the app<->native address translation, not some unrelated host-side
 * buffer.
 */
#include <pymergetic/metal/util/arena.h>
#include <pymergetic/metal/util/log.h>
#include <pymergetic/metal/util/size.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
	char human[PM_METAL_UTIL_SIZE_FORMAT_MAX];
	pm_metal_util_size_format(human, sizeof(human), 92946432ULL);
	printf("t3_util_native: size=%s\n", human);

	pm_metal_util_log_set_level(PM_METAL_LOG_WARN);
	char name[8];
	pm_metal_util_log_level_name(pm_metal_util_log_get_level(), name, sizeof(name));
	printf("t3_util_native: level=%s\n", name);

	/* Below the floor just set — must be a silent no-op, not an error. */
	pm_metal_util_log_write(PM_METAL_LOG_STREAM_STDOUT, PM_METAL_LOG_INFO, "should not appear");
	pm_metal_util_log_write(PM_METAL_LOG_STREAM_STDOUT, PM_METAL_LOG_ERROR, "t3_util_native: at/above floor");
	pm_metal_util_log_write_raw(PM_METAL_LOG_STREAM_STDOUT, "t3_util_native: raw, unfiltered");

	static unsigned char buf[256];
	pm_metal_util_arena_t *arena = pm_metal_util_arena_init(buf, sizeof(buf));
	if (!arena) {
		printf("t3_util_native: arena_init failed\n");
		return 1;
	}

	void *a = pm_metal_util_arena_alloc(arena, 32);
	void *b = pm_metal_util_arena_alloc(arena, 32);
	if (!a || !b) {
		printf("t3_util_native: arena_alloc failed\n");
		return 1;
	}

	/* Prove the host really did write through into *this* module's own
	 * memory — not into some unrelated host-side buffer. */
	memset(a, 0xAB, 32);
	printf("t3_util_native: a[0]=0x%02x used=%zu\n", ((unsigned char *)a)[0],
	       pm_metal_util_arena_used(arena));

	pm_metal_util_arena_free(arena, a);
	pm_metal_util_arena_free(arena, b);
	printf("t3_util_native: used_after_free=%zu\n", pm_metal_util_arena_used(arena));

	return 0;
}
