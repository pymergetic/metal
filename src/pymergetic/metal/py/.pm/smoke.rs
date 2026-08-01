//! Host smoke — py edge + B0/B1 upy faces (needs mem init).
use std::alloc::{alloc, Layout};

use pymergetic_metal_mem::api as mem;
use pymergetic_metal_py::upy::py::{
    bc, bc0, builtin, gc, malloc, mpconfig, mpstate, obj, objects, qstr, qstrdefs, runtime, vm,
};
use pymergetic_metal_py::{
    pm_metal_py_alloc, pm_metal_py_bind_reg, pm_metal_py_free, pm_metal_py_gc_enabled,
    pm_metal_py_libc_policy, pm_metal_py_ready,
};
use pymergetic_metal_reg::{pm_metal_reg_bind, pm_metal_reg_call0};

fn main() {
    const N: usize = 256 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());
    assert_eq!(mem::init(base, N), 0);

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
    assert_eq!(vm::execute(&code, &mut st), runtime::VmReturnKind::Normal);
    assert!(obj::is_immediate(st.result));

    // small-int multi: opcode 0x70 + 16 + 5 => value 5
    let op = bc0::LOAD_CONST_SMALL_INT_MULTI + 16 + 5;
    let code2 = [op, bc0::RETURN_VALUE];
    let mut st2 = vm::CodeState::new();
    assert_eq!(vm::execute(&code2, &mut st2), runtime::VmReturnKind::Normal);
    assert!(obj::is_small_int(st2.result));
    assert_eq!(obj::small_int_value(st2.result), 5);

    // unknown opcode -> exception
    let mut st3 = vm::CodeState::new();
    assert_eq!(
        vm::execute(&[0x00], &mut st3),
        runtime::VmReturnKind::Exception
    );

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

        let fun = objects::objfun::new(&[bc0::LOAD_CONST_TRUE, bc0::RETURN_VALUE], 2);
        let code = objects::objfun::code(fun).unwrap();
        let mut stf = vm::CodeState::new();
        assert_eq!(vm::execute(code, &mut stf), runtime::VmReturnKind::Normal);
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
        let metal_py = builtin::builtinimport::import_module("pymergetic.metal.py").unwrap();
        assert!(builtin::builtinimport::has_reg_marker(metal_py, "ready"));
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
        assert!(objects::objmodule::load_attr(jm, obj::new_qstr(qstr::from_str("dumps"))).is_some());
        assert!(builtin::builtinimport::import_module("os").is_some());
        assert!(builtin::builtinimport::import_module("time").is_some());
        assert!(builtin::builtinimport::import_module("re").is_some());
        assert!(builtin::builtinimport::import_module("vfs").is_some());
    }

    // --- B6: asyncio REWRITE -> Metal async ---
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

    eprintln!("py W4.2 smoke ok");
}
