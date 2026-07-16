/*
 * T21 — read Metal /proc nodes answered by hooks (virtual proc).
 */
#include <stdio.h>
#include <string.h>

static int dump_file(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[256];
	int n = 0;

	if (!f) {
		printf("t21_proc_mounts: open %s failed\n", path);
		return 1;
	}
	printf("--- %s ---\n", path);
	while (n < 8 && fgets(line, sizeof(line), f)) {
		fputs(line, stdout);
		n++;
	}
	if (n >= 8 && !feof(f)) {
		printf("...\n");
	}
	fclose(f);
	return 0;
}

/* cmdline/environ are NUL-separated — print printable preview. */
static int dump_nul_file(const char *path, const char *label)
{
	FILE *f = fopen(path, "rb");
	char buf[256];
	size_t n;
	size_t i;

	if (!f) {
		printf("t21_proc_mounts: open %s failed\n", path);
		return 1;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	printf("--- %s (%zu bytes preview) ---\n", label, n);
	for (i = 0; i < n; i++) {
		putchar(buf[i] ? buf[i] : ' ');
	}
	putchar('\n');
	return n > 0 ? 0 : 1;
}

int main(void)
{
	if (dump_file("/proc/mounts") != 0) {
		return 1;
	}
	if (dump_file("/proc/filesystems") != 0) {
		return 1;
	}
	if (dump_file("/proc/version") != 0) {
		return 1;
	}
	if (dump_file("/proc/uptime") != 0) {
		return 1;
	}
	if (dump_file("/proc/meminfo") != 0) {
		return 1;
	}
	if (dump_file("/proc/cpuinfo") != 0) {
		return 1;
	}
	if (dump_nul_file("/proc/self/cmdline", "/proc/self/cmdline") != 0) {
		return 1;
	}
	if (dump_nul_file("/proc/self/environ", "/proc/self/environ") != 0) {
		return 1;
	}
	return 0;
}
