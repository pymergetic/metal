/* mrustc in-process embed — reproduces the mrustc CLI driver (src/main.cpp)
 * without its arg parsing, `exit()`, or subprocess calls. The CLI driver is
 * excluded from bin/mrustc.a (only main.o is filtered out), so this shim owns
 * the parts that live in main.o:
 *
 *   - gTargetVersion (extern, defined only in main.cpp)
 *   - debug phase registration (init_debug_list)
 *
 * and then runs the same pipeline main() runs, up to and including Trans_Codegen
 * with CodegenOutput::Executable. TransOptions::build_command_file is set to a
 * non-empty sentinel, which makes CodeGenerator_C write <out>.c and skip the
 * `system("gcc ...")` step — the generated C is returned to the caller for the
 * TCC/WASM stage. No subprocess is spawned.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <utility>
#include <unistd.h>

#include <version.hpp>
#include <string_view.hpp>
#include <target_detect.h>
#include <debug_inner.hpp>
#include <memory_dump.hpp>

#include "parse/lex.hpp"
#include "parse/parseerror.hpp"
#include "parse/common.hpp"
#include "ast/ast.hpp"
#include "ast/crate.hpp"
#include <main_bindings.hpp>
#include "resolve/main_bindings.hpp"
#include "hir/main_bindings.hpp"
#include "hir_conv/main_bindings.hpp"
#include "hir_typeck/main_bindings.hpp"
#include "hir_expand/main_bindings.hpp"
#include "mir/main_bindings.hpp"
#include "trans/main_bindings.hpp"
#include "trans/target.hpp"
#include "expand/cfg.hpp"

#include "mrustc_embed.h"

TargetVersion	gTargetVersion = TargetVersion::Rustc1_90;

// Keep the phase list in lock-step with the driver's init_debug_list() so
// MRUSTC_DEBUG filtering behaves identically.
static void init_debug_list()
{
    debug_init_phases("MRUSTC_DEBUG", {
        "Target Load", "Parse", "LoadCrates", "Expand", "Dump Expanded",
        "Implicit Crates", "Resolve Use", "Resolve Index", "Resolve Absolute",
        "HIR Lower", "Lifetime Elision", "Resolve Type Aliases", "Resolve Bind",
        "Resolve UFCS Outer", "Resolve UFCS paths", "Resolve HIR Self Type",
        "Resolve HIR Markings", "Sort Impls", "Constant Evaluate",
        "Typecheck Outer", "Typecheck Expressions", "Expand HIR Annotate",
        "Expand HIR Static Borrow Mark", "Expand HIR Lifetimes",
        "Expand HIR Closures", "Expand HIR Static Borrow", "Expand HIR Calls",
        "Expand HIR VTables", "Expand HIR Reborrows", "Expand HIR ErasedType",
        "Typecheck Expressions (validate)", "Expand HIR Lifetimes (validate)",
        "Dump HIR", "Lower MIR", "MIR Validate", "MIR Validate Full Early",
        "Dump MIR", "Constant Evaluate Full", "MIR Cleanup", "MIR Borrowcheck",
        "MIR Optimise", "MIR Validate PO", "MIR Validate Full", "HIR Serialise",
        "Trans Enumerate", "Trans Auto Impls", "Trans Monomorph",
        "MIR Optimise Inline", "MIR Cleanup 2", "MIR Optimise Inline PostSave",
        "Trans Enumerate Cleanup", "Trans Codegen"
        });
}

namespace {
struct CoutRedirect {
    std::ostringstream sink;
    std::streambuf *saved;
    CoutRedirect() : saved(std::cout.rdbuf(sink.rdbuf())) {}
    ~CoutRedirect() { std::cout.rdbuf(saved); }
};
}

int pm_metal_jit_rs_mrustc_compile(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len)
{
    if (!rs_source || rs_len == 0 || !c_out || !c_out_len || c_out_cap == 0) {
        return -1;
    }

    char tmpl[] = "/tmp/.jit_rs_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmpl); return -1; }
    size_t w = fwrite(rs_source, 1, rs_len, f);
    fclose(f);
    if (w != rs_len) { unlink(tmpl); return -1; }

    std::string rs_file = tmpl;
    std::string outbase = rs_file + ".out";

    int rc = -1;
    try
    {
        // std::cout is redirected for the duration of the compile so the
        // pass/DEBUG chatter doesn't hit the host's stdout. stderr is NOT
        // redirected: real diagnostics still reach the caller.
        CoutRedirect redirect;

        // A compile error must throw, not abort() the host process.
        g_span_throw_on_error = true;

        init_debug_list();

        Cfg_SetValue("rust_compiler", "mrustc");
        Target_SetCfg(DEFAULT_TARGET_NAME);
        Expand_Init();

        // Populate the crate search path from $MRUSTC_LIBDIR (the dir holding
        // the pre-built libcore/libstd HIR data), mirroring the CLI's
        // ProgramParams → g_crate_load_dirs plumbing.
        if (const char *libdir = getenv("MRUSTC_LIBDIR")) {
            AST::g_crate_load_dirs.push_back(libdir);
        }

        AST::Crate crate = Parse_Crate(rs_file, AST::Edition::Rust2015);

        AST::g_crate_overrides.clear();
        crate.load_externs();

        Expand(crate);

        // Force RustLib crate type — the JIT compiles functions, not binaries.
        // Trans_Enumerate_Public emits every public function without requiring
        // a main entrypoint. LOAD_STD (default) loads core, which is needed
        // for i32 etc. even in #![no_std] programs.
        crate.m_crate_type = AST::Crate::Type::RustLib;
        crate.set_crate_name("jit_rs");

        Resolve_Use(crate);
        Resolve_Index(crate);
        Resolve_Absolutise(crate);

        ::HIR::CratePtr hir_crate = LowerHIR_FromAST(std::move(crate));

        ConvertHIR_LifetimeElision(*hir_crate);
        ConvertHIR_ExpandAliases(*hir_crate);
        ConvertHIR_Bind(*hir_crate);
        ConvertHIR_ResolveUFCS_Outer(*hir_crate);
        ConvertHIR_ExpandAliases_Self(*hir_crate);
        ConvertHIR_Markings(*hir_crate);
        ConvertHIR_ResolveUFCS_SortImpls(*hir_crate);
        ConvertHIR_ResolveUFCS(*hir_crate);
        ConvertHIR_ConstantEvaluate(*hir_crate);

        Typecheck_ModuleLevel(*hir_crate);
        Typecheck_Expressions(*hir_crate);

        HIR_Expand_AnnotateUsage(*hir_crate);
        HIR_Expand_StaticBorrowConstants_Mark(*hir_crate);
        HIR_Expand_LifetimeInfer(*hir_crate);
        HIR_Expand_Closures(*hir_crate);
        HIR_Expand_StaticBorrowConstants(*hir_crate);
        HIR_Expand_VTables(*hir_crate);
        HIR_Expand_UfcsEverything(*hir_crate);
        HIR_Expand_Reborrows(*hir_crate);
        HIR_Expand_ErasedType(*hir_crate);
        Typecheck_Expressions_Validate(*hir_crate);

        HIR_GenerateMIR(*hir_crate);
        MIR_CheckCrate(*hir_crate);
        MIR_CleanupCrate(*hir_crate);
        MIR_OptimiseCrate(*hir_crate, false);
        MIR_CheckCrate(*hir_crate);

        TransOptions trans_opt;
        trans_opt.mode = "c";
        trans_opt.build_command_file = outbase + ".cc.cmd";
        trans_opt.opt_level = 0;
        trans_opt.panic_crate = "panic_abort";
        trans_opt.emit_debug_info = false;

        TransList items = Trans_Enumerate_Public(*hir_crate);
        Trans_AutoImpls(*hir_crate, items);
        Trans_Monomorphise_List(*hir_crate, items);
        MIR_OptimiseCrate_Inlining(*hir_crate, items, false);
        MIR_Cleanup_SetPostMonomorph();
        MIR_CleanupCrate(*hir_crate);
        MIR_OptimiseCrate_Inlining(*hir_crate, items, true);
        Trans_Enumerate_Cleanup(*hir_crate, items);

        Trans_Codegen(outbase, CodegenOutput::StaticLibrary, trans_opt,
            std::move(hir_crate), std::move(items), "");

        std::string cfile = outbase + ".c";
        FILE *cf = fopen(cfile.c_str(), "rb");
        if (!cf) {
            rc = -1;
        } else {
            size_t n = fread(c_out, 1, c_out_cap - 1, cf);
            fclose(cf);
            c_out[n] = '\0';
            *c_out_len = n;
            rc = (n > 0) ? 0 : -1;
        }
        unlink(cfile.c_str());
        unlink((outbase + ".cc.cmd").c_str());
    }
    catch (const std::exception&) {
        rc = -1;
    }
    catch (const char*) {
        rc = -1;
    }
    catch (unsigned int) {
        rc = -1;
    }
    catch (...) {
        rc = -1;
    }

    unlink(rs_file.c_str());
    return rc;
}
