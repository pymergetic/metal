/*
 * Runtime — export-style env inheritance for a respawn()ed "subshell".
 * Real shells split their variables into two kinds: local (visible only
 * to that shell itself) and exported (also handed to every child it
 * spawns) — `export FOO` is the difference between the two. There is no
 * shell in this tree anymore, and no WASI import lets a guest spawn()
 * another process itself (only a native host driver can call
 * runtime/process.h's spawn()) — but any *future* driver that wants
 * that same local/exported split (a shell-like mod driven through some
 * future host-mediated spawn import, or a host-side CLI wrapper) needs
 * exactly this: a correct, reusable way to turn "here is my whole
 * variable table, some local, some exported" into the plain
 * envc/envp pm_metal_process_spawn() already expects, without
 * hand-rolling string concatenation and malloc bookkeeping at every
 * call site. Host-only — nothing here runs inside a mod.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_ENV_H_
#define PYMERGETIC_METAL_RUNTIME_ENV_H_

typedef struct pm_metal_env_var {
	const char *name;
	const char *value;
	int exported; /* 0 = local to this driver only; nonzero = inherited by a respawned child */
} pm_metal_env_var_t;

/* impl: common — src/common/pymergetic/metal/runtime/env.c
 *
 * build_exported(): walks `vars` (`count` entries) and mallocs a
 * "NAME=VALUE" envp/envc array containing every entry whose `exported`
 * is nonzero, in the same relative order, skipping every local one —
 * the exact shape spawn()'s own envc/envp parameters expect, so the
 * result can be passed straight through with no further conversion.
 * `vars` itself is only read, never retained past this call. Free the
 * result with free_exported() below once the respawn()ed child no
 * longer needs it (spawn() itself already copies envp internally — see
 * process.h — so this can be freed right after that call returns, no
 * need to keep it alive for the child's whole lifetime). Returns 0/-1
 * (bad args, or out of memory — on -1, nothing is left half-built:
 * anything already allocated is freed first). */
int pm_metal_env_build_exported(const pm_metal_env_var_t *vars, int count, char ***out_envp, int *out_envc);

/* impl: common — src/common/pymergetic/metal/runtime/env.c
 *
 * free_exported(): frees exactly what build_exported() above returned.
 * Safe on a NULL envp with envc == 0 (i.e. a `vars` with zero exported
 * entries) — build_exported() returns exactly that shape in that case,
 * not an error. */
void pm_metal_env_free_exported(char **envp, int envc);

#endif /* PYMERGETIC_METAL_RUNTIME_ENV_H_ */
