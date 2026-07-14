/*
 * Port contract — list (or just check the existence of) one real host
 * directory. Backs the shell's `ls`/`cd` builtins (see
 * shell/commands/ls.c, cd.c) — the one other piece of raw host filesystem
 * access this codebase needs, alongside port/platform.h's read_file().
 */
#ifndef PYMERGETIC_METAL_PORT_DIR_H_
#define PYMERGETIC_METAL_PORT_DIR_H_

/* impl: bind — src/linux/pymergetic/metal/port/dir.c (opendir/readdir)
 *              src/zephyr/pymergetic/metal/port/dir.c (stub — deferred with the rest of zephyr's shell, see docs/RUNTIME.md "Bring-up plan")
 *
 * Opens host_path as a directory. If `visit` is non-NULL, calls
 * visit(name, is_dir, ctx) once per entry ("." and ".." skipped, order
 * unspecified — same as readdir(3) itself) before returning; pass
 * visit=NULL to just check host_path is a real, listable directory
 * without paying for a full enumeration you'd throw away (see
 * shell/commands/cd.c, which uses exactly that to validate a target
 * before committing to it). Returns 0/-1 (does not exist, or isn't a
 * directory). */
int pm_metal_port_dir_list(const char *host_path, void (*visit)(const char *name, int is_dir, void *ctx),
			    void *ctx);

#endif /* PYMERGETIC_METAL_PORT_DIR_H_ */
