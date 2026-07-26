/*
 * Mod registry — load/unload hooks; functions + commands → process.
 *
 * Contract: docs/MODS.md
 *   Loader calls only pm_metal_mod_on_load / pm_metal_mod_on_unload.
 *   Mod registers funcs/cmds from on_load. No magic step export.
 *   process = registered command runs a function in a task.
 *   Guests use the same load/unload/cmd API as the host (mod→mod).
 *
 * Convenience umbrella — one #include for the whole API. Split into
 * sub-headers because this got too long to scan as one file; call
 * sites don't need to change or care which one a symbol lives in:
 *   mod_types.h      ids/handles, enums, POD structs (about/author/cap/...)
 *   mod_lifecycle.h  load/unload/ready/reset, set_capability, set_about/
 *                    about_get, register_func/register_cmd (on_load-time API)
 *   mod_call.h       cmd_invoke, func/cmd resolve, fn_coro, fn_process
 *   mod_fresh.h      fresh_open/fresh_resolve/fresh_close
 *   mod_core.h       host-internal hooks (session/coro/native_register)
 * Only need one group's types (e.g. boot/authors.h just needs
 * pm_metal_mod_about_t)? Include that sub-header directly instead.
 *
 * impl: src/pymergetic/metal/guest/mod/mod.c
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_H_

#include <pymergetic/metal/guest/mod/mod_call.h>      /* IWYU pragma: export */
#include <pymergetic/metal/guest/mod/mod_core.h>      /* IWYU pragma: export */
#include <pymergetic/metal/guest/mod/mod_fresh.h>     /* IWYU pragma: export */
#include <pymergetic/metal/guest/mod/mod_lifecycle.h> /* IWYU pragma: export */
#include <pymergetic/metal/guest/mod/mod_types.h>     /* IWYU pragma: export */

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_H_ */
