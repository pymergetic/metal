//! Cold-path completeness ledger — inspect only.
//!
//! Hot dispatch stays on [`crate::RegExport`] (one atomic ptr). This table
//! records many callees (any lang, multiple per lang) and caller edges
//! with honesty / sync-async metadata. Never walked on the call path.

use core::cell::UnsafeCell;
use core::ffi::c_void;

use crate::spin::Spin;
use crate::table::{FUNC_MAX, MODULE_MAX};

pub const LEDGER_METHOD_MAX: usize = 256;
pub const LEDGER_CALLEE_MAX: usize = 8;
pub const LEDGER_CALLER_MAX: usize = 8;
pub const LEDGER_LABEL_MAX: usize = 32;

pub const LANG_C: u8 = 0;
pub const LANG_RS: u8 = 1;
pub const LANG_PY: u8 = 2;

pub const ROLE_MUSCLE: u8 = 0;
pub const ROLE_FACE: u8 = 1;
pub const ROLE_TRAMPOLINE: u8 = 2;
pub const ROLE_SHIM: u8 = 3;

pub const HONESTY_OK: u8 = 0;
pub const HONESTY_STUB: u8 = 1;
pub const HONESTY_INCOMPLETE: u8 = 2;

pub const VIA_IMPORT_ROW: u8 = 0;
pub const VIA_BIND: u8 = 1;
pub const VIA_PY_ATTR: u8 = 2;
pub const VIA_GUEST_FWD: u8 = 3;

#[derive(Clone, Copy)]
struct CalleeRec {
    used: bool,
    lang: u8,
    role: u8,
    honesty: u8,
    sync: bool,
    async_: bool,
    ptr: *const c_void,
    partner: [u8; FUNC_MAX],
    partner_len: u8,
    label: [u8; LEDGER_LABEL_MAX],
    label_len: u8,
}

#[derive(Clone, Copy)]
struct CallerRec {
    used: bool,
    lang: u8,
    via: u8,
    honesty: u8,
    module: [u8; MODULE_MAX],
    module_len: u8,
}

#[derive(Clone, Copy)]
struct MethodRec {
    used: bool,
    module: [u8; MODULE_MAX],
    module_len: u8,
    func: [u8; FUNC_MAX],
    func_len: u8,
    callees: [CalleeRec; LEDGER_CALLEE_MAX],
    callee_n: u8,
    callers: [CallerRec; LEDGER_CALLER_MAX],
    caller_n: u8,
}

impl CalleeRec {
    const fn empty() -> Self {
        Self {
            used: false,
            lang: 0,
            role: 0,
            honesty: 0,
            sync: false,
            async_: false,
            ptr: core::ptr::null(),
            partner: [0; FUNC_MAX],
            partner_len: 0,
            label: [0; LEDGER_LABEL_MAX],
            label_len: 0,
        }
    }
}

impl CallerRec {
    const fn empty() -> Self {
        Self {
            used: false,
            lang: 0,
            via: 0,
            honesty: 0,
            module: [0; MODULE_MAX],
            module_len: 0,
        }
    }
}

impl MethodRec {
    const fn empty() -> Self {
        Self {
            used: false,
            module: [0; MODULE_MAX],
            module_len: 0,
            func: [0; FUNC_MAX],
            func_len: 0,
            callees: [CalleeRec::empty(); LEDGER_CALLEE_MAX],
            callee_n: 0,
            callers: [CallerRec::empty(); LEDGER_CALLER_MAX],
            caller_n: 0,
        }
    }
}

pub struct Ledger {
    lock: Spin,
    methods: UnsafeCell<[MethodRec; LEDGER_METHOD_MAX]>,
    count: UnsafeCell<usize>,
}

// Safety: mutations under `lock`.
unsafe impl Sync for Ledger {}

impl Ledger {
    pub const fn new() -> Self {
        Self {
            lock: Spin::new(),
            methods: UnsafeCell::new([MethodRec::empty(); LEDGER_METHOD_MAX]),
            count: UnsafeCell::new(0),
        }
    }

    fn find_or_alloc(&self, module: &[u8], func: &[u8]) -> Option<usize> {
        let methods = unsafe { &mut *self.methods.get() };
        let count = unsafe { *self.count.get() };
        for i in 0..count {
            if !methods[i].used {
                continue;
            }
            let m = &methods[i];
            if m.module_len as usize == module.len()
                && m.func_len as usize == func.len()
                && m.module[..module.len()] == *module
                && m.func[..func.len()] == *func
            {
                return Some(i);
            }
        }
        if count >= LEDGER_METHOD_MAX {
            return None;
        }
        let i = count;
        methods[i].used = true;
        methods[i].module_len = module.len() as u8;
        methods[i].func_len = func.len() as u8;
        methods[i].module[..module.len()].copy_from_slice(module);
        methods[i].func[..func.len()].copy_from_slice(func);
        methods[i].callee_n = 0;
        methods[i].caller_n = 0;
        unsafe {
            *self.count.get() = count + 1;
        }
        Some(i)
    }

    pub fn add_callee(
        &self,
        module: &[u8],
        func: &[u8],
        lang: u8,
        role: u8,
        honesty: u8,
        sync: bool,
        async_: bool,
        partner: &[u8],
        label: &[u8],
        ptr: *const c_void,
    ) -> i32 {
        if module.is_empty() || func.is_empty() {
            return -1;
        }
        if module.len() >= MODULE_MAX || func.len() >= FUNC_MAX {
            return -1;
        }
        self.lock.lock();
        let rc = (|| {
            let i = self.find_or_alloc(module, func)?;
            let methods = unsafe { &mut *self.methods.get() };
            let m = &mut methods[i];
            let llen = label.len().min(LEDGER_LABEL_MAX - 1);
            /* Dedupe by lang+role+label so connect/publish/seed are idempotent. */
            for j in 0..(m.callee_n as usize) {
                let c = &mut m.callees[j];
                if !c.used {
                    continue;
                }
                if c.lang == lang
                    && c.role == role
                    && c.label_len as usize == llen
                    && (llen == 0 || c.label[..llen] == label[..llen])
                {
                    c.honesty = honesty;
                    c.sync = sync;
                    c.async_ = async_;
                    c.ptr = ptr;
                    let plen = partner.len().min(FUNC_MAX - 1);
                    c.partner_len = plen as u8;
                    if plen > 0 {
                        c.partner[..plen].copy_from_slice(&partner[..plen]);
                    }
                    return Some(0i32);
                }
            }
            if m.callee_n as usize >= LEDGER_CALLEE_MAX {
                return None;
            }
            let j = m.callee_n as usize;
            let c = &mut m.callees[j];
            *c = CalleeRec::empty();
            c.used = true;
            c.lang = lang;
            c.role = role;
            c.honesty = honesty;
            c.sync = sync;
            c.async_ = async_;
            c.ptr = ptr;
            let plen = partner.len().min(FUNC_MAX - 1);
            c.partner_len = plen as u8;
            if plen > 0 {
                c.partner[..plen].copy_from_slice(&partner[..plen]);
            }
            c.label_len = llen as u8;
            if llen > 0 {
                c.label[..llen].copy_from_slice(&label[..llen]);
            }
            m.callee_n = m.callee_n.saturating_add(1);
            Some(0i32)
        })();
        self.lock.unlock();
        rc.unwrap_or(-1)
    }

    pub fn add_caller(
        &self,
        module: &[u8],
        func: &[u8],
        lang: u8,
        caller_module: &[u8],
        via: u8,
        honesty: u8,
    ) -> i32 {
        if module.is_empty() || func.is_empty() || caller_module.is_empty() {
            return -1;
        }
        if module.len() >= MODULE_MAX
            || func.len() >= FUNC_MAX
            || caller_module.len() >= MODULE_MAX
        {
            return -1;
        }
        self.lock.lock();
        let rc = (|| {
            let i = self.find_or_alloc(module, func)?;
            let methods = unsafe { &mut *self.methods.get() };
            let m = &mut methods[i];
            for j in 0..(m.caller_n as usize) {
                let c = &mut m.callers[j];
                if !c.used {
                    continue;
                }
                if c.lang == lang
                    && c.via == via
                    && c.module_len as usize == caller_module.len()
                    && c.module[..caller_module.len()] == *caller_module
                {
                    c.honesty = honesty;
                    return Some(0i32);
                }
            }
            if m.caller_n as usize >= LEDGER_CALLER_MAX {
                return None;
            }
            let j = m.caller_n as usize;
            let c = &mut m.callers[j];
            *c = CallerRec::empty();
            c.used = true;
            c.lang = lang;
            c.via = via;
            c.honesty = honesty;
            c.module_len = caller_module.len() as u8;
            c.module[..caller_module.len()].copy_from_slice(caller_module);
            m.caller_n = m.caller_n.saturating_add(1);
            Some(0i32)
        })();
        self.lock.unlock();
        rc.unwrap_or(-1)
    }

    fn method_is_gap(m: &MethodRec) -> bool {
        let mut has_c = false;
        let mut has_rs = false;
        let mut has_py = false;
        let mut bad = false;
        for j in 0..(m.callee_n as usize) {
            let c = &m.callees[j];
            if !c.used {
                continue;
            }
            match c.lang {
                LANG_C => has_c = true,
                LANG_RS => has_rs = true,
                LANG_PY => has_py = true,
                _ => {}
            }
            if c.honesty != HONESTY_OK {
                bad = true;
            }
            if c.sync && !c.async_ && c.partner_len == 0 && c.role == ROLE_MUSCLE {
                bad = true;
            }
        }
        !has_c || !has_rs || !has_py || bad || (m.caller_n > 0 && m.callee_n == 0)
    }

    pub fn method_count(&self) -> usize {
        self.lock.lock();
        let n = unsafe { *self.count.get() };
        self.lock.unlock();
        n
    }

    pub fn gap_count(&self) -> u32 {
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let mut gaps = 0u32;
        for i in 0..count {
            let m = &methods[i];
            if m.used && Self::method_is_gap(m) {
                gaps = gaps.saturating_add(1);
            }
        }
        self.lock.unlock();
        gaps
    }

    /// Write a compact JSON rollup into `buf` (counts only).
    ///
    /// Full method rows live under `/inspect/reg/<module>` — embedding every
    /// method here does not scale once floor RegMods publish into the ledger.
    /// Returns byte length or -1.
    pub fn write_json(&self, buf: &mut [u8]) -> i32 {
        let mut w = JsonWriter::new(buf);
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let gaps = {
            let mut g = 0u32;
            for i in 0..count {
                let m = &methods[i];
                if m.used && Self::method_is_gap(m) {
                    g = g.saturating_add(1);
                }
            }
            g
        };
        let ok = (|| {
            w.obj_start()?;
            w.key("schema")?;
            w.u32(1)?;
            w.comma()?;
            w.key("method_count")?;
            w.u32(count as u32)?;
            w.comma()?;
            w.key("gap_count")?;
            w.u32(gaps)?;
            w.obj_end()?;
            Some(())
        })();
        self.lock.unlock();
        match ok {
            Some(()) => w.len() as i32,
            None => -1,
        }
    }

    pub fn write_module_json(&self, module: &[u8], buf: &mut [u8]) -> i32 {
        let mut w = JsonWriter::new(buf);
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let mut method_count = 0u32;
        let mut gaps = 0u32;
        for i in 0..count {
            let m = &methods[i];
            if !m.used || m.module_len as usize != module.len() || m.module[..module.len()] != *module
            {
                continue;
            }
            method_count = method_count.saturating_add(1);
            if Self::method_is_gap(m) {
                gaps = gaps.saturating_add(1);
            }
        }
        if method_count == 0 {
            self.lock.unlock();
            return -1;
        }
        let ok = (|| {
            w.obj_start()?;
            w.key("module")?;
            w.str_bytes(module)?;
            w.comma()?;
            w.key("method_count")?;
            w.u32(method_count)?;
            w.comma()?;
            w.key("gap_count")?;
            w.u32(gaps)?;
            w.comma()?;
            w.key("methods")?;
            w.arr_start()?;
            let mut first = true;
            for i in 0..count {
                let m = &methods[i];
                if !m.used
                    || m.module_len as usize != module.len()
                    || m.module[..module.len()] != *module
                {
                    continue;
                }
                if !first {
                    w.comma()?;
                }
                first = false;
                write_method(&mut w, m)?;
            }
            w.arr_end()?;
            w.obj_end()?;
            Some(())
        })();
        self.lock.unlock();
        match ok {
            Some(()) => w.len() as i32,
            None => -1,
        }
    }

    pub fn write_method_json(&self, module: &[u8], func: &[u8], buf: &mut [u8]) -> i32 {
        let mut w = JsonWriter::new(buf);
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let mut found: Option<&MethodRec> = None;
        for i in 0..count {
            let m = &methods[i];
            if !m.used {
                continue;
            }
            if m.module_len as usize == module.len()
                && m.func_len as usize == func.len()
                && m.module[..module.len()] == *module
                && m.func[..func.len()] == *func
            {
                found = Some(m);
                break;
            }
        }
        let ok = if let Some(m) = found {
            write_method(&mut w, m)
        } else {
            None
        };
        self.lock.unlock();
        match ok {
            Some(()) => w.len() as i32,
            None => -1,
        }
    }
}

fn write_method(w: &mut JsonWriter<'_>, m: &MethodRec) -> Option<()> {
    w.obj_start()?;
    w.key("module")?;
    w.str_bytes(&m.module[..m.module_len as usize])?;
    w.comma()?;
    w.key("func")?;
    w.str_bytes(&m.func[..m.func_len as usize])?;
    w.comma()?;
    w.key("callees")?;
    w.arr_start()?;
    let mut first = true;
    for j in 0..(m.callee_n as usize) {
        let c = &m.callees[j];
        if !c.used {
            continue;
        }
        if !first {
            w.comma()?;
        }
        first = false;
        w.obj_start()?;
        w.key("lang")?;
        w.str(lang_name(c.lang))?;
        w.comma()?;
        w.key("role")?;
        w.str(role_name(c.role))?;
        w.comma()?;
        w.key("honesty")?;
        w.str(honesty_name(c.honesty))?;
        w.comma()?;
        w.key("sync")?;
        w.bool(c.sync)?;
        w.comma()?;
        w.key("async")?;
        w.bool(c.async_)?;
        w.comma()?;
        w.key("label")?;
        w.str_bytes(&c.label[..c.label_len as usize])?;
        w.comma()?;
        w.key("async_partner")?;
        w.str_bytes(&c.partner[..c.partner_len as usize])?;
        w.comma()?;
        w.key("has_ptr")?;
        w.bool(!c.ptr.is_null())?;
        w.obj_end()?;
    }
    w.arr_end()?;
    w.comma()?;
    w.key("callers")?;
    w.arr_start()?;
    first = true;
    for j in 0..(m.caller_n as usize) {
        let c = &m.callers[j];
        if !c.used {
            continue;
        }
        if !first {
            w.comma()?;
        }
        first = false;
        w.obj_start()?;
        w.key("lang")?;
        w.str(lang_name(c.lang))?;
        w.comma()?;
        w.key("module")?;
        w.str_bytes(&c.module[..c.module_len as usize])?;
        w.comma()?;
        w.key("via")?;
        w.str(via_name(c.via))?;
        w.comma()?;
        w.key("honesty")?;
        w.str(honesty_name(c.honesty))?;
        w.obj_end()?;
    }
    w.arr_end()?;
    w.obj_end()?;
    Some(())
}

fn lang_name(l: u8) -> &'static str {
    match l {
        LANG_C => "c",
        LANG_RS => "rs",
        LANG_PY => "py",
        _ => "?",
    }
}
fn role_name(r: u8) -> &'static str {
    match r {
        ROLE_MUSCLE => "muscle",
        ROLE_FACE => "face",
        ROLE_TRAMPOLINE => "trampoline",
        ROLE_SHIM => "shim",
        _ => "?",
    }
}
fn honesty_name(h: u8) -> &'static str {
    match h {
        HONESTY_OK => "ok",
        HONESTY_STUB => "stub",
        HONESTY_INCOMPLETE => "incomplete",
        _ => "?",
    }
}
fn via_name(v: u8) -> &'static str {
    match v {
        VIA_IMPORT_ROW => "import_row",
        VIA_BIND => "bind",
        VIA_PY_ATTR => "py_attr",
        VIA_GUEST_FWD => "guest_fwd",
        _ => "?",
    }
}

struct JsonWriter<'a> {
    buf: &'a mut [u8],
    n: usize,
}

impl<'a> JsonWriter<'a> {
    fn new(buf: &'a mut [u8]) -> Self {
        Self { buf, n: 0 }
    }
    fn len(&self) -> usize {
        self.n
    }
    fn push(&mut self, b: u8) -> Option<()> {
        if self.n >= self.buf.len() {
            return None;
        }
        self.buf[self.n] = b;
        self.n += 1;
        Some(())
    }
    fn raw(&mut self, s: &str) -> Option<()> {
        for b in s.as_bytes() {
            self.push(*b)?;
        }
        Some(())
    }
    fn obj_start(&mut self) -> Option<()> {
        self.push(b'{')
    }
    fn obj_end(&mut self) -> Option<()> {
        self.push(b'}')
    }
    fn arr_start(&mut self) -> Option<()> {
        self.push(b'[')
    }
    fn arr_end(&mut self) -> Option<()> {
        self.push(b']')
    }
    fn comma(&mut self) -> Option<()> {
        self.push(b',')
    }
    fn key(&mut self, k: &str) -> Option<()> {
        self.str(k)?;
        self.push(b':')
    }
    fn bool(&mut self, v: bool) -> Option<()> {
        self.raw(if v { "true" } else { "false" })
    }
    fn u32(&mut self, v: u32) -> Option<()> {
        let mut tmp = [0u8; 10];
        let mut x = v;
        let mut i = tmp.len();
        if x == 0 {
            return self.push(b'0');
        }
        while x > 0 {
            i -= 1;
            tmp[i] = b'0' + (x % 10) as u8;
            x /= 10;
        }
        for b in &tmp[i..] {
            self.push(*b)?;
        }
        Some(())
    }
    fn str(&mut self, s: &str) -> Option<()> {
        self.str_bytes(s.as_bytes())
    }
    fn str_bytes(&mut self, s: &[u8]) -> Option<()> {
        self.push(b'"')?;
        for &b in s {
            match b {
                b'"' | b'\\' => {
                    self.push(b'\\')?;
                    self.push(b)?;
                }
                b if b < 0x20 => {
                    /* skip control */
                }
                _ => self.push(b)?,
            }
        }
        self.push(b'"')
    }
}

/// Completeness report options (cold path / inspect only).
#[derive(Clone, Copy)]
pub struct CompletenessOpts<'a> {
    pub module: Option<&'a [u8]>,
    pub gaps_only: bool,
    pub detail: bool,
    pub json: bool,
}

fn method_lang_flags(m: &MethodRec) -> (bool, bool, bool) {
    let mut has_c = false;
    let mut has_rs = false;
    let mut has_py = false;
    for j in 0..(m.callee_n as usize) {
        let c = &m.callees[j];
        if !c.used {
            continue;
        }
        match c.lang {
            LANG_C => has_c = true,
            LANG_RS => has_rs = true,
            LANG_PY => has_py = true,
            _ => {}
        }
    }
    (has_c, has_rs, has_py)
}

fn method_main_lang(m: &MethodRec) -> &'static str {
    for pref in [LANG_C, LANG_RS, LANG_PY] {
        for j in 0..(m.callee_n as usize) {
            let c = &m.callees[j];
            if c.used && c.role == ROLE_MUSCLE && !c.ptr.is_null() && c.lang == pref {
                return lang_name(pref);
            }
        }
    }
    for pref in [LANG_C, LANG_RS, LANG_PY] {
        for j in 0..(m.callee_n as usize) {
            let c = &m.callees[j];
            if c.used && c.role == ROLE_MUSCLE && c.lang == pref {
                return lang_name(pref);
            }
        }
    }
    for j in 0..(m.callee_n as usize) {
        let c = &m.callees[j];
        if c.used {
            return lang_name(c.lang);
        }
    }
    "?"
}

fn module_matches(m: &MethodRec, filter: Option<&[u8]>) -> bool {
    match filter {
        None => true,
        Some(want) => {
            m.used
                && m.module_len as usize == want.len()
                && m.module[..want.len()] == *want
        }
    }
}

struct TextWriter<'a> {
    buf: &'a mut [u8],
    n: usize,
}

impl<'a> TextWriter<'a> {
    fn new(buf: &'a mut [u8]) -> Self {
        Self { buf, n: 0 }
    }
    fn len(&self) -> usize {
        self.n
    }
    fn push(&mut self, b: u8) -> Option<()> {
        if self.n >= self.buf.len() {
            return None;
        }
        self.buf[self.n] = b;
        self.n += 1;
        Some(())
    }
    fn raw(&mut self, s: &str) -> Option<()> {
        for b in s.as_bytes() {
            self.push(*b)?;
        }
        Some(())
    }
    fn bytes(&mut self, s: &[u8]) -> Option<()> {
        for &b in s {
            self.push(b)?;
        }
        Some(())
    }
    fn u32(&mut self, v: u32) -> Option<()> {
        let mut tmp = [0u8; 10];
        let mut x = v;
        let mut i = tmp.len();
        if x == 0 {
            return self.push(b'0');
        }
        while x > 0 {
            i -= 1;
            tmp[i] = b'0' + (x % 10) as u8;
            x /= 10;
        }
        for b in &tmp[i..] {
            self.push(*b)?;
        }
        Some(())
    }
    fn nl(&mut self) -> Option<()> {
        self.push(b'\n')
    }
}

impl Ledger {
    pub fn write_completeness(&self, opts: CompletenessOpts<'_>, buf: &mut [u8]) -> i32 {
        if opts.json {
            self.write_completeness_json(opts, buf)
        } else {
            self.write_completeness_tree(opts, buf)
        }
    }

    fn write_completeness_tree(&self, opts: CompletenessOpts<'_>, buf: &mut [u8]) -> i32 {
        let mut w = TextWriter::new(buf);
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let ok = (|| {
            let mut method_n = 0u32;
            let mut gap_n = 0u32;
            let mut mod_n = 0u32;
            let mut seen_mod: [bool; LEDGER_METHOD_MAX] = [false; LEDGER_METHOD_MAX];
            for i in 0..count {
                let m = &methods[i];
                if !m.used || !module_matches(m, opts.module) {
                    continue;
                }
                let gap = Self::method_is_gap(m);
                if opts.gaps_only && !gap {
                    continue;
                }
                method_n = method_n.saturating_add(1);
                if gap {
                    gap_n = gap_n.saturating_add(1);
                }
                let mut first = true;
                for j in 0..i {
                    let o = &methods[j];
                    if o.used
                        && o.module_len == m.module_len
                        && o.module[..m.module_len as usize] == m.module[..m.module_len as usize]
                        && module_matches(o, opts.module)
                        && (!opts.gaps_only || Self::method_is_gap(o))
                    {
                        first = false;
                        break;
                    }
                }
                if first {
                    mod_n = mod_n.saturating_add(1);
                    seen_mod[i] = true;
                }
            }

            w.raw("reg completeness  methods=")?;
            w.u32(method_n)?;
            w.raw("  gaps=")?;
            w.u32(gap_n)?;
            w.raw("  modules=")?;
            w.u32(mod_n)?;
            w.nl()?;

            /* Emit modules in first-seen order. */
            let mut mod_idx = 0u32;
            for i in 0..count {
                if !seen_mod[i] {
                    continue;
                }
                let m0 = &methods[i];
                let mod_bytes = &m0.module[..m0.module_len as usize];
                let mut m_count = 0u32;
                let mut m_gaps = 0u32;
                for j in 0..count {
                    let m = &methods[j];
                    if !m.used
                        || m.module_len as usize != mod_bytes.len()
                        || m.module[..mod_bytes.len()] != *mod_bytes
                    {
                        continue;
                    }
                    let gap = Self::method_is_gap(m);
                    if opts.gaps_only && !gap {
                        continue;
                    }
                    m_count = m_count.saturating_add(1);
                    if gap {
                        m_gaps = m_gaps.saturating_add(1);
                    }
                }
                mod_idx = mod_idx.saturating_add(1);
                let last_mod = mod_idx == mod_n;
                w.raw(if last_mod { "└─ " } else { "├─ " })?;
                w.bytes(mod_bytes)?;
                w.raw("  methods=")?;
                w.u32(m_count)?;
                w.raw("  gaps=")?;
                w.u32(m_gaps)?;
                w.nl()?;

                let mut emitted = 0u32;
                for j in 0..count {
                    let m = &methods[j];
                    if !m.used
                        || m.module_len as usize != mod_bytes.len()
                        || m.module[..mod_bytes.len()] != *mod_bytes
                    {
                        continue;
                    }
                    let gap = Self::method_is_gap(m);
                    if opts.gaps_only && !gap {
                        continue;
                    }
                    emitted = emitted.saturating_add(1);
                    let last_m = emitted == m_count;
                    let branch = if last_mod { "   " } else { "│  " };
                    w.raw(branch)?;
                    w.raw(if last_m { "└─ " } else { "├─ " })?;
                    w.bytes(&m.func[..m.func_len as usize])?;
                    w.raw("  main=")?;
                    w.raw(method_main_lang(m))?;
                    let (hc, hrs, hpy) = method_lang_flags(m);
                    w.raw("  have=")?;
                    let mut first_lang = true;
                    for (on, name) in [(hc, "c"), (hrs, "rs"), (hpy, "py")] {
                        if on {
                            if !first_lang {
                                w.push(b',')?;
                            }
                            first_lang = false;
                            w.raw(name)?;
                        }
                    }
                    if first_lang {
                        w.raw("-")?;
                    }
                    let mut miss_first = true;
                    for (on, name) in [(hc, "c"), (hrs, "rs"), (hpy, "py")] {
                        if !on {
                            if miss_first {
                                w.raw("  miss=")?;
                                miss_first = false;
                            } else {
                                w.push(b',')?;
                            }
                            w.raw(name)?;
                        }
                    }
                    w.raw("  faces=")?;
                    let mut face_first = true;
                    for k in 0..(m.callee_n as usize) {
                        let c = &m.callees[k];
                        if c.used && c.role == ROLE_FACE {
                            if !face_first {
                                w.push(b',')?;
                            }
                            face_first = false;
                            w.raw(lang_name(c.lang))?;
                        }
                    }
                    if face_first {
                        w.raw("-")?;
                    }
                    let mut bad_first = true;
                    for k in 0..(m.callee_n as usize) {
                        let c = &m.callees[k];
                        if c.used && c.honesty != HONESTY_OK {
                            if bad_first {
                                w.raw("  bad=")?;
                                bad_first = false;
                            } else {
                                w.push(b',')?;
                            }
                            w.raw(lang_name(c.lang))?;
                            w.push(b':')?;
                            w.raw(honesty_name(c.honesty))?;
                        }
                    }
                    if !gap {
                        w.raw("  ok")?;
                    }
                    w.nl()?;
                    if opts.detail {
                        for k in 0..(m.callee_n as usize) {
                            let c = &m.callees[k];
                            if !c.used {
                                continue;
                            }
                            w.raw(branch)?;
                            w.raw(if last_m { "   " } else { "│  " })?;
                            w.raw("├─ ")?;
                            w.raw(lang_name(c.lang))?;
                            w.push(b' ')?;
                            w.raw(role_name(c.role))?;
                            w.push(b' ')?;
                            w.raw(honesty_name(c.honesty))?;
                            if !c.ptr.is_null() {
                                w.raw("  ptr")?;
                            }
                            w.nl()?;
                        }
                    }
                }
            }
            Some(())
        })();
        self.lock.unlock();
        match ok {
            Some(()) => w.len() as i32,
            None => -1,
        }
    }

    fn write_completeness_json(&self, opts: CompletenessOpts<'_>, buf: &mut [u8]) -> i32 {
        let mut w = JsonWriter::new(buf);
        self.lock.lock();
        let methods = unsafe { &*self.methods.get() };
        let count = unsafe { *self.count.get() };
        let ok = (|| {
            let mut method_n = 0u32;
            let mut gap_n = 0u32;
            let mut mod_heads: [usize; LEDGER_METHOD_MAX] = [0; LEDGER_METHOD_MAX];
            let mut mod_n = 0usize;
            for i in 0..count {
                let m = &methods[i];
                if !m.used || !module_matches(m, opts.module) {
                    continue;
                }
                let gap = Self::method_is_gap(m);
                if opts.gaps_only && !gap {
                    continue;
                }
                method_n = method_n.saturating_add(1);
                if gap {
                    gap_n = gap_n.saturating_add(1);
                }
                let mut found = false;
                for h in 0..mod_n {
                    let o = &methods[mod_heads[h]];
                    if o.module_len == m.module_len
                        && o.module[..m.module_len as usize] == m.module[..m.module_len as usize]
                    {
                        found = true;
                        break;
                    }
                }
                if !found && mod_n < LEDGER_METHOD_MAX {
                    mod_heads[mod_n] = i;
                    mod_n += 1;
                }
            }

            w.obj_start()?;
            w.key("schema")?;
            w.u32(1)?;
            w.comma()?;
            w.key("method_count")?;
            w.u32(method_n)?;
            w.comma()?;
            w.key("gap_count")?;
            w.u32(gap_n)?;
            w.comma()?;
            w.key("modules")?;
            w.arr_start()?;
            for h in 0..mod_n {
                if h > 0 {
                    w.comma()?;
                }
                let mod_bytes = {
                    let m0 = &methods[mod_heads[h]];
                    &m0.module[..m0.module_len as usize]
                };
                let mut m_count = 0u32;
                let mut m_gaps = 0u32;
                for j in 0..count {
                    let m = &methods[j];
                    if !m.used
                        || m.module_len as usize != mod_bytes.len()
                        || m.module[..mod_bytes.len()] != *mod_bytes
                    {
                        continue;
                    }
                    let gap = Self::method_is_gap(m);
                    if opts.gaps_only && !gap {
                        continue;
                    }
                    m_count = m_count.saturating_add(1);
                    if gap {
                        m_gaps = m_gaps.saturating_add(1);
                    }
                }
                w.obj_start()?;
                w.key("module")?;
                w.str_bytes(mod_bytes)?;
                w.comma()?;
                w.key("methods")?;
                w.u32(m_count)?;
                w.comma()?;
                w.key("gaps")?;
                w.u32(m_gaps)?;
                w.obj_end()?;
            }
            w.arr_end()?;
            w.comma()?;
            w.key("gaps")?;
            w.arr_start()?;
            /* Per-gap rows only with detail=1 — floor RegMods make the full
             * gap list too large for Inspect's default body buffers. */
            let mut first_gap = true;
            for i in 0..count {
                if !opts.detail {
                    break;
                }
                let m = &methods[i];
                if !m.used || !module_matches(m, opts.module) || !Self::method_is_gap(m) {
                    continue;
                }
                if !first_gap {
                    w.comma()?;
                }
                first_gap = false;
                let (hc, hrs, hpy) = method_lang_flags(m);
                w.obj_start()?;
                w.key("module")?;
                w.str_bytes(&m.module[..m.module_len as usize])?;
                w.comma()?;
                w.key("func")?;
                w.str_bytes(&m.func[..m.func_len as usize])?;
                w.comma()?;
                w.key("main")?;
                w.str(method_main_lang(m))?;
                w.comma()?;
                w.key("have")?;
                w.arr_start()?;
                let mut fl = true;
                for (on, name) in [(hc, "c"), (hrs, "rs"), (hpy, "py")] {
                    if on {
                        if !fl {
                            w.comma()?;
                        }
                        fl = false;
                        w.str(name)?;
                    }
                }
                w.arr_end()?;
                w.comma()?;
                w.key("miss")?;
                w.arr_start()?;
                fl = true;
                for (on, name) in [(hc, "c"), (hrs, "rs"), (hpy, "py")] {
                    if !on {
                        if !fl {
                            w.comma()?;
                        }
                        fl = false;
                        w.str(name)?;
                    }
                }
                w.arr_end()?;
                w.comma()?;
                w.key("faces")?;
                w.arr_start()?;
                fl = true;
                for k in 0..(m.callee_n as usize) {
                    let c = &m.callees[k];
                    if c.used && c.role == ROLE_FACE {
                        if !fl {
                            w.comma()?;
                        }
                        fl = false;
                        w.str(lang_name(c.lang))?;
                    }
                }
                w.arr_end()?;
                w.comma()?;
                w.key("bad")?;
                w.arr_start()?;
                fl = true;
                for k in 0..(m.callee_n as usize) {
                    let c = &m.callees[k];
                    if c.used && c.honesty != HONESTY_OK {
                        if !fl {
                            w.comma()?;
                        }
                        fl = false;
                        /* "c:stub" style */
                        let mut tmp = [0u8; 24];
                        let ln = lang_name(c.lang).as_bytes();
                        let hn = honesty_name(c.honesty).as_bytes();
                        let mut n = 0;
                        for &b in ln {
                            if n < tmp.len() {
                                tmp[n] = b;
                                n += 1;
                            }
                        }
                        if n < tmp.len() {
                            tmp[n] = b':';
                            n += 1;
                        }
                        for &b in hn {
                            if n < tmp.len() {
                                tmp[n] = b;
                                n += 1;
                            }
                        }
                        w.str_bytes(&tmp[..n])?;
                    }
                }
                w.arr_end()?;
                if opts.detail {
                    w.comma()?;
                    w.key("callees")?;
                    w.arr_start()?;
                    fl = true;
                    for k in 0..(m.callee_n as usize) {
                        let c = &m.callees[k];
                        if !c.used {
                            continue;
                        }
                        if !fl {
                            w.comma()?;
                        }
                        fl = false;
                        w.obj_start()?;
                        w.key("lang")?;
                        w.str(lang_name(c.lang))?;
                        w.comma()?;
                        w.key("role")?;
                        w.str(role_name(c.role))?;
                        w.comma()?;
                        w.key("honesty")?;
                        w.str(honesty_name(c.honesty))?;
                        w.comma()?;
                        w.key("has_ptr")?;
                        w.bool(!c.ptr.is_null())?;
                        w.obj_end()?;
                    }
                    w.arr_end()?;
                }
                w.obj_end()?;
            }
            w.arr_end()?;
            w.obj_end()?;
            Some(())
        })();
        self.lock.unlock();
        match ok {
            Some(()) => w.len() as i32,
            None => -1,
        }
    }
}

pub static LEDGER: Ledger = Ledger::new();

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    #[test]
    fn honesty_stub_counts_as_gap() {
        let l = Ledger::new();
        assert_eq!(
            l.add_callee(
                b"m.stub",
                b"f",
                LANG_C,
                ROLE_SHIM,
                HONESTY_STUB,
                true,
                false,
                b"",
                b"hal_stub",
                ptr::null(),
            ),
            0
        );
        assert_eq!(l.gap_count(), 1);
    }

    #[test]
    fn sync_muscle_without_async_partner_is_gap() {
        let l = Ledger::new();
        assert_eq!(
            l.add_callee(
                b"m.sync",
                b"parkable",
                LANG_C,
                ROLE_MUSCLE,
                HONESTY_OK,
                true,
                false,
                b"",
                b"c_muscle",
                ptr::null(),
            ),
            0
        );
        assert_eq!(l.gap_count(), 1);
        /* Partner clears the sync/async parity gap; missing langs remain. */
        assert_eq!(
            l.add_callee(
                b"m.sync",
                b"parkable",
                LANG_C,
                ROLE_MUSCLE,
                HONESTY_OK,
                true,
                false,
                b"parkable_async",
                b"c_muscle",
                ptr::null(),
            ),
            0
        );
        assert_eq!(l.method_count(), 1);
        assert_eq!(l.gap_count(), 1); /* still missing rs/py */
        let _ = l.add_callee(
            b"m.sync",
            b"parkable",
            LANG_RS,
            ROLE_FACE,
            HONESTY_OK,
            true,
            true,
            b"parkable_async",
            b"rs_face",
            ptr::null(),
        );
        let _ = l.add_callee(
            b"m.sync",
            b"parkable",
            LANG_PY,
            ROLE_FACE,
            HONESTY_OK,
            true,
            true,
            b"parkable_async",
            b"py_face",
            ptr::null(),
        );
        assert_eq!(l.gap_count(), 0);
    }

    #[test]
    fn add_callee_dedupes_by_label() {
        let l = Ledger::new();
        assert_eq!(
            l.add_callee(
                b"m",
                b"f",
                LANG_C,
                ROLE_MUSCLE,
                HONESTY_OK,
                false,
                true,
                b"",
                b"x",
                ptr::null(),
            ),
            0
        );
        assert_eq!(
            l.add_callee(
                b"m",
                b"f",
                LANG_C,
                ROLE_MUSCLE,
                HONESTY_INCOMPLETE,
                false,
                true,
                b"",
                b"x",
                ptr::null(),
            ),
            0
        );
        let mut buf = [0u8; 512];
        let n = l.write_method_json(b"m", b"f", &mut buf);
        assert!(n > 0);
        let s = core::str::from_utf8(&buf[..n as usize]).unwrap();
        assert_eq!(s.matches("\"lang\"").count(), 1);
        assert!(s.contains("incomplete"));
        assert!(s.contains("async_partner"));
    }
}
