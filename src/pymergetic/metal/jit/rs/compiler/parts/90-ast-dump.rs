/* ================= AST dump (inspect face) ================= */

/* Renders the tree through `Out` — the same arena-owned growable sink the
 * emitted C rides (10-lexer.rs): a dump can never outgrow a fixed slab,
 * the arena is the only ceiling. Depth by two spaces per level. */
unsafe fn dump_node(
    out: *mut Out,
    n: *const pm_jit_rsx_ast_t,
    depth: usize,
) {
    if n.is_null() {
        return;
    }
    let mut i = 0usize;
    while i < depth {
        unsafe { (*out).puts(b"  \0".as_ptr()) };
        i += 1;
    }
    unsafe { (*out).puts(unsafe { ast_kind_name(unsafe { (*n).kind }) }) };
    if unsafe { (*n).text_len } > 0 {
        unsafe { (*out).puts(b" \0".as_ptr()) };
        unsafe { (*out).put(unsafe { (*n).text }, unsafe { (*n).text_len }) };
    }
    /* line tag on every node: refusal-to-AST correlation */
    unsafe { (*out).puts(b" @\0".as_ptr()) };
    unsafe { (*out).put_u32(unsafe { (*n).line }) };
    unsafe { (*out).putc(b'\n') };
    let kids = unsafe { (*n).kids };
    let nk = unsafe { (*n).n_kids } as usize;
    let mut j = 0usize;
    while j < nk {
        unsafe { dump_node(out, *kids.add(j), depth + 1) };
        j += 1;
    }
}

/* Fixed-buffer twin — the legacy C ABI's renderer (kept: the compiled
 * tests call it; the Out sink above is the real one). Same shape, no
 * growth: short buffers are the caller's to size. */
unsafe fn dump_node_fixed(
    out: *mut u8,
    cap: usize,
    at_in: usize,
    n: *const pm_jit_rsx_ast_t,
    depth: usize,
) -> usize {
    let mut at = at_in;
    if n.is_null() {
        return at;
    }
    let mut i = 0usize;
    while i < depth {
        at = unsafe { bput(out, cap, at, b"  \0".as_ptr(), 2) };
        i += 1;
    }
    at = unsafe { zput(out, cap, at, unsafe { ast_kind_name(unsafe { (*n).kind }) }) };
    if unsafe { (*n).text_len } > 0 {
        at = unsafe { bput(out, cap, at, b" \0".as_ptr(), 1) };
        at = unsafe { bput(out, cap, at, unsafe { (*n).text }, unsafe { (*n).text_len }) };
    }
    /* line tag on every node: refusal-to-AST correlation */
    at = unsafe { bput(out, cap, at, b" @\0".as_ptr(), 2) };
    at += unsafe { zput_num(out.add(at), cap - at, unsafe { (*n).line }) };
    at = unsafe { bput(out, cap, at, b"\n\0".as_ptr(), 1) };
    let kids = unsafe { (*n).kids };
    let nk = unsafe { (*n).n_kids } as usize;
    let mut j = 0usize;
    while j < nk {
        at = unsafe { dump_node_fixed(out, cap, at, *kids.add(j), depth + 1) };
        j += 1;
    }
    at
}

unsafe fn ast_kind_name(k: pm_jit_rsx_ast_kind) -> *const u8 {
    let z = match k {
        pm_jit_rsx_ast_kind::FILE => b"FILE\0".as_ptr(),
        pm_jit_rsx_ast_kind::USE => b"USE\0".as_ptr(),
        pm_jit_rsx_ast_kind::FN => b"FN\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT => b"STRUCT\0".as_ptr(),
        pm_jit_rsx_ast_kind::ENUM => b"ENUM\0".as_ptr(),
        pm_jit_rsx_ast_kind::IMPL => b"IMPL\0".as_ptr(),
        pm_jit_rsx_ast_kind::EXTERN_BLOCK => b"EXTERN_BLOCK\0".as_ptr(),
        pm_jit_rsx_ast_kind::STATIC => b"STATIC\0".as_ptr(),
        pm_jit_rsx_ast_kind::CONST => b"CONST\0".as_ptr(),
        pm_jit_rsx_ast_kind::TYPE_ALIAS => b"TYPE_ALIAS\0".as_ptr(),
        pm_jit_rsx_ast_kind::TRAIT => b"TRAIT\0".as_ptr(),
        pm_jit_rsx_ast_kind::MODULE => b"MODULE\0".as_ptr(),
        pm_jit_rsx_ast_kind::ATTR => b"ATTR\0".as_ptr(),
        pm_jit_rsx_ast_kind::BLOCK => b"BLOCK\0".as_ptr(),
        pm_jit_rsx_ast_kind::STMT => b"STMT\0".as_ptr(),
        pm_jit_rsx_ast_kind::LET => b"LET\0".as_ptr(),
        pm_jit_rsx_ast_kind::IF => b"IF\0".as_ptr(),
        pm_jit_rsx_ast_kind::MATCH => b"MATCH\0".as_ptr(),
        pm_jit_rsx_ast_kind::MATCH_ARM => b"MATCH_ARM\0".as_ptr(),
        pm_jit_rsx_ast_kind::LOOP => b"LOOP\0".as_ptr(),
        pm_jit_rsx_ast_kind::WHILE => b"WHILE\0".as_ptr(),
        pm_jit_rsx_ast_kind::FOR => b"FOR\0".as_ptr(),
        pm_jit_rsx_ast_kind::RETURN => b"RETURN\0".as_ptr(),
        pm_jit_rsx_ast_kind::BREAK => b"BREAK\0".as_ptr(),
        pm_jit_rsx_ast_kind::CONTINUE => b"CONTINUE\0".as_ptr(),
        pm_jit_rsx_ast_kind::EXPR_STMT => b"EXPR_STMT\0".as_ptr(),
        pm_jit_rsx_ast_kind::ASSIGN => b"ASSIGN\0".as_ptr(),
        pm_jit_rsx_ast_kind::BINARY => b"BINARY\0".as_ptr(),
        pm_jit_rsx_ast_kind::UNARY => b"UNARY\0".as_ptr(),
        pm_jit_rsx_ast_kind::CALL => b"CALL\0".as_ptr(),
        pm_jit_rsx_ast_kind::METHOD_CALL => b"METHOD_CALL\0".as_ptr(),
        pm_jit_rsx_ast_kind::FIELD => b"FIELD\0".as_ptr(),
        pm_jit_rsx_ast_kind::PATH => b"PATH\0".as_ptr(),
        pm_jit_rsx_ast_kind::LITERAL => b"LITERAL\0".as_ptr(),
        pm_jit_rsx_ast_kind::TUPLE => b"TUPLE\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT_LIT => b"STRUCT_LIT\0".as_ptr(),
        pm_jit_rsx_ast_kind::CLOSURE => b"CLOSURE\0".as_ptr(),
        pm_jit_rsx_ast_kind::INDEX => b"INDEX\0".as_ptr(),
        pm_jit_rsx_ast_kind::ARRAY => b"ARRAY\0".as_ptr(),
        pm_jit_rsx_ast_kind::CAST => b"CAST\0".as_ptr(),
        pm_jit_rsx_ast_kind::MACRO => b"MACRO\0".as_ptr(),
        pm_jit_rsx_ast_kind::PAREN => b"PAREN\0".as_ptr(),
        pm_jit_rsx_ast_kind::TYPE => b"TYPE\0".as_ptr(),
        pm_jit_rsx_ast_kind::PARAM => b"PARAM\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT_FIELD => b"STRUCT_FIELD\0".as_ptr(),
        pm_jit_rsx_ast_kind::ENUM_VARIANT => b"ENUM_VARIANT\0".as_ptr(),
        pm_jit_rsx_ast_kind::GENERIC => b"GENERIC\0".as_ptr(),
        pm_jit_rsx_ast_kind::WHERE => b"WHERE\0".as_ptr(),
    };
    z
}

