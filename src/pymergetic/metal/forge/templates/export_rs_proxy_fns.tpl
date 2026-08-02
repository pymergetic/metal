{% for f in fns %}pub unsafe fn {{ f.name }}({{ f.args }}){{ f.sig_ret }} {
    let __p = __PM_METAL_IMPORT_{{ f.name }}.entry().map(|e| e.get()).unwrap_or(core::ptr::null());
    assert!(!__p.is_null(), "{{ f.name }}: provider {{ module_name }} not connected");
    let __f: unsafe extern "C" fn({{ f.arg_tys }}){{ f.fn_ty_ret }} = core::mem::transmute(__p);
    __f({{ f.call_args }})
}

{% endfor %}