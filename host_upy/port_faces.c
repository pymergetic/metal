/* port_faces.c — the host seat's embedded-µPy port faces (ports/embed route).
 *
 * The embed package ships the py core + gchelper but leaves the !MICROPY_VFS
 * port faces to the integrator (py/builtin.h: "A port can provide these
 * functions"). This seat has no filesystem for the embedded kernel: modules
 * arrive through the object loop (mpy bytes in memory via jit.py), never
 * from a path. The three faces below are the browser seat's exact
 * !MICROPY_VFS posture (ports/webassembly/main.c) — the established
 * in-tree pattern for a no-filesystem µPy seat:
 *
 *   - mp_lexer_new_from_file: loud ENOENT (nothing lexes from a path)
 *   - mp_import_stat: NO_EXIST for every path (no stray import half-opens)
 *   - mp_builtin_open: none (io's open exists, opens nothing)
 *
 * Ownership/lifetime: no state, no allocation — pure refusal faces.
 */
#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

#if !MICROPY_VFS

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)args;
    (void)kwargs;
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

#endif
