# Upy Rust mirror inventory (complete)

Exhaustive map of upstream MicroPython `py/` + `extmod/` + `shared/` into
`src/pymergetic/metal/py/upy/`. Source of filenames: upstream micropython
(github.com/micropython/micropython). After vendor, compare against
`external/micropython/`.

**No stub implementations.** A `MIRROR`/`REWRITE` row is unfinished until
the `.rs` has real logic. Prefer updating a row to `OMIT_*` with a reason
over landing hollow files.

Parent plan: [`ORCHESTRATION.md`](ORCHESTRATION.md).

## Legend

| Tag | Meaning |
|-----|---------|
| `MIRROR` | Required Rust face/body under `upy/` |
| `REWRITE` | Was pure Python — rewrite in Rust (no VM-core `.py`) |
| `DEAD` | GC / upy threads / upy scheduler / gchelper — gone |
| `OMIT_ARCH` | Non-x86_64 asm/nlr/emit/semihost |
| `OMIT_HW` | HW/bluetooth/upy-lwip/upy-fat-lfs — Metal owns devices/FS/net |
| `TOOL` | Build-time only |
| `SKIP_VENDOR` | Nested third-party trees — do not mirror |
| `SHARED_OPT` | Pull only when a MIRROR row needs it |

## Metal edge (not upstream)

```text
src/pymergetic/metal/py/
  .pm/{module,Cargo.toml,build.rs,smoke.rs}
  __init__.rs
  loop.rs
  async_bridge.rs
  step.rs
  bind.rs
  handle.rs
  alloc.rs
  gc_off.rs
  libc_policy.rs
  shell.rs
  port/
    mpconfigport.h
    mphalport.h
    mphalport.c
    stubs.c
  upy/                    # inventory tables below
```

## `upy/py/` — every upstream `.h` → `.rs` face

| Upstream | Rust face | Tag |
|----------|---------|-----|
| `py/asmarm.h` | `upy/py/asmarm.rs` | `OMIT_ARCH` |
| `py/asmbase.h` | `upy/py/asmbase.rs` | `MIRROR` |
| `py/asmrv32.h` | `upy/py/asmrv32.rs` | `OMIT_ARCH` |
| `py/asmthumb.h` | `upy/py/asmthumb.rs` | `OMIT_ARCH` |
| `py/asmx64.h` | `upy/py/asmx64.rs` | `MIRROR` |
| `py/asmx86.h` | `upy/py/asmx86.rs` | `OMIT_ARCH` |
| `py/asmxtensa.h` | `upy/py/asmxtensa.rs` | `OMIT_ARCH` |
| `py/bc.h` | `upy/py/bc.rs` | `MIRROR` |
| `py/bc0.h` | `upy/py/bc0.rs` | `MIRROR` |
| `py/binary.h` | `upy/py/binary.rs` | `MIRROR` |
| `py/builtin.h` | `upy/py/builtin.rs` | `MIRROR` |
| `py/compile.h` | `upy/py/compile.rs` | `MIRROR` |
| `py/cstack.h` | `upy/py/cstack.rs` | `MIRROR` |
| `py/dynruntime.h` | `upy/py/dynruntime.rs` | `MIRROR` |
| `py/emit.h` | `upy/py/emit.rs` | `MIRROR` |
| `py/emitglue.h` | `upy/py/emitglue.rs` | `MIRROR` |
| `py/formatfloat.h` | `upy/py/formatfloat.rs` | `MIRROR` |
| `py/frozenmod.h` | `upy/py/frozenmod.rs` | `MIRROR` |
| `py/gc.h` | `upy/py/gc.rs` | `DEAD` |
| `py/grammar.h` | `upy/py/grammar.rs` | `MIRROR` |
| `py/lexer.h` | `upy/py/lexer.rs` | `MIRROR` |
| `py/misc.h` | `upy/py/misc.rs` | `MIRROR` |
| `py/mpconfig.h` | `upy/py/mpconfig.rs` | `MIRROR` |
| `py/mperrno.h` | `upy/py/mperrno.rs` | `MIRROR` |
| `py/mphal.h` | `upy/py/mphal.rs` | `MIRROR` |
| `py/mpprint.h` | `upy/py/mpprint.rs` | `MIRROR` |
| `py/mpstate.h` | `upy/py/mpstate.rs` | `MIRROR` |
| `py/mpthread.h` | `upy/py/mpthread.rs` | `DEAD` |
| `py/mpz.h` | `upy/py/mpz.rs` | `MIRROR` |
| `py/nativeglue.h` | `upy/py/nativeglue.rs` | `MIRROR` |
| `py/nlr.h` | `upy/py/nlr.rs` | `MIRROR` |
| `py/obj.h` | `upy/py/obj.rs` | `MIRROR` |
| `py/objarray.h` | `upy/py/objarray.rs` | `MIRROR` |
| `py/objcode.h` | `upy/py/objcode.rs` | `MIRROR` |
| `py/objexcept.h` | `upy/py/objexcept.rs` | `MIRROR` |
| `py/objfun.h` | `upy/py/objfun.rs` | `MIRROR` |
| `py/objgenerator.h` | `upy/py/objgenerator.rs` | `MIRROR` |
| `py/objint.h` | `upy/py/objint.rs` | `MIRROR` |
| `py/objint_impl.h` | `upy/py/objint_impl.rs` | `MIRROR` |
| `py/objlist.h` | `upy/py/objlist.rs` | `MIRROR` |
| `py/objmodule.h` | `upy/py/objmodule.rs` | `MIRROR` |
| `py/objnamedtuple.h` | `upy/py/objnamedtuple.rs` | `MIRROR` |
| `py/objstr.h` | `upy/py/objstr.rs` | `MIRROR` |
| `py/objstringio.h` | `upy/py/objstringio.rs` | `MIRROR` |
| `py/objtuple.h` | `upy/py/objtuple.rs` | `MIRROR` |
| `py/objtype.h` | `upy/py/objtype.rs` | `MIRROR` |
| `py/pairheap.h` | `upy/py/pairheap.rs` | `MIRROR` |
| `py/parse.h` | `upy/py/parse.rs` | `MIRROR` |
| `py/parsenum.h` | `upy/py/parsenum.rs` | `MIRROR` |
| `py/parsenumbase.h` | `upy/py/parsenumbase.rs` | `MIRROR` |
| `py/persistentcode.h` | `upy/py/persistentcode.rs` | `MIRROR` |
| `py/profile.h` | `upy/py/profile.rs` | `MIRROR` |
| `py/pystack.h` | `upy/py/pystack.rs` | `MIRROR` |
| `py/qstr.h` | `upy/py/qstr.rs` | `MIRROR` |
| `py/qstrdefs.h` | `upy/py/qstrdefs.rs` | `MIRROR` |
| `py/reader.h` | `upy/py/reader.rs` | `MIRROR` |
| `py/repl.h` | `upy/py/repl.rs` | `MIRROR` |
| `py/ringbuf.h` | `upy/py/ringbuf.rs` | `MIRROR` |
| `py/runtime.h` | `upy/py/runtime.rs` | `MIRROR` |
| `py/runtime0.h` | `upy/py/runtime0.rs` | `MIRROR` |
| `py/scope.h` | `upy/py/scope.rs` | `MIRROR` |
| `py/smallint.h` | `upy/py/smallint.rs` | `MIRROR` |
| `py/stackctrl.h` | `upy/py/stackctrl.rs` | `MIRROR` |
| `py/stream.h` | `upy/py/stream.rs` | `MIRROR` |
| `py/unicode.h` | `upy/py/unicode.rs` | `MIRROR` |
| `py/vmentrytable.h` | `upy/py/vmentrytable.rs` | `MIRROR` |

## `upy/py/` — every upstream `.c` → `.rs` body

| Upstream | Rust body | Tag |
|----------|---------|-----|
| `py/argcheck.c` | `upy/py/argcheck.rs` | `MIRROR` |
| `py/asmarm.c` | `upy/py/asmarm.rs` | `OMIT_ARCH` |
| `py/asmbase.c` | `upy/py/asmbase.rs` | `MIRROR` |
| `py/asmrv32.c` | `upy/py/asmrv32.rs` | `OMIT_ARCH` |
| `py/asmthumb.c` | `upy/py/asmthumb.rs` | `OMIT_ARCH` |
| `py/asmx64.c` | `upy/py/asmx64.rs` | `MIRROR` |
| `py/asmx86.c` | `upy/py/asmx86.rs` | `OMIT_ARCH` |
| `py/asmxtensa.c` | `upy/py/asmxtensa.rs` | `OMIT_ARCH` |
| `py/bc.c` | `upy/py/bc.rs` | `MIRROR` |
| `py/binary.c` | `upy/py/binary.rs` | `MIRROR` |
| `py/builtinevex.c` | `upy/py/builtin/builtinevex.rs` | `MIRROR` |
| `py/builtinhelp.c` | `upy/py/builtin/builtinhelp.rs` | `MIRROR` |
| `py/builtinimport.c` | `upy/py/builtin/builtinimport.rs` | `MIRROR` |
| `py/compile.c` | `upy/py/compile.rs` | `MIRROR` |
| `py/cstack.c` | `upy/py/cstack.rs` | `MIRROR` |
| `py/emitbc.c` | `upy/py/emitbc.rs` | `MIRROR` |
| `py/emitcommon.c` | `upy/py/emitcommon.rs` | `MIRROR` |
| `py/emitglue.c` | `upy/py/emitglue.rs` | `MIRROR` |
| `py/emitinlinerv32.c` | `upy/py/emitinlinerv32.rs` | `OMIT_ARCH` |
| `py/emitinlinethumb.c` | `upy/py/emitinlinethumb.rs` | `OMIT_ARCH` |
| `py/emitinlinextensa.c` | `upy/py/emitinlinextensa.rs` | `OMIT_ARCH` |
| `py/emitnarm.c` | `upy/py/emitnarm.rs` | `OMIT_ARCH` |
| `py/emitnative.c` | `upy/py/emitnative.rs` | `MIRROR` |
| `py/emitndebug.c` | `upy/py/emitndebug.rs` | `MIRROR` |
| `py/emitnrv32.c` | `upy/py/emitnrv32.rs` | `OMIT_ARCH` |
| `py/emitnthumb.c` | `upy/py/emitnthumb.rs` | `OMIT_ARCH` |
| `py/emitnx64.c` | `upy/py/emitnx64.rs` | `MIRROR` |
| `py/emitnx86.c` | `upy/py/emitnx86.rs` | `OMIT_ARCH` |
| `py/emitnxtensa.c` | `upy/py/emitnxtensa.rs` | `OMIT_ARCH` |
| `py/emitnxtensawin.c` | `upy/py/emitnxtensawin.rs` | `OMIT_ARCH` |
| `py/formatfloat.c` | `upy/py/formatfloat.rs` | `MIRROR` |
| `py/frozenmod.c` | `upy/py/frozenmod.rs` | `MIRROR` |
| `py/gc.c` | `upy/py/gc.rs` | `DEAD` |
| `py/lexer.c` | `upy/py/lexer.rs` | `MIRROR` |
| `py/malloc.c` | `upy/py/malloc.rs` | `MIRROR` |
| `py/map.c` | `upy/py/map.rs` | `MIRROR` |
| `py/modarray.c` | `upy/py/builtin/modarray.rs` | `MIRROR` |
| `py/modbuiltins.c` | `upy/py/builtin/modbuiltins.rs` | `MIRROR` |
| `py/modcmath.c` | `upy/py/builtin/modcmath.rs` | `MIRROR` |
| `py/modcollections.c` | `upy/py/builtin/modcollections.rs` | `MIRROR` |
| `py/moderrno.c` | `upy/py/builtin/moderrno.rs` | `MIRROR` |
| `py/modgc.c` | `upy/py/modgc.rs` | `DEAD` |
| `py/modio.c` | `upy/py/builtin/modio.rs` | `MIRROR` |
| `py/modmath.c` | `upy/py/builtin/modmath.rs` | `MIRROR` |
| `py/modmicropython.c` | `upy/py/builtin/modmicropython.rs` | `MIRROR` |
| `py/modstring.c` | `upy/py/builtin/modstring.rs` | `MIRROR` |
| `py/modstruct.c` | `upy/py/builtin/modstruct.rs` | `MIRROR` |
| `py/modsys.c` | `upy/py/builtin/modsys.rs` | `MIRROR` |
| `py/modthread.c` | `upy/py/modthread.rs` | `DEAD` |
| `py/modweakref.c` | `upy/py/builtin/modweakref.rs` | `MIRROR` |
| `py/mpprint.c` | `upy/py/mpprint.rs` | `MIRROR` |
| `py/mpstate.c` | `upy/py/mpstate.rs` | `MIRROR` |
| `py/mpz.c` | `upy/py/mpz.rs` | `MIRROR` |
| `py/nativeglue.c` | `upy/py/nativeglue.rs` | `MIRROR` |
| `py/nlr.c` | `upy/py/nlr.rs` | `MIRROR` |
| `py/nlraarch64.c` | `upy/py/nlraarch64.rs` | `OMIT_ARCH` |
| `py/nlrloong64.c` | `upy/py/nlrloong64.rs` | `OMIT_ARCH` |
| `py/nlrmips.c` | `upy/py/nlrmips.rs` | `OMIT_ARCH` |
| `py/nlrpowerpc.c` | `upy/py/nlrpowerpc.rs` | `OMIT_ARCH` |
| `py/nlrrv32.c` | `upy/py/nlrrv32.rs` | `OMIT_ARCH` |
| `py/nlrrv64.c` | `upy/py/nlrrv64.rs` | `OMIT_ARCH` |
| `py/nlrsetjmp.c` | `upy/py/nlrsetjmp.rs` | `MIRROR` |
| `py/nlrthumb.c` | `upy/py/nlrthumb.rs` | `OMIT_ARCH` |
| `py/nlrx64.c` | `upy/py/nlrx64.rs` | `MIRROR` |
| `py/nlrx86.c` | `upy/py/nlrx86.rs` | `OMIT_ARCH` |
| `py/nlrxtensa.c` | `upy/py/nlrxtensa.rs` | `OMIT_ARCH` |
| `py/obj.c` | `upy/py/objects/obj.rs` | `MIRROR` |
| `py/objarray.c` | `upy/py/objects/objarray.rs` | `MIRROR` |
| `py/objattrtuple.c` | `upy/py/objects/objattrtuple.rs` | `MIRROR` |
| `py/objbool.c` | `upy/py/objects/objbool.rs` | `MIRROR` |
| `py/objboundmeth.c` | `upy/py/objects/objboundmeth.rs` | `MIRROR` |
| `py/objcell.c` | `upy/py/objects/objcell.rs` | `MIRROR` |
| `py/objclosure.c` | `upy/py/objects/objclosure.rs` | `MIRROR` |
| `py/objcode.c` | `upy/py/objects/objcode.rs` | `MIRROR` |
| `py/objcomplex.c` | `upy/py/objects/objcomplex.rs` | `MIRROR` |
| `py/objdeque.c` | `upy/py/objects/objdeque.rs` | `MIRROR` |
| `py/objdict.c` | `upy/py/objects/objdict.rs` | `MIRROR` |
| `py/objenumerate.c` | `upy/py/objects/objenumerate.rs` | `MIRROR` |
| `py/objexcept.c` | `upy/py/objects/objexcept.rs` | `MIRROR` |
| `py/objfilter.c` | `upy/py/objects/objfilter.rs` | `MIRROR` |
| `py/objfloat.c` | `upy/py/objects/objfloat.rs` | `MIRROR` |
| `py/objfun.c` | `upy/py/objects/objfun.rs` | `MIRROR` |
| `py/objgenerator.c` | `upy/py/objects/objgenerator.rs` | `MIRROR` |
| `py/objgetitemiter.c` | `upy/py/objects/objgetitemiter.rs` | `MIRROR` |
| `py/objint.c` | `upy/py/objects/objint.rs` | `MIRROR` |
| `py/objint_longlong.c` | `upy/py/objects/objint_longlong.rs` | `MIRROR` |
| `py/objint_mpz.c` | `upy/py/objects/objint_mpz.rs` | `MIRROR` |
| `py/objlist.c` | `upy/py/objects/objlist.rs` | `MIRROR` |
| `py/objmap.c` | `upy/py/objects/objmap.rs` | `MIRROR` |
| `py/objmodule.c` | `upy/py/objects/objmodule.rs` | `MIRROR` |
| `py/objnamedtuple.c` | `upy/py/objects/objnamedtuple.rs` | `MIRROR` |
| `py/objnone.c` | `upy/py/objects/objnone.rs` | `MIRROR` |
| `py/objobject.c` | `upy/py/objects/objobject.rs` | `MIRROR` |
| `py/objpolyiter.c` | `upy/py/objects/objpolyiter.rs` | `MIRROR` |
| `py/objproperty.c` | `upy/py/objects/objproperty.rs` | `MIRROR` |
| `py/objrange.c` | `upy/py/objects/objrange.rs` | `MIRROR` |
| `py/objreversed.c` | `upy/py/objects/objreversed.rs` | `MIRROR` |
| `py/objringio.c` | `upy/py/objects/objringio.rs` | `MIRROR` |
| `py/objset.c` | `upy/py/objects/objset.rs` | `MIRROR` |
| `py/objsingleton.c` | `upy/py/objects/objsingleton.rs` | `MIRROR` |
| `py/objslice.c` | `upy/py/objects/objslice.rs` | `MIRROR` |
| `py/objstr.c` | `upy/py/objects/objstr.rs` | `MIRROR` |
| `py/objstringio.c` | `upy/py/objects/objstringio.rs` | `MIRROR` |
| `py/objstrunicode.c` | `upy/py/objects/objstrunicode.rs` | `MIRROR` |
| `py/objtemplate.c` | `upy/py/objects/objtemplate.rs` | `MIRROR` |
| `py/objtuple.c` | `upy/py/objects/objtuple.rs` | `MIRROR` |
| `py/objtype.c` | `upy/py/objects/objtype.rs` | `MIRROR` |
| `py/objzip.c` | `upy/py/objects/objzip.rs` | `MIRROR` |
| `py/opmethods.c` | `upy/py/opmethods.rs` | `MIRROR` |
| `py/pairheap.c` | `upy/py/pairheap.rs` | `MIRROR` |
| `py/parse.c` | `upy/py/parse.rs` | `MIRROR` |
| `py/parsenum.c` | `upy/py/parsenum.rs` | `MIRROR` |
| `py/parsenumbase.c` | `upy/py/parsenumbase.rs` | `MIRROR` |
| `py/persistentcode.c` | `upy/py/persistentcode.rs` | `MIRROR` |
| `py/profile.c` | `upy/py/profile.rs` | `MIRROR` |
| `py/pystack.c` | `upy/py/pystack.rs` | `MIRROR` |
| `py/qstr.c` | `upy/py/qstr.rs` | `MIRROR` |
| `py/reader.c` | `upy/py/reader.rs` | `MIRROR` |
| `py/repl.c` | `upy/py/repl.rs` | `MIRROR` |
| `py/ringbuf.c` | `upy/py/ringbuf.rs` | `MIRROR` |
| `py/runtime.c` | `upy/py/runtime.rs` | `MIRROR` |
| `py/runtime_utils.c` | `upy/py/runtime_utils.rs` | `MIRROR` |
| `py/scheduler.c` | `upy/py/scheduler.rs` | `DEAD` |
| `py/scope.c` | `upy/py/scope.rs` | `MIRROR` |
| `py/sequence.c` | `upy/py/sequence.rs` | `MIRROR` |
| `py/showbc.c` | `upy/py/showbc.rs` | `MIRROR` |
| `py/smallint.c` | `upy/py/smallint.rs` | `MIRROR` |
| `py/stackctrl.c` | `upy/py/stackctrl.rs` | `MIRROR` |
| `py/stream.c` | `upy/py/stream.rs` | `MIRROR` |
| `py/unicode.c` | `upy/py/unicode.rs` | `MIRROR` |
| `py/vm.c` | `upy/py/vm.rs` | `MIRROR` |
| `py/vstr.c` | `upy/py/vstr.rs` | `MIRROR` |
| `py/warning.c` | `upy/py/warning.rs` | `MIRROR` |

## Upstream `py/*.py` build tools

| Upstream | Disposition | Tag |
|----------|-----------|-----|
| `py/make_root_pointers.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |
| `py/makecompresseddata.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |
| `py/makemoduledefs.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |
| `py/makeqstrdata.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |
| `py/makeqstrdefs.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |
| `py/makeversionhdr.py` | `forge py/.pm/build.rs + build/py/` | `TOOL` |

## `upy/extmod/` — headers

| Upstream | Rust face | Tag |
|----------|---------|-----|
| `extmod/cyw43_config_common.h` | `upy/extmod/cyw43_config_common.rs` | `OMIT_HW` |
| `extmod/font_petme128_8x8.h` | `upy/extmod/font_petme128_8x8.rs` | `OMIT_HW` |
| `extmod/machine_can.h` | `upy/extmod/machine_can.rs` | `OMIT_HW` |
| `extmod/machine_can_port.h` | `upy/extmod/machine_can_port.rs` | `OMIT_HW` |
| `extmod/misc.h` | `upy/extmod/misc.rs` | `MIRROR` |
| `extmod/modbluetooth.h` | `upy/extmod/modbluetooth.rs` | `OMIT_HW` |
| `extmod/modmachine.h` | `upy/extmod/modmachine.rs` | `OMIT_HW` |
| `extmod/modnetwork.h` | `upy/extmod/modnetwork.rs` | `OMIT_HW` |
| `extmod/modopenamp.h` | `upy/extmod/modopenamp.rs` | `OMIT_HW` |
| `extmod/modopenamp_remoteproc.h` | `upy/extmod/modopenamp_remoteproc.rs` | `OMIT_HW` |
| `extmod/modplatform.h` | `upy/extmod/modplatform.rs` | `MIRROR` |
| `extmod/modtime.h` | `upy/extmod/modtime.rs` | `MIRROR` |
| `extmod/modwebsocket.h` | `upy/extmod/modwebsocket.rs` | `OMIT_HW` |
| `extmod/mpbthci.h` | `upy/extmod/mpbthci.rs` | `OMIT_HW` |
| `extmod/network_cyw43.h` | `upy/extmod/network_cyw43.rs` | `OMIT_HW` |
| `extmod/vfs.h` | `upy/extmod/vfs.rs` | `MIRROR` |
| `extmod/vfs_fat.h` | `upy/extmod/vfs_fat.rs` | `OMIT_HW` |
| `extmod/vfs_lfs.h` | `upy/extmod/vfs_lfs.rs` | `OMIT_HW` |
| `extmod/vfs_posix.h` | `upy/extmod/vfs_posix.rs` | `OMIT_HW` |
| `extmod/vfs_rom.h` | `upy/extmod/vfs_rom.rs` | `OMIT_HW` |
| `extmod/virtpin.h` | `upy/extmod/virtpin.rs` | `OMIT_HW` |

## `upy/extmod/` — bodies

| Upstream | Rust body | Tag |
|----------|---------|-----|
| `extmod/machine_adc.c` | `upy/extmod/machine_adc.rs` | `OMIT_HW` |
| `extmod/machine_adc_block.c` | `upy/extmod/machine_adc_block.rs` | `OMIT_HW` |
| `extmod/machine_bitstream.c` | `upy/extmod/machine_bitstream.rs` | `OMIT_HW` |
| `extmod/machine_can.c` | `upy/extmod/machine_can.rs` | `OMIT_HW` |
| `extmod/machine_i2c.c` | `upy/extmod/machine_i2c.rs` | `OMIT_HW` |
| `extmod/machine_i2c_target.c` | `upy/extmod/machine_i2c_target.rs` | `OMIT_HW` |
| `extmod/machine_i2s.c` | `upy/extmod/machine_i2s.rs` | `OMIT_HW` |
| `extmod/machine_mem.c` | `upy/extmod/machine_mem.rs` | `OMIT_HW` |
| `extmod/machine_pinbase.c` | `upy/extmod/machine_pinbase.rs` | `OMIT_HW` |
| `extmod/machine_pulse.c` | `upy/extmod/machine_pulse.rs` | `OMIT_HW` |
| `extmod/machine_pwm.c` | `upy/extmod/machine_pwm.rs` | `OMIT_HW` |
| `extmod/machine_signal.c` | `upy/extmod/machine_signal.rs` | `OMIT_HW` |
| `extmod/machine_spi.c` | `upy/extmod/machine_spi.rs` | `OMIT_HW` |
| `extmod/machine_timer.c` | `upy/extmod/machine_timer.rs` | `OMIT_HW` |
| `extmod/machine_uart.c` | `upy/extmod/machine_uart.rs` | `OMIT_HW` |
| `extmod/machine_usb_device.c` | `upy/extmod/machine_usb_device.rs` | `OMIT_HW` |
| `extmod/machine_wdt.c` | `upy/extmod/machine_wdt.rs` | `OMIT_HW` |
| `extmod/modasyncio.c` | `upy/extmod/modasyncio.rs` | `MIRROR` |
| `extmod/modbinascii.c` | `upy/extmod/modbinascii.rs` | `MIRROR` |
| `extmod/modbluetooth.c` | `upy/extmod/modbluetooth.rs` | `OMIT_HW` |
| `extmod/modbtree.c` | `upy/extmod/modbtree.rs` | `OMIT_HW` |
| `extmod/modcryptolib.c` | `upy/extmod/modcryptolib.rs` | `MIRROR` |
| `extmod/moddeflate.c` | `upy/extmod/moddeflate.rs` | `MIRROR` |
| `extmod/modframebuf.c` | `upy/extmod/modframebuf.rs` | `OMIT_HW` |
| `extmod/modhashlib.c` | `upy/extmod/modhashlib.rs` | `MIRROR` |
| `extmod/modheapq.c` | `upy/extmod/modheapq.rs` | `MIRROR` |
| `extmod/modjson.c` | `upy/extmod/modjson.rs` | `MIRROR` |
| `extmod/modlwip.c` | `upy/extmod/modlwip.rs` | `OMIT_HW` |
| `extmod/modmachine.c` | `upy/extmod/modmachine.rs` | `OMIT_HW` |
| `extmod/modmarshal.c` | `upy/extmod/modmarshal.rs` | `MIRROR` |
| `extmod/modnetwork.c` | `upy/extmod/modnetwork.rs` | `OMIT_HW` |
| `extmod/modonewire.c` | `upy/extmod/modonewire.rs` | `OMIT_HW` |
| `extmod/modopenamp.c` | `upy/extmod/modopenamp.rs` | `OMIT_HW` |
| `extmod/modopenamp_remoteproc.c` | `upy/extmod/modopenamp_remoteproc.rs` | `OMIT_HW` |
| `extmod/modopenamp_remoteproc_store.c` | `upy/extmod/modopenamp_remoteproc_store.rs` | `OMIT_HW` |
| `extmod/modos.c` | `upy/extmod/modos.rs` | `MIRROR` |
| `extmod/modplatform.c` | `upy/extmod/modplatform.rs` | `MIRROR` |
| `extmod/modrandom.c` | `upy/extmod/modrandom.rs` | `MIRROR` |
| `extmod/modre.c` | `upy/extmod/modre.rs` | `MIRROR` |
| `extmod/modselect.c` | `upy/extmod/modselect.rs` | `MIRROR` |
| `extmod/modsocket.c` | `upy/extmod/modsocket.rs` | `MIRROR` |
| `extmod/modtime.c` | `upy/extmod/modtime.rs` | `MIRROR` |
| `extmod/modtls_axtls.c` | `upy/extmod/modtls_axtls.rs` | `OMIT_HW` |
| `extmod/modtls_mbedtls.c` | `upy/extmod/modtls_mbedtls.rs` | `OMIT_HW` |
| `extmod/moductypes.c` | `upy/extmod/moductypes.rs` | `MIRROR` |
| `extmod/modvfs.c` | `upy/extmod/modvfs.rs` | `MIRROR` |
| `extmod/modwebrepl.c` | `upy/extmod/modwebrepl.rs` | `OMIT_HW` |
| `extmod/modwebsocket.c` | `upy/extmod/modwebsocket.rs` | `OMIT_HW` |
| `extmod/mpbthci.c` | `upy/extmod/mpbthci.rs` | `OMIT_HW` |
| `extmod/network_cyw43.c` | `upy/extmod/network_cyw43.rs` | `OMIT_HW` |
| `extmod/network_esp_hosted.c` | `upy/extmod/network_esp_hosted.rs` | `OMIT_HW` |
| `extmod/network_lwip.c` | `upy/extmod/network_lwip.rs` | `OMIT_HW` |
| `extmod/network_ninaw10.c` | `upy/extmod/network_ninaw10.rs` | `OMIT_HW` |
| `extmod/network_ppp_lwip.c` | `upy/extmod/network_ppp_lwip.rs` | `OMIT_HW` |
| `extmod/network_wiznet5k.c` | `upy/extmod/network_wiznet5k.rs` | `OMIT_HW` |
| `extmod/os_dupterm.c` | `upy/extmod/os_dupterm.rs` | `OMIT_HW` |
| `extmod/vfs.c` | `upy/extmod/vfs.rs` | `MIRROR` |
| `extmod/vfs_blockdev.c` | `upy/extmod/vfs_blockdev.rs` | `MIRROR` |
| `extmod/vfs_fat.c` | `upy/extmod/vfs_fat.rs` | `OMIT_HW` |
| `extmod/vfs_fat_diskio.c` | `upy/extmod/vfs_fat_diskio.rs` | `OMIT_HW` |
| `extmod/vfs_fat_file.c` | `upy/extmod/vfs_fat_file.rs` | `OMIT_HW` |
| `extmod/vfs_lfs.c` | `upy/extmod/vfs_lfs.rs` | `OMIT_HW` |
| `extmod/vfs_lfsx.c` | `upy/extmod/vfs_lfsx.rs` | `OMIT_HW` |
| `extmod/vfs_lfsx_file.c` | `upy/extmod/vfs_lfsx_file.rs` | `OMIT_HW` |
| `extmod/vfs_posix.c` | `upy/extmod/vfs_posix.rs` | `OMIT_HW` |
| `extmod/vfs_posix_file.c` | `upy/extmod/vfs_posix_file.rs` | `OMIT_HW` |
| `extmod/vfs_reader.c` | `upy/extmod/vfs_reader.rs` | `MIRROR` |
| `extmod/vfs_rom.c` | `upy/extmod/vfs_rom.rs` | `OMIT_HW` |
| `extmod/vfs_rom_file.c` | `upy/extmod/vfs_rom_file.rs` | `OMIT_HW` |
| `extmod/virtpin.c` | `upy/extmod/virtpin.rs` | `OMIT_HW` |

## `upy/extmod/asyncio/` — pure Python → Rust

| Upstream | Rust body | Tag |
|----------|---------|-----|
| `extmod/asyncio/__init__.py` | `upy/extmod/asyncio/__init__.rs` | `REWRITE` |
| `extmod/asyncio/core.py` | `upy/extmod/asyncio/core.rs` | `REWRITE` |
| `extmod/asyncio/event.py` | `upy/extmod/asyncio/event.rs` | `REWRITE` |
| `extmod/asyncio/funcs.py` | `upy/extmod/asyncio/funcs.rs` | `REWRITE` |
| `extmod/asyncio/lock.py` | `upy/extmod/asyncio/lock.rs` | `REWRITE` |
| `extmod/asyncio/manifest.py` | `—` | `TOOL` |
| `extmod/asyncio/stream.py` | `upy/extmod/asyncio/stream.rs` | `REWRITE` |
| `extmod/asyncio/task.py` | `upy/extmod/asyncio/task.rs` | `REWRITE` |
| `extmod/asyncio/uasyncio.py` | `upy/extmod/asyncio/uasyncio.rs` | `REWRITE` |

## `upy/extmod/` vendor dirs

| Upstream | Disposition | Tag |
|----------|-----------|-----|
| `extmod/axtls-include/` | `—` | `SKIP_VENDOR` |
| `extmod/berkeley-db/` | `—` | `SKIP_VENDOR` |
| `extmod/btstack/` | `—` | `SKIP_VENDOR` |
| `extmod/libmetal/` | `—` | `SKIP_VENDOR` |
| `extmod/littlefs-include/` | `—` | `SKIP_VENDOR` |
| `extmod/lwip-include/` | `—` | `SKIP_VENDOR` |
| `extmod/mbedtls/` | `—` | `SKIP_VENDOR` |
| `extmod/nimble/` | `—` | `SKIP_VENDOR` |

## `upy/shared/`

| Upstream | Rust | Tag |
|----------|----|-----|
| `shared/README.md` | `—` | `TOOL` |
| `shared/libc/__errno.c` | `upy/shared/libc/__errno.rs` | `SHARED_OPT` |
| `shared/libc/abort_.c` | `upy/shared/libc/abort_.rs` | `SHARED_OPT` |
| `shared/libc/printf.c` | `upy/shared/libc/printf.rs` | `SHARED_OPT` |
| `shared/libc/string0.c` | `upy/shared/libc/string0.rs` | `SHARED_OPT` |
| `shared/memzip/README.md` | `—` | `TOOL` |
| `shared/memzip/import.c` | `—` | `OMIT_HW` |
| `shared/memzip/lexermemzip.c` | `—` | `OMIT_HW` |
| `shared/memzip/make-memzip.py` | `—` | `TOOL` |
| `shared/memzip/memzip.c` | `—` | `OMIT_HW` |
| `shared/memzip/memzip.h` | `—` | `OMIT_HW` |
| `shared/netutils/dhcpserver.c` | `—` | `OMIT_HW` |
| `shared/netutils/dhcpserver.h` | `—` | `OMIT_HW` |
| `shared/netutils/netutils.c` | `—` | `OMIT_HW` |
| `shared/netutils/netutils.h` | `—` | `OMIT_HW` |
| `shared/netutils/trace.c` | `—` | `OMIT_HW` |
| `shared/readline/readline.c` | `upy/shared/readline/readline.rs` | `SHARED_OPT` |
| `shared/readline/readline.h` | `upy/shared/readline/readline.rs` | `SHARED_OPT` |
| `shared/runtime/gchelper.h` | `upy/shared/runtime/gchelper.rs` | `DEAD` |
| `shared/runtime/gchelper_generic.c` | `upy/shared/runtime/gchelper_generic.rs` | `DEAD` |
| `shared/runtime/gchelper_loong64.s` | `—` | `OMIT_ARCH` |
| `shared/runtime/gchelper_native.c` | `upy/shared/runtime/gchelper_native.rs` | `DEAD` |
| `shared/runtime/gchelper_rv32i.s` | `—` | `OMIT_ARCH` |
| `shared/runtime/gchelper_rv64i.s` | `—` | `OMIT_ARCH` |
| `shared/runtime/gchelper_thumb1.s` | `—` | `OMIT_ARCH` |
| `shared/runtime/gchelper_thumb2.s` | `—` | `OMIT_ARCH` |
| `shared/runtime/interrupt_char.c` | `upy/shared/runtime/interrupt_char.rs` | `SHARED_OPT` |
| `shared/runtime/interrupt_char.h` | `upy/shared/runtime/interrupt_char.rs` | `SHARED_OPT` |
| `shared/runtime/mpirq.c` | `upy/shared/runtime/mpirq.rs` | `SHARED_OPT` |
| `shared/runtime/mpirq.h` | `upy/shared/runtime/mpirq.rs` | `SHARED_OPT` |
| `shared/runtime/pyexec.c` | `upy/shared/runtime/pyexec.rs` | `SHARED_OPT` |
| `shared/runtime/pyexec.h` | `upy/shared/runtime/pyexec.rs` | `SHARED_OPT` |
| `shared/runtime/semihosting_arm.c` | `—` | `OMIT_ARCH` |
| `shared/runtime/semihosting_arm.h` | `—` | `OMIT_ARCH` |
| `shared/runtime/semihosting_rv32.c` | `—` | `OMIT_ARCH` |
| `shared/runtime/semihosting_rv32.h` | `—` | `OMIT_ARCH` |
| `shared/runtime/softtimer.c` | `upy/shared/runtime/softtimer.rs` | `SHARED_OPT` |
| `shared/runtime/softtimer.h` | `upy/shared/runtime/softtimer.rs` | `SHARED_OPT` |
| `shared/runtime/stdout_helpers.c` | `upy/shared/runtime/stdout_helpers.rs` | `SHARED_OPT` |
| `shared/runtime/sys_stdio_mphal.c` | `upy/shared/runtime/sys_stdio_mphal.rs` | `SHARED_OPT` |
| `shared/timeutils/timeutils.c` | `upy/shared/timeutils/timeutils.rs` | `SHARED_OPT` |
| `shared/timeutils/timeutils.h` | `upy/shared/timeutils/timeutils.rs` | `SHARED_OPT` |
| `shared/tinyusb` | `—` | `SKIP_VENDOR` |

## Counts

| Tag | Rows |
|-----|------|
| `MIRROR` | 189 |
| `OMIT_HW` | 76 |
| `OMIT_ARCH` | 37 |
| `SHARED_OPT` | 18 |
| `TOOL` | 10 |
| `DEAD` | 9 |
| `SKIP_VENDOR` | 9 |
| `REWRITE` | 8 |

Total inventory rows: **356**.

## Completeness gate

1. Every `MIRROR` / `REWRITE` row that you claim done has finished `.rs` logic.
2. No core builtin remains only as upstream `.c` or `.py`.
3. `./forge-cli mod check` + `build/run bios` + `build/run efi`.
4. No Metal Rust `#include` of `external/micropython/py/*.h` once the face exists.
5. Diff this file against `external/micropython/{py,extmod,shared}` after vendor — no silent new upstream files.

