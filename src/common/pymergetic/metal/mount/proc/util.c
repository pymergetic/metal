#include "pymergetic/metal/mount/proc/util.h"

#include <string.h>

int pm_metal_mount_proc_put_str(char *out, size_t cap, size_t *out_len, const char *s)
{
	size_t len;

	if (!out || !out_len || !s) {
		return -1;
	}
	len = strlen(s);
	if (len >= cap) {
		return -1;
	}
	memcpy(out, s, len + 1);
	*out_len = len;
	return 0;
}

int pm_metal_mount_proc_put_nul_list(char *out, size_t cap, size_t *out_len, int count,
				     const char *const *items)
{
	size_t used = 0;
	int i;

	if (!out || !out_len || cap == 0) {
		return -1;
	}
	if (count <= 0 || !items) {
		out[0] = '\0';
		*out_len = 0;
		return 0;
	}
	for (i = 0; i < count; i++) {
		const char *s = items[i] ? items[i] : "";
		size_t len = strlen(s);

		if (used + len + 1 >= cap) {
			return -1;
		}
		memcpy(out + used, s, len);
		used += len;
		out[used++] = '\0';
	}
	*out_len = used;
	return 0;
}
