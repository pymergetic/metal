/*
 * Stage B — impl: common. See fstab.h.
 */
#include "pymergetic/metal/mount/fstab.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/mount/mount.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/port/platform.h"

/* Read file bytes are raw, not NUL-terminated (see port/platform.h) — every
 * line gets copied into this fixed, NUL-terminated scratch buffer before
 * any string function ever touches it. A line longer than this is logged
 * and skipped, not truncated-and-misparsed. */
#define PM_METAL_MOUNT_FSTAB_LINE_MAX 256
#define PM_METAL_MOUNT_FSTAB_MAX_FIELDS 6

/* In-place whitespace tokenizer over a NUL-terminated, caller-owned
 * buffer (never the raw file bytes — see above) — no quoting, matches
 * real fstab's own column shape. Returns the number of fields found
 * (<= max_fields); `line` is mutated (NULs inserted at each boundary,
 * same convention as strtok()). */
static size_t pm_metal_mount_fstab_split(char *line, char *fields[], size_t max_fields)
{
	size_t n = 0;
	char *p = line;

	while (*p && n < max_fields) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (!*p) {
			break;
		}
		fields[n++] = p;
		while (*p && *p != ' ' && *p != '\t') {
			p++;
		}
		if (*p) {
			*p = '\0';
			p++;
		}
	}
	return n;
}

int pm_metal_mount_fstab_apply_fields(const char *source, const char *target, const char *fstype,
				       const char *options)
{
	pm_metal_mount_kind_t kind;

	if (!source || !target || !fstype) {
		fprintf(stderr, "pm_metal_mount: fstab: missing field(s)\n");
		return -1;
	}
	if (pm_metal_mount_kind_by_name(fstype, &kind) != 0) {
		fprintf(stderr, "pm_metal_mount: fstab: unknown fstype '%s'\n", fstype);
		return -1;
	}
	if (pm_metal_mount(target, kind, source, (options && options[0]) ? options : NULL) != 0) {
		fprintf(stderr, "pm_metal_mount: fstab: mount failed: %s %s %s\n", source, target, fstype);
		return -1;
	}
	return 0;
}

int pm_metal_mount_fstab_apply(const char *guest_fstab_path)
{
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	uint8_t *buf;
	uint32_t len;
	uint32_t line_start;

	if (!guest_fstab_path) {
		return 0;
	}
	if (pm_metal_mount_resolve(guest_fstab_path, host_path, sizeof(host_path)) != 0) {
		return 0; /* nothing mounted yet — shouldn't happen once called after Stage A */
	}
	if (!pm_metal_port_file_exists(host_path)) {
		return 0; /* no fstab at all — fully backward compatible, see fstab.h */
	}
	if (pm_metal_port_read_file(host_path, &buf, &len) != 0) {
		fprintf(stderr, "pm_metal_mount: fstab: read failed: %s\n", guest_fstab_path);
		return 0;
	}

	line_start = 0;
	while (line_start < len) {
		uint32_t line_end = line_start;
		uint32_t copy_len;
		char line[PM_METAL_MOUNT_FSTAB_LINE_MAX];
		char *fields[PM_METAL_MOUNT_FSTAB_MAX_FIELDS];
		size_t field_count;

		while (line_end < len && buf[line_end] != '\n') {
			line_end++;
		}
		copy_len = line_end - line_start;
		if (copy_len > 0 && buf[line_start + copy_len - 1] == '\r') {
			copy_len--; /* tolerate CRLF fstab files */
		}
		if (copy_len >= sizeof(line)) {
			fprintf(stderr, "pm_metal_mount: fstab: line too long, skipped\n");
			line_start = line_end + 1;
			continue;
		}
		memcpy(line, buf + line_start, copy_len);
		line[copy_len] = '\0';
		line_start = line_end + 1;

		field_count = pm_metal_mount_fstab_split(line, fields, PM_METAL_MOUNT_FSTAB_MAX_FIELDS);
		if (field_count == 0 || fields[0][0] == '#') {
			continue; /* blank or comment line */
		}
		if (field_count < 3) {
			fprintf(stderr, "pm_metal_mount: fstab: short line, skipped\n");
			continue;
		}

		/* <source> <target> <fstype> <options> <dump> <pass> — dump/pass
		 * (fields[4]/[5], if present at all) are intentionally never
		 * looked at, per fstab.h's own doc comment. */
		pm_metal_mount_fstab_apply_fields(fields[0], fields[1], fields[2],
						   field_count > 3 ? fields[3] : NULL);
	}

	pm_metal_memory_bytecode_ops()->free(buf);
	return 0;
}
