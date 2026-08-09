#!/usr/bin/env python3
"""Rewrite pointer-heavy glue faces from C headers with correct types."""
from __future__ import annotations

import re
from pathlib import Path


def parse_header(path: Path):
    text = path.read_text()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    decls = []
    for m in re.finditer(
        r"^(?!#)\s*((?:[\w\s]|const)+?\*?)\s*(pm_metal_[A-Za-z0-9_]+)\s*\(([^;]*)\)\s*;",
        text,
        re.M,
    ):
        ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
        if "typedef" in ret:
            continue
        decls.append((ret, name, args))
    return decls


def gen(seat, include, decls, strip_prefs):
    short = seat.split(".")[-1]
    mod = "mp_module_pymergetic_metal_" + seat.replace(".", "_")
    qstr = "pymergetic_dot_metal"
    for p in seat.split("."):
        qstr += "_dot_" + p
    lines = [
        f"/* pymergetic.metal.{seat} — µPy face (pointer-safe). */",
        '#include "py/obj.h"',
        '#include "py/objstr.h"',
        '#include "py/runtime.h"',
        f"#include <{include}>",
        "",
    ]
    objs = []
    for ret, name, args in decls:
        pyname = name
        for pref in strip_prefs:
            if name.startswith(pref):
                pyname = name[len(pref) :]
                break
        ret_s = " ".join(ret.split())
        # Struct-by-value / non-scalar returns cannot cast to mp_int_t.
        # size_t / uint*_t / int*_t are scalars despite the _t suffix.
        _scalar_t = {
            "size_t",
            "ssize_t",
            "ptrdiff_t",
            "uintptr_t",
            "intptr_t",
            "uint8_t",
            "uint16_t",
            "uint32_t",
            "uint64_t",
            "int8_t",
            "int16_t",
            "int32_t",
            "int64_t",
        }
        ret_is_struct = (
            ("*" not in ret_s)
            and ret_s not in _scalar_t
            and (ret_s.endswith("_t") or ret_s.startswith("struct "))
        )
        alist = []
        if args and args != "void":
            parts = []
            depth = 0
            cur = ""
            for ch in args:
                if ch == "(":
                    depth += 1
                    cur += ch
                elif ch == ")":
                    depth -= 1
                    cur += ch
                elif ch == "," and depth == 0:
                    parts.append(cur.strip())
                    cur = ""
                else:
                    cur += ch
            if cur.strip():
                parts.append(cur.strip())
            for i, p in enumerate(parts):
                mm = re.match(r"^(.*?)([A-Za-z_][A-Za-z0-9_]*)$", p.strip())
                if not mm:
                    alist.append((f"a{i}", p, "ptr" if "*" in p else "int"))
                else:
                    ctype, an = mm.group(1).strip(), mm.group(2)
                    kind = "ptr" if "*" in ctype or "*" in p else "int"
                    if ("char" in ctype or "uint8_t" in ctype) and kind == "ptr":
                        kind = "buf"
                    alist.append((an, ctype, kind))
        if ret_is_struct or any(
            "(*" in a[1] or "_fn" in a[1] or a[0] == "walker" for a in alist
        ):
            lines.append(
                f"static mp_obj_t {short}_{pyname}(void) {{ return MP_OBJ_NEW_SMALL_INT(-1); }}"
            )
            lines.append(
                f"static MP_DEFINE_CONST_FUN_OBJ_0({short}_{pyname}_obj, {short}_{pyname});"
            )
            lines.append("")
            objs.append(pyname)
            continue
        ac = len(alist)
        if ac == 0:
            lines.append(f"static mp_obj_t {short}_{pyname}(void) {{")
            if ret_s == "void":
                lines.append(f"    {name}(); return mp_const_none;")
            elif "*" in ret_s:
                lines.append(
                    f"    return mp_obj_new_int((mp_int_t)(uintptr_t){name}());"
                )
            else:
                lines.append(f"    return mp_obj_new_int((mp_int_t){name}());")
            lines.append("}")
            lines.append(
                f"static MP_DEFINE_CONST_FUN_OBJ_0({short}_{pyname}_obj, {short}_{pyname});"
            )
        else:
            lines.append(
                f"static mp_obj_t {short}_{pyname}(size_t n_args, const mp_obj_t *args) {{"
            )
            lines.append("    (void)n_args;")
            call = []
            for i, (an, ctype, kind) in enumerate(alist):
                if kind == "int":
                    if "uint64" in ctype:
                        lines.append(
                            f"    uint64_t {an} = (uint64_t)mp_obj_get_int(args[{i}]);"
                        )
                    elif "uint16" in ctype:
                        lines.append(
                            f"    uint16_t {an} = (uint16_t)mp_obj_get_int(args[{i}]);"
                        )
                    elif "uint8" in ctype:
                        lines.append(
                            f"    uint8_t {an} = (uint8_t)mp_obj_get_int(args[{i}]);"
                        )
                    elif "size_t" in ctype:
                        lines.append(
                            f"    size_t {an} = (size_t)mp_obj_get_int(args[{i}]);"
                        )
                    elif "uint32" in ctype:
                        lines.append(
                            f"    uint32_t {an} = (uint32_t)mp_obj_get_int(args[{i}]);"
                        )
                    else:
                        lines.append(
                            f"    int32_t {an} = (int32_t)mp_obj_get_int(args[{i}]);"
                        )
                    call.append(an)
                elif kind == "buf":
                    lines.append(f"    mp_buffer_info_t {an}_bi;")
                    lines.append(f"    const uint8_t *{an};")
                    lines.append(
                        f"    if (args[{i}] == mp_const_none) {{ {an} = NULL; }}"
                    )
                    lines.append(
                        f"    else if (mp_obj_is_str_or_bytes(args[{i}])) {{"
                    )
                    lines.append(
                        f"        size_t _{an}_n; const char *_{an}_s = mp_obj_str_get_data(args[{i}], &_{an}_n); {an}=(const uint8_t*)_{an}_s;"
                    )
                    lines.append("    } else {")
                    lines.append(
                        f"        mp_get_buffer_raise(args[{i}], &{an}_bi, MP_BUFFER_READ); {an}=(const uint8_t*){an}_bi.buf;"
                    )
                    lines.append("    }")
                    if "const" not in ctype:
                        call.append("(uint8_t *)(uintptr_t)" + an)
                    else:
                        call.append(an)
                else:
                    # Opaque / typed pointer: int handle → void*, then cast to ctype.
                    ctype_clean = ctype.replace("const ", "").strip()
                    lines.append(
                        f"    void *_{an}_v = (args[{i}] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[{i}]);"
                    )
                    lines.append(f"    {ctype} {an} = ({ctype_clean})_{an}_v;")
                    call.append(an)
            cargs = ", ".join(call)
            if ret_s == "void":
                lines.append(f"    {name}({cargs}); return mp_const_none;")
            elif "*" in ret_s:
                lines.append(
                    f"    return mp_obj_new_int((mp_int_t)(uintptr_t){name}({cargs}));"
                )
            else:
                lines.append(f"    return mp_obj_new_int((mp_int_t){name}({cargs}));")
            lines.append("}")
            lines.append(
                f"static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN({short}_{pyname}_obj, {ac}, {ac}, {short}_{pyname});"
            )
        lines.append("")
        objs.append(pyname)
    lines.append(f"static const mp_rom_map_elem_t {short}_globals_table[] = {{")
    lines.append(
        f"    {{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_{qstr}) }},"
    )
    for n in objs:
        lines.append(
            f"    {{ MP_ROM_QSTR(MP_QSTR_{n}), MP_ROM_PTR(&{short}_{n}_obj) }},"
        )
    lines += [
        "};",
        f"static MP_DEFINE_CONST_DICT({short}_globals, {short}_globals_table);",
        f"const mp_obj_module_t {mod} = {{",
        "    .base = { &mp_type_module },",
        f"    .globals = (mp_obj_dict_t *)&{short}_globals,",
        "};",
        "",
    ]
    return "\n".join(lines), len(objs)


def main():
    jobs = [
        (
            "mem.tlsf",
            "pymergetic/metal/mem/tlsf/__init__.h",
            "include/pymergetic/metal/mem/tlsf/__init__.h",
            "glue/pymergetic/metal/mem/tlsf.c",
            ["pm_metal_mem_tlsf_"],
        ),
        (
            "mem.arena",
            "pymergetic/metal/mem/arena/__init__.h",
            "include/pymergetic/metal/mem/arena/__init__.h",
            "glue/pymergetic/metal/mem/arena.c",
            ["pm_metal_mem_arena_"],
        ),
        (
            "mem.lock",
            "pymergetic/metal/mem/lock/__init__.h",
            "include/pymergetic/metal/mem/lock/__init__.h",
            "glue/pymergetic/metal/mem/lock.c",
            ["pm_metal_mem_lock_"],
        ),
        (
            "wamr_host",
            "pymergetic/metal/wamr_host/__init__.h",
            "include/pymergetic/metal/wamr_host/__init__.h",
            "glue/pymergetic/metal/wamr_host.c",
            ["pm_metal_wasm_", "pm_metal_"],
        ),
        (
            "rt",
            "pymergetic/metal/rt/__init__.h",
            "include/pymergetic/metal/rt/__init__.h",
            "glue/pymergetic/metal/rt.c",
            ["pm_metal_rt_"],
        ),
        (
            "hwtree",
            "pymergetic/metal/hwtree/__init__.h",
            "include/pymergetic/metal/hwtree/__init__.h",
            "glue/pymergetic/metal/hwtree.c",
            ["pm_metal_hwtree_"],
        ),
    ]
    base = Path(__file__).resolve().parents[1]
    for seat, inc, hdr, out, prefs in jobs:
        decls = parse_header(base / hdr)
        body, n = gen(seat, inc, decls, prefs)
        (base / out).write_text(body)
        print(seat, "decls", len(decls), "Py", n)


if __name__ == "__main__":
    main()
