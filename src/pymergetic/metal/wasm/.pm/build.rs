//! Build WAMR: host via cmake; freestanding via cc (interp + Metal platform).
//! External/wamr stays vanilla — no in-tree patches.
use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join(".."); // .../wasm
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let metal_dir = mod_dir.join("..").canonicalize().expect("metal dir");
    let package_root = metal_dir
        .join("../../..")
        .canonicalize()
        .expect("package root");
    let wamr = package_root.join("external/wamr");
    let port = mod_dir.join("port");
    let plat = port.join("platform");
    let libc = metal_dir.join("libc");
    let src_root = package_root.join("src");

    println!("cargo:rerun-if-changed={}", port.join("runtime.h").display());
    println!(
        "cargo:rerun-if-changed={}",
        port.join("runtime_host.c").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        plat.join("metal_platform.c").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        plat.join("platform_internal.h").display()
    );

    if freestanding {
        if !wamr.join("core/iwasm/include/wasm_export.h").is_file() {
            panic!("external/wamr missing; run W5.1 vendor first");
        }
        build_freestanding_wamr(
            &wamr, &port, &plat, &libc, &src_root, &out_dir, uefi,
        );
        return;
    }

    if !wamr.join("core/iwasm/include/wasm_export.h").is_file() {
        panic!("external/wamr missing; run W5.1 vendor first");
    }

    let wamr_build = out_dir.join("wamr-build");
    let libiwasm = wamr_build.join("libiwasm.a");
    if !libiwasm.is_file() {
        build_wamr_host(&wamr, &wamr_build);
    }
    assert!(libiwasm.is_file(), "libiwasm.a missing after cmake");

    compile_c_host(
        &port.join("runtime_host.c"),
        &out_dir.join("runtime_host.o"),
        &[
            &port,
            &wamr.join("core/iwasm/include"),
            &src_root,
        ],
    );
    archive(
        &out_dir.join("libpm_metal_wasm_port.a"),
        &[&out_dir.join("runtime_host.o")],
    );

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=pm_metal_wasm_port");
    println!("cargo:rustc-link-search=native={}", wamr_build.display());
    println!("cargo:rustc-link-lib=static=iwasm");
    println!("cargo:rustc-link-lib=dylib=m");
    println!("cargo:rustc-link-lib=dylib=pthread");
    println!("cargo:rustc-link-lib=dylib=dl");
}

fn build_freestanding_wamr(
    wamr: &Path,
    port: &Path,
    plat: &Path,
    libc: &Path,
    src_root: &Path,
    _out_dir: &Path,
    uefi: bool,
) {
    let core = wamr.join("core");
    let iwasm = core.join("iwasm");
    let shared = core.join("shared");

    let mut sources: Vec<PathBuf> = Vec::new();
    sources.push(port.join("runtime_host.c"));
    sources.push(plat.join("metal_platform.c"));
    sources.push(shared.join("platform/common/math/math.c"));

    for name in [
        "ems/ems_alloc.c",
        "ems/ems_gc.c",
        "ems/ems_hmu.c",
        "ems/ems_kfc.c",
        "mem_alloc.c",
    ] {
        sources.push(shared.join("mem-alloc").join(name));
    }

    for name in [
        "bh_assert.c",
        "bh_bitmap.c",
        "bh_common.c",
        "bh_hashmap.c",
        "bh_leb128.c",
        "bh_list.c",
        "bh_log.c",
        "bh_queue.c",
        "bh_vector.c",
        "runtime_timer.c",
    ] {
        sources.push(shared.join("utils").join(name));
    }

    for name in [
        "wasm_blocking_op.c",
        "wasm_c_api.c",
        "wasm_exec_env.c",
        "wasm_loader_common.c",
        "wasm_memory.c",
        "wasm_native.c",
        "wasm_runtime_common.c",
        "wasm_shared_memory.c",
    ] {
        sources.push(iwasm.join("common").join(name));
    }
    if uefi {
        sources.push(iwasm.join("common/arch/invokeNative_mingw_x64.s"));
    } else {
        sources.push(iwasm.join("common/arch/invokeNative_em64.s"));
    }

    for name in ["wasm_interp_fast.c", "wasm_loader.c", "wasm_runtime.c"] {
        sources.push(iwasm.join("interpreter").join(name));
    }

    let mut build = cc::Build::new();
    build.warnings(false);
    build.compiler("clang");
    build.flag("-std=c11");
    build.flag("-O2");
    build.flag("-fno-strict-aliasing");
    build.flag("-fno-stack-protector");
    build.flag("-ffreestanding");
    build.flag("-nostdinc");
    build.flag("-Wno-unused-parameter");
    build.flag("-Wno-sign-compare");
    build.flag("-Wno-missing-field-initializers");
    build.flag("-Wno-format");

    /* Keep config.h vanilla — override via -D. */
    build.define("BH_PLATFORM_METAL", None);
    build.define("BUILD_TARGET_X86_64", None);
    build.define("WASM_ENABLE_INTERP", "1");
    build.define("WASM_ENABLE_FAST_INTERP", "1");
    build.define("WASM_ENABLE_AOT", "0");
    build.define("WASM_ENABLE_JIT", "0");
    build.define("WASM_ENABLE_FAST_JIT", "0");
    build.define("WASM_ENABLE_LIBC_BUILTIN", "0");
    build.define("WASM_ENABLE_LIBC_WASI", "0");
    build.define("WASM_ENABLE_SIMD", "0");
    build.define("WASM_ENABLE_MULTI_MODULE", "0");
    build.define("WASM_ENABLE_SHARED_MEMORY", "0");
    build.define("WASM_ENABLE_MINI_LOADER", "0");
    build.define("WASM_DISABLE_HW_BOUND_CHECK", "1");
    build.define("WASM_DISABLE_STACK_HW_BOUND_CHECK", "1");
    build.define("WASM_ENABLE_BULK_MEMORY", "1");
    build.define("BH_MALLOC", "wasm_runtime_malloc");
    build.define("BH_FREE", "wasm_runtime_free");
    /* Avoid host glibc masquerade in freestanding TUs. */
    build.flag("-U__linux__");
    build.flag("-Ulinux");
    build.flag("-U__gnu_linux__");

    build.include(plat);
    build.include(port);
    build.include(libc);
    build.include(src_root);
    build.include(iwasm.join("include"));
    build.include(iwasm.join("interpreter"));
    build.include(iwasm.join("common"));
    build.include(shared.join("platform/include"));
    build.include(shared.join("mem-alloc"));
    build.include(shared.join("utils"));
    build.include(shared.join("utils/uncommon"));
    build.include(&core);

    for s in &sources {
        build.file(s);
        println!("cargo:rerun-if-changed={}", s.display());
    }

    if uefi {
        build.flag("--target=x86_64-unknown-windows-gnu");
        build.flag("-fshort-wchar");
        build.flag("-mno-red-zone");
    } else {
        build.flag("--target=x86_64-unknown-none-elf");
    }

    build.compile("pm_metal_wasm_port");
}

fn build_wamr_host(wamr: &Path, build_dir: &Path) {
    std::fs::create_dir_all(build_dir).expect("wamr build dir");
    let src = wamr.join("product-mini/platforms/linux");
    let status = Command::new("cmake")
        .args([
            "-S",
            src.to_str().unwrap(),
            "-B",
            build_dir.to_str().unwrap(),
            "-DWAMR_BUILD_INTERP=1",
            "-DWAMR_BUILD_AOT=0",
            "-DWAMR_BUILD_JIT=0",
            "-DWAMR_BUILD_FAST_JIT=0",
            "-DWAMR_BUILD_LIBC_BUILTIN=1",
            "-DWAMR_BUILD_LIBC_WASI=0",
            "-DWAMR_BUILD_SIMD=0",
            "-DWAMR_BUILD_MULTI_MODULE=0",
        ])
        .status()
        .expect("cmake configure wamr");
    assert!(status.success(), "cmake configure failed");
    let status = Command::new("cmake")
        .args([
            "--build",
            build_dir.to_str().unwrap(),
            "--target",
            "vmlib",
            "-j",
        ])
        .status()
        .expect("cmake build wamr");
    assert!(status.success(), "cmake build vmlib failed");
}

fn compile_c_host(src: &Path, obj: &Path, includes: &[&Path]) {
    let mut cmd = Command::new("clang");
    cmd.arg("-c").arg(src).arg("-o").arg(obj);
    cmd.args(["-std=c11", "-O2", "-fno-strict-aliasing", "-Wall"]);
    for inc in includes {
        cmd.arg(format!("-I{}", inc.display()));
    }
    println!("cargo:rerun-if-changed={}", src.display());
    let status = cmd.status().expect("clang");
    assert!(status.success(), "clang failed for {}", src.display());
}

fn archive(lib: &Path, objs: &[&Path]) {
    let _ = std::fs::remove_file(lib);
    let mut cmd = Command::new("ar");
    cmd.arg("crs").arg(lib);
    for o in objs {
        cmd.arg(o);
    }
    let status = cmd.status().expect("ar");
    assert!(status.success(), "ar failed");
}
