#include <pymergetic/metal/sys/hostinfo.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int mkdir_p(const char *path)
{
	char buf[512];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(buf)) {
		return -1;
	}

	memcpy(buf, path, len + 1);

	for (char *p = buf + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
			return -1;
		}
		*p = '/';
	}

	if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
		return -1;
	}

	return 0;
}

int pm_metal_sys_hostinfo_publish(const char *vfs_root, const void *blob, size_t blob_len)
{
	char path[512];
	FILE *f;

	if (vfs_root == NULL || blob == NULL || blob_len == 0) {
		return -1;
	}

	if (mkdir_p(vfs_root) != 0) {
		return -1;
	}

	if (snprintf(path, sizeof(path), "%s/bootstrap", vfs_root) >= (int)sizeof(path)) {
		return -1;
	}

	f = fopen(path, "wb");
	if (f == NULL) {
		return -1;
	}

	if (fwrite(blob, 1, blob_len, f) != blob_len) {
		fclose(f);
		return -1;
	}

	fclose(f);
	return 0;
}
