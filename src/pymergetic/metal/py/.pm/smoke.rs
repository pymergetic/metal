//! Host smoke — py edge + B0/B1 upy faces (needs mem init).
use std::alloc::{alloc, Layout};

/// Firmware resolves these against `boot/externals`; host smoke has no boot.
#[no_mangle]
pub extern "C" fn pm_metal_external_count() -> u32 {
    0
}
#[no_mangle]
pub unsafe extern "C" fn pm_metal_external_get(
    _idx: u32,
    _out: *mut core::ffi::c_void,
) -> i32 {
    -1
}

/// Host smoke has no net/http/microdot crate; provide C edges for bindcatalog.
#[no_mangle]
pub extern "C" fn pm_metal_net_http_microdot_register() -> u32 {
    7
}
#[no_mangle]
pub extern "C" fn pm_metal_net_http_microdot_handle(_conn_id: u32) -> i32 {
    0
}
/// Sentinel pointers so import attaches typed refuse wrappers for get/route.
#[no_mangle]
pub extern "C" fn pm_metal_net_http_microdot_get_stub() -> i32 {
    -1
}
#[no_mangle]
pub extern "C" fn pm_metal_net_http_microdot_route_stub() -> i32 {
    -1
}
#[no_mangle]
pub extern "C" fn pm_metal_net_http_microdot_bind_reg() -> i32 {
    use core::ffi::c_void;
    use pymergetic_metal_reg::pm_metal_reg_register;
    let mod_name = b"pymergetic.metal.net.http.microdot\0";
    let rows: [(&[u8], *const c_void); 4] = [
        (
            b"register\0",
            pm_metal_net_http_microdot_register as *const c_void,
        ),
        (
            b"handle\0",
            pm_metal_net_http_microdot_handle as *const c_void,
        ),
        (
            b"get\0",
            pm_metal_net_http_microdot_get_stub as *const c_void,
        ),
        (
            b"route\0",
            pm_metal_net_http_microdot_route_stub as *const c_void,
        ),
    ];
    for (name, ptr) in rows {
        if unsafe { pm_metal_reg_register(mod_name.as_ptr(), name.as_ptr(), ptr) } != 0 {
            return -1;
        }
    }
    0
}

use pymergetic_metal_mem::api as mem;
use pymergetic_metal_py::upy::py::{
    bc, bc0, builtin, compile, emitcommon, frozenmod, gc, lexer, malloc, mpconfig, mpstate,
    nativeglue, obj, objects, parse, qstr, qstrdefs, reader, repl, runtime, vm,
};
use pymergetic_metal_py::{
    pm_metal_py_alloc, pm_metal_py_bind_reg, pm_metal_py_free, pm_metal_py_gc_enabled,
    pm_metal_py_libc_policy, pm_metal_py_loop_feed, pm_metal_py_loop_last_result_i32,
    pm_metal_py_loop_last_result_valid, pm_metal_py_loop_reset, pm_metal_py_loop_step,
    pm_metal_py_ready,
};
use pymergetic_metal_reg::{pm_metal_reg_bind, pm_metal_reg_call0};

fn floor_log_ready() {
    /* Always-proxy log needs console's RegMod + init (boot reg_bootstrap). */
    use pymergetic_metal_console as _;
    extern "C" {
        fn pm_metal_console_mod_load() -> i32;
        fn pm_metal_log_mod_load() -> i32;
        fn pm_metal_console_init0(ring_bytes: usize) -> i32;
    }
    unsafe {
        assert_eq!(pm_metal_console_mod_load(), 0);
        assert_eq!(pm_metal_log_mod_load(), 0);
        assert_eq!(pm_metal_console_init0(0), 0);
    }
}

fn main() {
    const N: usize = 256 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());
    assert_eq!(mem::init(base, N), 0);

    floor_log_ready();

    assert_eq!(pm_metal_py_ready(), 0);
    runtime::init();
    assert!(mpstate::ready());
    assert!(!mpconfig::ENABLE_GC);
    assert_eq!(pm_metal_py_gc_enabled(), 0);
    assert_eq!(pm_metal_py_libc_policy(), 1);

    // --- B0: qstr / obj ---
    assert_eq!(qstr::from_str("__name__"), qstrdefs::QSTR_NAME);
    assert_eq!(qstr::str(qstrdefs::QSTR_NONE), b"None");
    let dyn_a = qstr::from_str("hello_metal");
    let dyn_b = qstr::from_str("hello_metal");
    assert_eq!(dyn_a, dyn_b);
    assert!(dyn_a >= qstrdefs::static_count());
    assert_eq!(qstr::str(dyn_a), b"hello_metal");

    let si = obj::new_small_int(21);
    assert!(obj::is_small_int(si));
    assert_eq!(obj::small_int_value(si), 21);
    let qo = obj::new_qstr(qstrdefs::QSTR_TRUE);
    assert!(obj::is_qstr(qo));
    assert_eq!(obj::qstr_value(qo), qstrdefs::QSTR_TRUE);

    // --- B1: malloc / gc DEAD / bc decode / mini-vm ---
    unsafe {
        let p = malloc::m_malloc0(32);
        assert!(!p.is_null());
        assert_eq!(*p, 0);
        let p2 = malloc::m_realloc(p, 64);
        assert!(!p2.is_null());
        malloc::m_free(p2);

        let g = gc::alloc(16, 0);
        assert!(!g.is_null());
        gc::free(g);
        gc::collect(); // no-op
        assert!(!gc::is_locked());
    }

    assert_eq!(bc0::format(bc0::LOAD_CONST_FALSE), bc0::FORMAT_BYTE);
    assert_eq!(bc0::RETURN_VALUE, 0x63);

    let mut ip = 0usize;
    let enc = [0x81u8, 0x02u8]; // varint 0x101
    assert_eq!(bc::decode_uint(&enc, &mut ip), Some(0x101));
    assert_eq!(ip, 2);

    // LOAD_CONST_TRUE, RETURN_VALUE
    let code = [bc0::LOAD_CONST_TRUE, bc0::RETURN_VALUE];
    let mut st = vm::CodeState::new();
    assert_eq!(vm::execute(&code, &[], &mut st), runtime::VmReturnKind::Normal);
    assert!(obj::is_immediate(st.result));

    // small-int multi: opcode 0x70 + 16 + 5 => value 5
    let op = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 5;
    let code2 = [op, bc0::RETURN_VALUE];
    let mut st2 = vm::CodeState::new();
    assert_eq!(vm::execute(&code2, &[], &mut st2), runtime::VmReturnKind::Normal);
    assert!(obj::is_small_int(st2.result));
    assert_eq!(obj::small_int_value(st2.result), 5);

    // unknown opcode -> exception
    let mut st3 = vm::CodeState::new();
    assert_eq!(
        vm::execute(&[0x00], &[], &mut st3),
        runtime::VmReturnKind::Exception
    );

    // JUMP / POP_JUMP_IF_FALSE: push 5, false -> jump over TRUE, return 5
    {
        let op5 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 5;
        // sint offset +1 encoded as 0x41 (see bc::decode_sint_offset)
        let code = [
            op5,
            bc0::LOAD_CONST_FALSE,
            bc0::POP_JUMP_IF_FALSE,
            0x41,
            bc0::LOAD_CONST_TRUE,
            bc0::RETURN_VALUE,
        ];
        let mut st = vm::CodeState::new();
        assert_eq!(vm::execute(&code, &[], &mut st), runtime::VmReturnKind::Normal);
        assert_eq!(obj::small_int_value_checked(st.result), Some(5));
    }

    // BUILD_LIST + LOAD_SUBSCR
    {
        let op7 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 7;
        let op0 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 0;
        let code = [
            op7,
            bc0::BUILD_LIST,
            0x01, // n=1
            op0,
            bc0::LOAD_SUBSCR,
            bc0::RETURN_VALUE,
        ];
        let mut st = vm::CodeState::new();
        assert_eq!(vm::execute(&code, &[], &mut st), runtime::VmReturnKind::Normal);
        assert_eq!(obj::small_int_value_checked(st.result), Some(7));
    }

    // LOAD_CONST_OBJ + CALL_FUNCTION into FunBc (args in locals)
    unsafe {
        // body: LOAD_FAST 0, LOAD_FAST 1, BINARY_ADD, RETURN
        let body = [
            bc0::LOAD_FAST_MULTI,
            bc0::LOAD_FAST_MULTI + 1,
            bc0::BINARY_OP_MULTI + emitcommon::BINARY_OP_ADD,
            bc0::RETURN_VALUE,
        ];
        let fun = objects::objfun::new(&body, 2, &[]);
        let consts = [fun];
        let op2 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 2;
        let op3 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 3;
        let code = [
            bc0::LOAD_CONST_OBJ,
            0x00, // consts[0]
            op2,
            op3,
            bc0::CALL_FUNCTION,
            0x02, // n_pos=2
            bc0::RETURN_VALUE,
        ];
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(&code, &consts, &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(obj::small_int_value_checked(st.result), Some(5));
        objects::objfun::free(fun);
    }

    // typed native KIND_I32_2 via nativeglue marshalling
    unsafe {
        extern "C" fn add2(a: i32, b: i32) -> i32 {
            a + b
        }
        let fun = objects::objfun_native::new(
            add2 as *const _,
            2,
            objects::objfun_native::KIND_I32_2,
        );
        let op4 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 4;
        let op6 = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 6;
        let code = [
            bc0::LOAD_CONST_OBJ,
            0x00,
            op4,
            op6,
            bc0::CALL_FUNCTION,
            0x02,
            bc0::RETURN_VALUE,
        ];
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(&code, &[fun], &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(obj::small_int_value_checked(st.result), Some(10));
        assert_eq!(nativeglue::as_i32(st.result), Some(10));
        objects::objfun_native::free(fun);
    }

    // --- B2: essential objects ---
    assert!(objects::objnone::is_none(objects::objnone::get()));
    assert_eq!(objects::objbool::value(objects::objbool::get(true)), Some(true));
    assert_eq!(objects::objint::as_isize(objects::objint::from_isize(-3)), Some(-3));
    assert_eq!(
        objects::kind_of(objects::objbool::get(false)),
        Some(objects::TypeKind::Bool)
    );

    unsafe {
        let s = objects::objstr::new(b"hi");
        assert_eq!(objects::objstr::as_bytes(s), Some(&b"hi"[..]));
        objects::objstr::free(s);

        let lst = objects::objlist::new(0);
        assert!(objects::objlist::append(lst, objects::objint::from_isize(7)));
        assert_eq!(objects::objlist::len(lst), Some(1));
        assert_eq!(
            objects::objlist::get(lst, 0),
            Some(objects::objint::from_isize(7))
        );
        objects::objlist::free(lst);

        let t = objects::objtuple::new(&[
            objects::objbool::get(true),
            objects::objnone::get(),
        ]);
        assert_eq!(objects::objtuple::len(t), Some(2));
        objects::objtuple::free(t);

        let d = objects::objdict::new(4);
        let k = obj::new_qstr(qstrdefs::QSTR_NAME);
        assert!(objects::objdict::store(d, k, objects::objint::from_isize(1)));
        assert_eq!(
            objects::objdict::load(d, k),
            Some(objects::objint::from_isize(1))
        );
        objects::objdict::free(d);

        let msg = objects::objstr::new(b"boom");
        let ex = objects::objexcept::new(qstrdefs::QSTR_EXCEPTION, msg);
        assert_eq!(objects::objexcept::type_name(ex), Some(qstrdefs::QSTR_EXCEPTION));
        objects::objexcept::free(ex);
        objects::objstr::free(msg);

        let fun = objects::objfun::new(&[bc0::LOAD_CONST_TRUE, bc0::RETURN_VALUE], 2, &[]);
        let code = objects::objfun::code(fun).unwrap();
        let mut stf = vm::CodeState::new();
        assert_eq!(vm::execute(code, &[], &mut stf), runtime::VmReturnKind::Normal);
        objects::objfun::free(fun);

        let m = objects::objmodule::new(qstrdefs::QSTR_MAIN);
        assert!(objects::objmodule::store_attr(
            m,
            obj::new_qstr(qstrdefs::QSTR_NAME),
            objects::objint::from_isize(99)
        ));
        assert_eq!(
            objects::objmodule::load_attr(m, obj::new_qstr(qstrdefs::QSTR_NAME)),
            Some(objects::objint::from_isize(99))
        );
        objects::objmodule::free(m);

        let ty = objects::objtype::as_obj(&objects::TYPE_LIST);
        assert_eq!(objects::objtype::kind(ty), Some(objects::TypeKind::List));
    }

    // --- B3: builtins + import -> reg ---
    unsafe {
        assert!(builtin::modbuiltins::len(objects::objlist::new(0)).is_some());
        let lst = objects::objlist::new(0);
        assert!(objects::objlist::append(lst, objects::objint::from_isize(1)));
        assert!(objects::objlist::append(lst, objects::objint::from_isize(2)));
        assert_eq!(builtin::modbuiltins::len(lst), Some(2));
        objects::objlist::free(lst);
        assert_eq!(
            builtin::modbuiltins::abs_int(objects::objint::from_isize(-9)),
            Some(9)
        );
        assert!(builtin::modbuiltins::isinstance(
            objects::objbool::get(true),
            objects::TypeKind::Bool
        ));

        let errno_m = builtin::builtinimport::import_module("errno").unwrap();
        assert_eq!(builtin::moderrno::get_attr(errno_m, "ENOENT"), Some(2));
        assert_eq!(
            builtin::builtinimport::import_module("errno"),
            Some(errno_m)
        ); // cached

        let sys_m = builtin::builtinimport::import_module("sys").unwrap();
        assert!(builtin::modsys::ready());
        assert_eq!(sys_m, builtin::modsys::module());

        let _bi = builtin::builtinimport::import_module("builtins").unwrap();

        assert_eq!(pm_metal_py_bind_reg(), 0);
        // Dotted import returns the top package (CPython); callables live on the leaf.
        let top = builtin::builtinimport::import_module("pymergetic.metal.py").unwrap();
        let leaf = builtin::modsys::modules_get_str("pymergetic.metal.py").expect("leaf cached");
        assert!(builtin::builtinimport::has_reg_marker(leaf, "ready"));
        // Parent chain: top.metal.py is the leaf.
        let metal = objects::objmodule::load_attr(
            top,
            obj::new_qstr(qstr::from_str("metal")),
        )
        .expect("pymergetic.metal");
        let py_mod = objects::objmodule::load_attr(
            metal,
            obj::new_qstr(qstr::from_str("py")),
        )
        .expect("pymergetic.metal.py attr");
        assert_eq!(py_mod, leaf);
        assert!(builtin::builtinimport::import_module("no.such.module").is_none());
    }

    // --- B4: more objects + math/array/collections/help ---
    unsafe {
        let f = objects::objfloat::new(-2.5);
        assert_eq!(objects::objfloat::value(f), Some(-2.5));
        assert_eq!(builtin::modmath::fabs(f), Some(2.5));
        let f2 = builtin::modmath::add(f, objects::objfloat::new(1.5)).unwrap();
        assert_eq!(objects::objfloat::value(f2), Some(-1.0));
        objects::objfloat::free(f);
        objects::objfloat::free(f2);
        assert_eq!(builtin::modmath::isqrt(16), Some(4));

        let set = objects::objset::new(8);
        assert!(objects::objset::add(set, objects::objint::from_isize(3)));
        assert!(objects::objset::contains(set, objects::objint::from_isize(3)));
        assert_eq!(objects::objset::len(set), Some(1));
        objects::objset::free(set);

        let r = objects::objrange::new(0, 5, 1);
        assert_eq!(objects::objrange::len(r), Some(5));
        assert_eq!(objects::objrange::get(r, 2), Some(2));
        objects::objrange::free(r);

        let sl = objects::objslice::new(
            objects::objint::from_isize(1),
            objects::objnone::get(),
            objects::objint::from_isize(2),
        );
        let (a, b, c) = objects::objslice::parts(sl).unwrap();
        assert_eq!(objects::objint::as_isize(a), Some(1));
        assert!(objects::objnone::is_none(b));
        assert_eq!(objects::objint::as_isize(c), Some(2));
        objects::objslice::free(sl);

        let cell = objects::objcell::new(objects::objint::from_isize(7));
        assert_eq!(
            objects::objint::as_isize(objects::objcell::get(cell).unwrap()),
            Some(7)
        );
        assert!(objects::objcell::set(cell, objects::objint::from_isize(8)));
        objects::objcell::free(cell);

        let ba = builtin::modarray::bytearray(3);
        assert!(objects::objarray::set(ba, 1, 0xAB));
        assert_eq!(objects::objarray::get(ba, 1), Some(0xAB));
        objects::objarray::free(ba);

        assert!(objects::objsingleton::is_ellipsis(
            objects::objsingleton::ELLIPSIS
        ));
        assert_eq!(
            objects::objtype::kind(objects::objobject::type_obj()),
            Some(objects::TypeKind::Object)
        );

        let dq = builtin::modcollections::deque_new(4);
        assert!(builtin::modcollections::append(
            dq,
            objects::objint::from_isize(1)
        ));
        assert!(builtin::modcollections::append(
            dq,
            objects::objint::from_isize(2)
        ));
        assert_eq!(
            objects::objint::as_isize(builtin::modcollections::popleft(dq).unwrap()),
            Some(1)
        );
        builtin::modcollections::free(dq);

        assert!(builtin::builtinhelp::help_text(None).contains("Metal upy"));
        assert!(builtin::builtinhelp::help_text(Some("import")).contains("pymergetic.metal"));
        assert!(builtin::builtinhelp::help_text(Some("sys")).contains("modules"));
    }

    // --- B5: extmod keep-list ---
    unsafe {
        use pymergetic_metal_py::upy::extmod::{
            modbinascii, modheapq, modjson, modos, modplatform, modrandom, modre, modtime, vfs,
        };

        let d = objects::objdict::new(4);
        let k = objects::objstr::new(b"a");
        assert!(objects::objdict::store(d, k, objects::objint::from_isize(1)));
        let dumped = modjson::dumps(d).unwrap();
        assert_eq!(objects::objstr::as_bytes(dumped), Some(&b"{\"a\":1}"[..]));
        let loaded = modjson::loads(b"{\"a\":1}").unwrap();
        assert_eq!(
            objects::objint::as_isize(
                objects::objdict::load(loaded, objects::objstr::new(b"a")).unwrap()
            ),
            Some(1)
        );
        let round = modjson::loads(objects::objstr::as_bytes(dumped).unwrap()).unwrap();
        assert_eq!(objects::objdict::len(round), Some(1));

        assert_eq!(
            objects::objstr::as_bytes(modbinascii::hexlify(b"AB").unwrap()),
            Some(&b"4142"[..])
        );
        assert_eq!(
            objects::objstr::as_bytes(modbinascii::unhexlify(b"4142").unwrap()),
            Some(&b"AB"[..])
        );
        let b64 = modbinascii::b2a_base64(b"hi").unwrap();
        assert_eq!(
            objects::objstr::as_bytes(modbinascii::a2b_base64(
                objects::objstr::as_bytes(b64).unwrap()
            )
            .unwrap()),
            Some(&b"hi"[..])
        );

        let heap = objects::objlist::new(0);
        assert!(modheapq::heappush(heap, objects::objint::from_isize(3)));
        assert!(modheapq::heappush(heap, objects::objint::from_isize(1)));
        assert!(modheapq::heappush(heap, objects::objint::from_isize(2)));
        assert_eq!(
            objects::objint::as_isize(modheapq::heappop(heap).unwrap()),
            Some(1)
        );
        assert_eq!(
            objects::objint::as_isize(modheapq::heappop(heap).unwrap()),
            Some(2)
        );
        objects::objlist::free(heap);

        modrandom::seed(42);
        let r = modrandom::getrandbits(8).unwrap();
        assert!(objects::objint::as_isize(r).unwrap() < 256);
        let ri = modrandom::randint(1, 3).unwrap();
        let riv = objects::objint::as_isize(ri).unwrap();
        assert!(riv >= 1 && riv <= 3);

        let t0 = modtime::ticks_us();
        let t1 = modtime::ticks_us();
        assert!(modtime::ticks_diff(t1, t0) >= 0);
        assert!(modtime::ticks_ms() <= t1 / 1000 + 1);

        assert_eq!(
            objects::objstr::as_bytes(modplatform::platform()),
            Some(&b"Metal"[..])
        );

        let u = modos::uname();
        assert_eq!(objects::objtuple::len(u), Some(5));
        assert_eq!(
            objects::objstr::as_bytes(objects::objtuple::get(u, 0).unwrap()),
            Some(&b"Metal"[..])
        );

        assert!(modre::match_str(b"ab.*z$", b"abcz"));
        assert!(!modre::match_str(b"^xyz$", b"xy"));
        let pat = modre::compile(b"a.c").unwrap();
        assert_eq!(
            objects::objbool::value(modre::match_obj(pat, objects::objstr::new(b"abc")).unwrap()),
            Some(true)
        );
        modre::free(pat);

        // thin vfs: no mount -> INVALID / None (honest)
        assert_eq!(vfs::open(b"/nope", vfs::O_RDONLY), vfs::INVALID);
        assert!(vfs::listdir(b"/").is_none());
        assert!(modos::listdir(b"/").is_none());

        let jm = builtin::builtinimport::import_module("json").unwrap();
        let dumps = objects::objmodule::load_attr(jm, obj::new_qstr(qstr::from_str("dumps")))
            .expect("json.dumps");
        assert!(objects::objfun_native::is_fun_native(dumps));
        let s = objects::objfun_native::call(dumps, &[objects::objint::from_isize(3)])
            .expect("json.dumps(3)");
        assert_eq!(objects::objstr::as_bytes(s), Some(&b"3"[..]));

        assert!(builtin::builtinimport::import_module("os").is_some());
        assert!(builtin::builtinimport::import_module("time").is_some());
        assert!(builtin::builtinimport::import_module("re").is_some());
        assert!(builtin::builtinimport::import_module("vfs").is_some());

        let bi = builtin::builtinimport::import_module("builtins").unwrap();
        assert!(objects::objfun_native::is_fun_native(
            objects::objmodule::load_attr(bi, obj::new_qstr(qstr::from_str("len"))).unwrap()
        ));
        assert!(objects::objfun_native::is_fun_native(
            objects::objmodule::load_attr(bi, obj::new_qstr(qstr::from_str("range"))).unwrap()
        ));

        assert_eq!(pm_metal_net_http_microdot_bind_reg(), 0);
        let md_top =
            builtin::builtinimport::import_module("pymergetic.metal.net.http.microdot").unwrap();
        let md = builtin::modsys::modules_get_str("pymergetic.metal.net.http.microdot")
            .expect("microdot leaf");
        let _ = md_top;
        let reg =
            objects::objmodule::load_attr(md, obj::new_qstr(qstr::from_str("register"))).unwrap();
        assert!(objects::objfun_native::is_fun_native(reg));
        let h = objects::objfun_native::call(reg, &[]).expect("microdot.register()");
        assert_eq!(obj::small_int_value_checked(h), Some(7));
    }

    // --- B6: asyncio REWRITE -> Metal async ---
    // Start with 4 runners before ensure_started so W11.5 B14 can prove
    // 1-runner-per-logical-core placement (start is once-only).
    unsafe {
        extern "C" {
            fn pm_metal_async_start(n_cpus: u32) -> i32;
            fn pm_metal_async_n_runners() -> u32;
        }
        assert_eq!(pm_metal_async_start(4), 0);
        assert_eq!(pm_metal_async_n_runners(), 4);
    }
    unsafe {
        use pymergetic_metal_py::upy::extmod::asyncio::{
            gather_tasks, run_until, sleep_ms, wait_for_ms, Event, Lock,
        };

        assert!(pymergetic_metal_py::upy::extmod::asyncio::ensure_started());
        let t = sleep_ms(0).unwrap();
        assert!(run_until(t.handle));
        assert!(t.done());
        t.cancel();

        let t2 = sleep_ms(1).unwrap();
        assert_eq!(wait_for_ms(t2.handle, 500), Ok(true));
        t2.cancel();

        let a = sleep_ms(0).unwrap();
        let b = sleep_ms(0).unwrap();
        assert!(gather_tasks(&[a, b]));
        a.cancel();
        b.cancel();

        let mut ev = Event::new();
        assert!(!ev.is_set());
        ev.set();
        assert!(ev.wait());

        let mut lk = Lock::new();
        assert!(lk.acquire());
        assert!(lk.locked());
        assert!(lk.release());

        assert!(builtin::builtinimport::import_module("asyncio").is_some());
        assert!(builtin::builtinimport::import_module("uasyncio").is_some());
    }

    // --- B7: shared SHARED_OPT as needed ---
    unsafe {
        use pymergetic_metal_py::upy::py::bc0;
        use pymergetic_metal_py::upy::shared::runtime::{pyexec, softtimer};

        let code = [bc0::LOAD_CONST_TRUE, bc0::RETURN_VALUE];
        assert!(pyexec::exec_returns_true(&code));
        match pyexec::execute_bytecode(&[0x00]) {
            pyexec::ExecResult::Exception => {}
            _ => panic!("expected exception"),
        }

        let t = softtimer::after_us(0).unwrap();
        assert!(softtimer::wait(t));
        t.cancel();
    }

    unsafe {
        let p = pm_metal_py_alloc(64);
        assert!(!p.is_null());
        pm_metal_py_free(p);

        let mod_name = b"pymergetic.metal.py\0";
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), b"ready\0".as_ptr()), 0);
        let bound = pm_metal_reg_bind(mod_name.as_ptr(), b"alloc\0".as_ptr());
        assert!(!bound.is_null());

        qstr::reset_dynamic_for_test();
    }

    unsafe {
        assert_eq!(pymergetic_metal_py::pm_metal_py_proof_print(), 0);
        assert!(builtin::modbuiltins::print_bytes(b"ok"));
        assert_eq!(pymergetic_metal_py::pm_metal_py_proof_await(), 0);
    }

    // --- B8: reader / lexer (upstream py/reader.h + py/lexer.c mem-only mirror) ---
    {
        use lexer::TokenKind as TK;

        fn tokenize(src: &'static [u8]) -> Vec<TK> {
            let name = qstr::from_str("<test>");
            let mut lex = lexer::Lexer::new(name, reader::Reader::new_mem(src));
            let mut kinds = Vec::new();
            loop {
                kinds.push(lex.tok_kind);
                if lex.tok_kind == TK::End {
                    break;
                }
                lex.to_next();
            }
            kinds
        }

        assert_eq!(
            tokenize(b"x = 1 + 2\n"),
            vec![
                TK::Name,
                TK::DelEqual,
                TK::Integer,
                TK::OpPlus,
                TK::Integer,
                TK::Newline,
                TK::End,
            ]
        );

        assert_eq!(
            tokenize(b"async def f():\n    await g()\n"),
            vec![
                TK::KwAsync,
                TK::KwDef,
                TK::Name,
                TK::DelParenOpen,
                TK::DelParenClose,
                TK::DelColon,
                TK::Newline,
                TK::Indent,
                TK::KwAwait,
                TK::Name,
                TK::DelParenOpen,
                TK::DelParenClose,
                TK::Newline,
                TK::Dedent,
                TK::End,
            ]
        );

        assert_eq!(
            tokenize(b"# comment\nx==y!=z<=w\n"),
            vec![
                TK::Newline,
                TK::Name,
                TK::OpDblEqual,
                TK::Name,
                TK::OpNotEqual,
                TK::Name,
                TK::OpLessEqual,
                TK::Name,
                TK::Newline,
                TK::End,
            ]
        );

        assert_eq!(
            tokenize(b"1_000 0x1F 3.14 2e10 5j\n"),
            vec![
                TK::Integer,
                TK::Integer,
                TK::FloatOrImag,
                TK::FloatOrImag,
                TK::FloatOrImag,
                TK::Newline,
                TK::End,
            ]
        );

        // string/bytes: quotes, raw/bytes prefixes, basic escapes, implicit concat
        let mut lex = lexer::Lexer::new(
            qstr::from_str("<str>"),
            reader::Reader::new_mem(b"'hi\\n' 'there' b'by' r'\\raw'\n"),
        );
        assert_eq!(lex.tok_kind, TK::String);
        assert_eq!(lex.tok_text(), b"hi\nthere"); // adjacent string literals concatenate
        lex.to_next();
        assert_eq!(lex.tok_kind, TK::Bytes);
        assert_eq!(lex.tok_text(), b"by");
        lex.to_next();
        assert_eq!(lex.tok_kind, TK::String);
        assert_eq!(lex.tok_text(), b"\\raw"); // raw string keeps the backslash
        lex.to_next();
        assert_eq!(lex.tok_kind, TK::Newline);
        lex.to_next();
        assert_eq!(lex.tok_kind, TK::End);

        // bracket nesting joins lines implicitly; unterminated string is honest, not silent
        assert_eq!(
            tokenize(b"(1,\n 2)\n"),
            vec![
                TK::DelParenOpen,
                TK::Integer,
                TK::DelComma,
                TK::Integer,
                TK::DelParenClose,
                TK::Newline,
                TK::End,
            ]
        );
        assert_eq!(tokenize(b"'unterminated\n")[0], TK::LonelyStringOpen);

        // owned mem reader: takes ownership of a Metal-heap buffer, frees on drop
        unsafe {
            let src = b"pass\n";
            let buf = malloc::m_malloc(src.len());
            assert!(!buf.is_null());
            std::ptr::copy_nonoverlapping(src.as_ptr(), buf, src.len());
            let rdr = reader::Reader::new_mem_owned(buf, src.len());
            let mut lex = lexer::Lexer::new(qstr::from_str("<owned>"), rdr);
            assert_eq!(lex.tok_kind, TK::KwPass);
            lex.to_next();
            assert_eq!(lex.tok_kind, TK::Newline);
            lex.to_next();
            assert_eq!(lex.tok_kind, TK::End);
        }
    }

    // --- B9: parse tree builder (upstream py/parse.c mirror, grammar.rs tables) ---
    {
        fn parse_src(src: &'static [u8]) -> Result<parse::ParseTree, parse::ParseError> {
            let name = qstr::from_str("<parse>");
            let mut lex = lexer::Lexer::new(name, reader::Reader::new_mem(src));
            parse::parse(&mut lex, parse::InputKind::File)
        }

        // `pass\n` -> a lone pass_stmt struct (0 children), unwrapped up through
        // file_input's transparent and_ident/list layers.
        let tree = parse_src(b"pass\n").expect("parse 'pass' failed");
        assert!(parse::is_struct_kind(tree.root, parse::RuleId::PassStmt));
        assert_eq!(parse::struct_num_nodes(tree.root), 0);

        // `x = 1\n` -> expr_stmt(id(x), int(1)), reachable as file_input's root.
        let tree = parse_src(b"x = 1\n").expect("parse 'x = 1' failed");
        assert!(parse::is_struct_kind(tree.root, parse::RuleId::ExprStmt));
        let lhs = unsafe { parse::struct_node(tree.root, 0) };
        assert!(parse::is_id(lhs));
        assert_eq!(parse::leaf_arg(lhs), qstr::from_str("x"));
        let rhs = unsafe { parse::struct_node(tree.root, 1) };
        assert!(parse::is_small_int(rhs));
        assert_eq!(parse::small_int_value(rhs), 1);

        // `async def f():\n    pass\n` -> async_stmt(async_funcdef... ) wrapping funcdef.
        let tree =
            parse_src(b"async def f():\n    pass\n").expect("parse 'async def' failed");
        assert!(parse::is_struct_kind(tree.root, parse::RuleId::AsyncStmt));

        // Garbage input is a real syntax error, not a silently-empty tree.
        match parse_src(b")(\n") {
            Err(parse::ParseError::Syntax { .. }) => {}
            Err(e) => panic!("expected Syntax error for garbage input, got {e:?}"),
            Ok(_) => panic!("expected a syntax error for garbage input, got Ok"),
        }

        // Numeric literal edge cases feed straight through parsenum/parsenumbase.
        let tree = parse_src(b"x = 0x1F\n").expect("parse hex literal failed");
        let rhs = unsafe { parse::struct_node(tree.root, 1) };
        assert!(parse::is_small_int(rhs));
        assert_eq!(parse::small_int_value(rhs), 0x1F);

        let tree = parse_src(b"x = 3.5\n").expect("parse float literal failed");
        let rhs = unsafe { parse::struct_node(tree.root, 1) };
        assert_eq!(parse::extract_float(rhs), Some(3.5));

        unsafe { qstr::reset_dynamic_for_test() };
    }

    // --- B10: compile (upstream py/compile.c mirror slice) -> vm::execute ---
    {
        fn parse_src(
            src: &'static [u8],
            kind: parse::InputKind,
        ) -> parse::ParseTree {
            let name = qstr::from_str("<compile>");
            let mut lex = lexer::Lexer::new(name, reader::Reader::new_mem(src));
            parse::parse(&mut lex, kind).expect("parse failed")
        }

        // `pass\n` -> a no-op module body; implicit `return None` at the end.
        let tree = parse_src(b"pass\n", parse::InputKind::File);
        let raw = compile::compile_file(&tree).expect("compile 'pass' failed");
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
            runtime::VmReturnKind::Normal
        );
        assert!(objects::objnone::is_none(st.result));

        // `1\n` as an eval expression -> RETURN_VALUE of the literal itself
        // (no implicit None -- eval returns the expression's value).
        let tree = parse_src(b"1\n", parse::InputKind::Eval);
        let raw = compile::compile_eval(&tree).expect("compile eval '1' failed");
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(obj::small_int_value_checked(st.result), Some(1));

        // `1 + 2\n` eval -> exercises ArithExpr chain + BINARY_OP_ADD.
        let tree = parse_src(b"1 + 2\n", parse::InputKind::Eval);
        let raw = compile::compile_eval(&tree).expect("compile eval '1 + 2' failed");
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(obj::small_int_value_checked(st.result), Some(3));

        // A richer expression exercising Term/ArithExpr precedence,
        // comparison, and unary '-' in one eval: (1 * 2 + 3) < -(0) -> False.
        let tree = parse_src(b"1 * 2 + 3 < -0\n", parse::InputKind::Eval);
        let raw = compile::compile_eval(&tree).expect("compile eval precedence failed");
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(objects::objbool::value(st.result), Some(false));

        // Module-level assignment + name load: `x = 1; y = x + 2`, then
        // read `y` back out of the caller-supplied globals dict.
        unsafe {
            let tree = parse_src(b"x = 1\ny = x + 2\n", parse::InputKind::File);
            let raw = compile::compile_file(&tree).expect("compile 'x=1;y=x+2' failed");
            let globals = objects::objdict::new(4);
            let mut st = vm::CodeState::new();
            st.globals = globals;
            assert_eq!(
                vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                runtime::VmReturnKind::Normal
            );
            assert!(objects::objnone::is_none(st.result));
            let y = obj::new_qstr(qstr::from_str("y"));
            assert_eq!(
                obj::small_int_value_checked(objects::objdict::load(globals, y).unwrap()),
                Some(3)
            );
            objects::objdict::free(globals);
        }

        // Function-local slots: `def f(): x = 1; return x` compiled via
        // `compile_funcdef_body` (locals + optional params).
        let tree = parse_src(
            b"def f():\n    x = 1\n    return x\n",
            parse::InputKind::File,
        );
        let raw = compile::compile_funcdef_body(&tree).expect("compile funcdef body failed");
        assert_eq!(raw.n_state, 1);
        let mut st = vm::CodeState::new();
        assert_eq!(
            vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
            runtime::VmReturnKind::Normal
        );
        assert_eq!(obj::small_int_value_checked(st.result), Some(1));

        // `if 1: x = 2` at module scope binds `x` in globals.
        unsafe {
            let tree = parse_src(b"if 1:\n    x = 2\n", parse::InputKind::File);
            let raw = compile::compile_file(&tree).expect("compile 'if' failed");
            let globals = objects::objdict::new(4);
            let mut st = vm::CodeState::new();
            st.globals = globals;
            assert_eq!(
                vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                runtime::VmReturnKind::Normal
            );
            let x = obj::new_qstr(qstr::from_str("x"));
            assert_eq!(
                obj::small_int_value_checked(objects::objdict::load(globals, x).unwrap()),
                Some(2)
            );
            objects::objdict::free(globals);
        }

        // Top-level `def` + call via `FunBc` and shared globals.
        unsafe {
            let tree = parse_src(
                b"def add(a,b):\n    return a+b\nr = add(2,3)\n",
                parse::InputKind::File,
            );
            let raw = compile::compile_file(&tree).expect("compile 'def add' failed");
            let globals = objects::objdict::new(8);
            let mut st = vm::CodeState::new();
            st.globals = globals;
            assert_eq!(
                vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                runtime::VmReturnKind::Normal
            );
            let r = obj::new_qstr(qstr::from_str("r"));
            assert_eq!(
                obj::small_int_value_checked(objects::objdict::load(globals, r).unwrap()),
                Some(5)
            );
            objects::objdict::free(globals);
        }

        // `from errno import ENOENT` after reg bind (real import-from path).
        unsafe {
            assert_eq!(pm_metal_py_bind_reg(), 0);
            let tree = parse_src(b"from errno import ENOENT\n", parse::InputKind::File);
            let raw = compile::compile_file(&tree).expect("compile from-import failed");
            let globals = objects::objdict::new(4);
            let mut st = vm::CodeState::new();
            st.globals = globals;
            assert_eq!(
                vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                runtime::VmReturnKind::Normal
            );
            let enoent = obj::new_qstr(qstr::from_str("ENOENT"));
            assert_eq!(
                obj::small_int_value_checked(objects::objdict::load(globals, enoent).unwrap()),
                Some(2)
            );
            objects::objdict::free(globals);
        }

        // Honest rejection: dict comprehensions are unsupported.
        let tree = parse_src(b"{x for x in y}\n", parse::InputKind::Eval);
        match compile::compile_eval(&tree) {
            Ok(_) => panic!("expected Unsupported for dict comp, got Ok"),
            Err(compile::CompileError::Unsupported { .. }) => {}
            Err(e) => panic!("expected Unsupported for dict comp, got {e:?}"),
        }

        // Extended compile coverage (MicroPython-core bar).
        unsafe {
            fn run_file(src: &'static [u8], globals: obj::MpObj) -> obj::MpObj {
                let tree = parse_src(src, parse::InputKind::File);
                let raw = compile::compile_file(&tree).unwrap_or_else(|e| panic!("compile failed for {:?}: {e:?}", core::str::from_utf8(src)));
                let mut st = vm::CodeState::new();
                st.globals = globals;
                assert_eq!(
                    vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                    runtime::VmReturnKind::Normal
                );
                st.result
            }

            fn run_eval(src: &'static [u8], globals: obj::MpObj) -> obj::MpObj {
                let tree = parse_src(src, parse::InputKind::Eval);
                let raw = compile::compile_eval(&tree)
                    .unwrap_or_else(|e| panic!("compile eval failed for {:?}: {e:?}", core::str::from_utf8(src).unwrap()));
                let mut st = vm::CodeState::new();
                st.globals = globals;
                assert_eq!(
                    vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                    runtime::VmReturnKind::Normal
                );
                st.result
            }

            let globals = objects::objdict::new(16);
            builtin::modbuiltins::seed_callables_into_dict(globals);

            // for x in range(3): sum
            run_file(
                b"s = 0\nfor x in range(3):\n    s = s + x\n",
                globals,
            );
            assert_eq!(
                obj::small_int_value_checked(
                    objects::objdict::load(globals, obj::new_qstr(qstr::from_str("s"))).unwrap()
                ),
                Some(3)
            );

            // short-circuit and/or + ternary
            assert_eq!(obj::small_int_value_checked(run_eval(b"1 and 0\n", globals)), Some(0));
            assert_eq!(obj::small_int_value_checked(run_eval(b"0 or 5\n", globals)), Some(5));
            assert_eq!(
                obj::small_int_value_checked(run_eval(b"2 if 1 else 3\n", globals)),
                Some(2)
            );

            // in / is
            assert_eq!(
                objects::objbool::value(run_eval(b"1 in [1, 2]\n", globals)),
                Some(true)
            );
            run_file(b"a = 1\nb = 1\n", globals);
            assert_eq!(
                objects::objbool::value(run_eval(b"a is b\n", globals)),
                Some(true)
            );

            // dict/set literals + subscript
            run_eval(b"{1: 2}\n", globals);
            assert_eq!(
                obj::small_int_value_checked(run_eval(b"{1: 2}[1]\n", globals)),
                Some(2)
            );
            assert_eq!(
                objects::objset::len(run_eval(b"{1, 2}\n", globals)),
                Some(2)
            );

            // augmented assign
            run_file(b"x = 1\nx += 1\n", globals);
            assert_eq!(
                obj::small_int_value_checked(
                    objects::objdict::load(globals, obj::new_qstr(qstr::from_str("x"))).unwrap()
                ),
                Some(2)
            );

            // class + instance attr via __init__
            run_file(
                b"class C:\n    def __init__(self):\n        self.v = 1\nv = C().v\n",
                globals,
            );
            assert_eq!(
                obj::small_int_value_checked(
                    objects::objdict::load(globals, obj::new_qstr(qstr::from_str("v"))).unwrap()
                ),
                Some(1)
            );

            // try/except
            run_file(
                b"try:\n    raise 1\nexcept Exception:\n    x = 2\n",
                globals,
            );
            assert_eq!(
                obj::small_int_value_checked(
                    objects::objdict::load(globals, obj::new_qstr(qstr::from_str("x"))).unwrap()
                ),
                Some(2)
            );

            // lambda
            assert_eq!(
                obj::small_int_value_checked(run_eval(b"(lambda a: a + 1)(4)\n", globals)),
                Some(5)
            );

            // list comprehension
            let lc = run_eval(b"[x + 1 for x in range(3)]\n", globals);
            assert_eq!(objects::objlist::len(lc), Some(3));
            assert_eq!(objects::objlist::get(lc, 0), Some(obj::new_small_int(1)));

            // nested def
            run_file(
                b"def outer():\n    def inner():\n        return 7\n    return inner()\nr = outer()\n",
                globals,
            );
            assert_eq!(
                obj::small_int_value_checked(
                    objects::objdict::load(globals, obj::new_qstr(qstr::from_str("r"))).unwrap()
                ),
                Some(7)
            );

            objects::objdict::free(globals);
        }

        // REPL-shaped: import json + call dumps (typed extmod, not marker).
        unsafe {
            let globals = objects::objdict::new(8);
            builtin::modbuiltins::seed_callables_into_dict(globals);
            let src = b"import json\nx = json.dumps(1)\n";
            let tree = parse_src(src, parse::InputKind::File);
            let raw = compile::compile_file(&tree).expect("compile json.dumps");
            let mut st = vm::CodeState::new();
            st.globals = globals;
            assert_eq!(
                vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st),
                runtime::VmReturnKind::Normal
            );
            let x = objects::objdict::load(globals, obj::new_qstr(qstr::from_str("x"))).unwrap();
            assert_eq!(objects::objstr::as_bytes(x), Some(&b"1"[..]));
            objects::objdict::free(globals);
        }

        unsafe { qstr::reset_dynamic_for_test() };
    }

    // --- B11: frozenmod registry + repl continue/exec (W11.1) ---
    unsafe {
        assert!(frozenmod::register_str("frozen_demo", b"x = 1\n"));
        let fr = frozenmod::find("frozen_demo").expect("frozen_demo not registered");
        assert_eq!(fr.name, "frozen_demo");
        assert_eq!(fr.src, &b"x = 1\n"[..]);
        assert_eq!(frozenmod::load_as_source("frozen_demo"), Some(&b"x = 1\n"[..]));
        assert!(frozenmod::load_as_source("no.such.module").is_none());
        // Duplicate name is a real, checkable rejection, not a silent overwrite.
        assert!(!frozenmod::register_str("frozen_demo", b"y = 2\n"));

        assert!(repl::continue_with_input(b"def f():\n"));
        assert!(!repl::continue_with_input(b"pass\n"));
        assert!(repl::continue_with_input(b"(1,\n"));
        assert!(repl::continue_with_input(b"'''still open\n"));
        assert!(!repl::continue_with_input(b""));

        // A persistent module globals dict makes `exec_line` calls into a
        // real multi-line REPL session, not just isolated one-shot evals.
        let globals = objects::objdict::new(4);

        // Run frozenmod's payload as a statement, then read the name it
        // defined back through a *second* exec_line call sharing the same
        // globals -- proves frozenmod's bytes are real, runnable source
        // wired straight into the compile/vm chain, not just stored bytes.
        let src = frozenmod::load_as_source("frozen_demo").unwrap();
        assert_eq!(
            repl::exec_line(src, repl::ReplMode::File, globals),
            Ok(repl::ReplResult::Executed)
        );
        match repl::exec_line(b"x + 2\n", repl::ReplMode::Single, globals) {
            Ok(repl::ReplResult::Value(v)) => {
                assert_eq!(obj::small_int_value_checked(v.obj), Some(3));
                assert_eq!(v.repr(), b"3");
            }
            other => panic!("expected Value(3) for 'x + 2', got {other:?}"),
        }
        objects::objdict::free(globals);

        // Stateless one-shot evals (no globals) -- the two forms B11 must
        // cover: an auto-printed expression, and a plain statement.
        match repl::exec_line(b"1+2\n", repl::ReplMode::Single, obj::OBJ_NULL) {
            Ok(repl::ReplResult::Value(v)) => assert_eq!(v.repr(), b"3"),
            other => panic!("expected Value(3) for '1+2', got {other:?}"),
        }
        assert_eq!(
            repl::exec_line(b"pass\n", repl::ReplMode::Single, obj::OBJ_NULL),
            Ok(repl::ReplResult::Executed)
        );
        assert_eq!(
            repl::exec_line(b"def f():\n", repl::ReplMode::Single, obj::OBJ_NULL),
            Ok(repl::ReplResult::NeedMore)
        );

        frozenmod::reset_for_test();
        qstr::reset_dynamic_for_test();
    }

    // --- B12: cooperative REPL loop (pm_metal_py_loop_* + mphal wiring, W11.2) ---
    unsafe {
        assert_eq!(pm_metal_py_loop_reset(), 0);

        // A single complete line executes immediately; its small-int
        // result comes back through the honest two-part test seam
        // (valid flag + value, not one sentinel that could be confused
        // with a real result).
        assert_eq!(pm_metal_py_loop_feed(b"1+2\n".as_ptr(), 4), 4);
        assert_eq!(pm_metal_py_loop_step(), 0);
        assert_eq!(pm_metal_py_loop_last_result_valid(), 1);
        assert_eq!(pm_metal_py_loop_last_result_i32(), 3);

        // An incomplete compound statement is a real PS2 continuation
        // (`exec_line`'s own `continue_with_input` check, exercised
        // through the loop this time), not silently executed or dropped.
        assert_eq!(pm_metal_py_loop_feed(b"def f():\n".as_ptr(), 9), 9);
        assert_eq!(pm_metal_py_loop_step(), 1);
        assert_eq!(pm_metal_py_loop_reset(), 0);

        // A plain statement executes with nothing to auto-print -- the
        // last-value seam honestly reports "no result", not a stale one.
        assert_eq!(pm_metal_py_loop_feed(b"pass\n".as_ptr(), 5), 5);
        assert_eq!(pm_metal_py_loop_step(), 0);
        assert_eq!(pm_metal_py_loop_last_result_valid(), 0);

        // A submission with no trailing '\n' anywhere near the line
        // buffer's cap is a real, checkable overflow -- not silent
        // truncation. Feed/step in bounded chunks (steps must stay
        // short, see metal-no-long-running-ops) until it trips.
        let chunk = [b'x'; 64];
        let mut saw_error = false;
        for _ in 0..40 {
            let _ = pm_metal_py_loop_feed(chunk.as_ptr(), chunk.len());
            if pm_metal_py_loop_step() == -1 {
                saw_error = true;
                break;
            }
        }
        assert!(saw_error, "expected loop_step overflow to return -1");
        assert_eq!(pm_metal_py_loop_last_result_valid(), 0);

        assert_eq!(pm_metal_py_loop_reset(), 0);
    }

    // --- B13: shell task idle on host (no UART / async runners here) ---
    assert_eq!(pymergetic_metal_py::pm_metal_py_shell_running(), 0);

    // --- B14: upy async concurrency + 1-runner/core metrics (W11.5) ---
    unsafe {
        extern "C" {
            fn pm_metal_async_n_runners() -> u32;
            fn pm_metal_async_metric_spawns() -> u64;
            fn pm_metal_async_metric_awaits() -> u64;
            fn pm_metal_async_metric_steps(runner: u32) -> u64;
        }
        assert_eq!(pm_metal_async_n_runners(), 4);
        let conc = pymergetic_metal_py::pm_metal_py_proof_concurrency();
        assert_eq!(conc, 0, "proof_concurrency failed rc={conc}");
        assert!(pm_metal_async_metric_spawns() >= 32);
        assert!(pm_metal_async_metric_awaits() > 0);
        let mut active = 0u32;
        let mut total = 0u64;
        for r in 0..4u32 {
            let s = pm_metal_async_metric_steps(r);
            total = total.saturating_add(s);
            if s > 0 {
                active += 1;
            }
        }
        assert!(total >= 32, "total steps {total}");
        assert!(active >= 2, "need multi-runner progress, active={active}");
    }

    eprintln!("py W4.2 smoke ok");
}


