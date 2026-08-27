/* Prefix all TCC backend symbols to avoid conflicts with the native backend.
 * Include this before wasm32-gen.c and wasm32-link.c when both backends
 * are linked into the same binary. */

#define g              wasm32_g
#define o              wasm32_o
#define gen_le16       wasm32_gen_le16
#define gen_le32       wasm32_gen_le32
#define gen_le64       wasm32_gen_le64
#define load           wasm32_load
#define store          wasm32_store
#define gfunc_sret     wasm32_gfunc_sret
#define gfunc_call     wasm32_gfunc_call
#define gfunc_prolog   wasm32_gfunc_prolog
#define gfunc_epilog   wasm32_gfunc_epilog
#define gjmp           wasm32_gjmp
#define gjmp_addr      wasm32_gjmp_addr
#define gjmp_cond      wasm32_gjmp_cond
#define gjmp_append    wasm32_gjmp_append
#define ggoto          wasm32_ggoto
#define gen_opi        wasm32_gen_opi
#define gen_opf        wasm32_gen_opf
#define gen_opl        wasm32_gen_opl
#define gen_cvt_itof   wasm32_gen_cvt_itof
#define gen_cvt_ftoi   wasm32_gen_cvt_ftoi
#define gen_cvt_ftof   wasm32_gen_cvt_ftof
#define gen_cvt_sxtw   wasm32_gen_cvt_sxtw
#define gen_cvt_csti   wasm32_gen_cvt_csti
#define gen_addr32     wasm32_gen_addr32
#define gen_addrpc32    wasm32_gen_addrpc32
#define gen_fill_nops  wasm32_gen_fill_nops
#define gen_increment_tcov wasm32_gen_increment_tcov
#define gen_va_start   wasm32_gen_va_start
#define gen_va_arg     wasm32_gen_va_arg
#define gen_clear_cache wasm32_gen_clear_cache
#define gen_vla_sp_save    wasm32_gen_vla_sp_save
#define gen_vla_sp_restore wasm32_gen_vla_sp_restore
#define gen_vla_alloc      wasm32_gen_vla_alloc
#define gen_vla_result     wasm32_gen_vla_result
#define reg_classes    wasm32_reg_classes
#define target_machine_defs wasm32_target_machine_defs