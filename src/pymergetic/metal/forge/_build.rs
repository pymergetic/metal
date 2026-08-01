//! forge build — declarative firmware units + thin host runner.
//!
//! Shape: tree paths → unit tables → one compile/link loop (bios/efi).

use alloc::string::String;
use alloc::vec::Vec;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::_host::{
    ensure_dir, ensure_file, push_flags, push_includes, run, sess_flag, sess_positional, which,
};
use crate::_port::{block_on, ForgeSession};

// ---------------------------------------------------------------------------
// Declarative units: (source relative to metal/, object stem)
// ---------------------------------------------------------------------------

type Unit = (&'static str, &'static str);

/// Disabled from the empty-boot skeleton (net stack is product, Phase E).
#[allow(dead_code)]
const LWIP: &[Unit] = &[
    ("../external/lwip/src/core/init.c", "lwip_init"),
    ("../external/lwip/src/core/def.c", "lwip_def"),
    ("../external/lwip/src/core/inet_chksum.c", "lwip_inet_chksum"),
    ("../external/lwip/src/core/ip.c", "lwip_ip"),
    ("../external/lwip/src/core/mem.c", "lwip_mem"),
    ("../external/lwip/src/core/memp.c", "lwip_memp"),
    ("../external/lwip/src/core/netif.c", "lwip_netif"),
    ("../external/lwip/src/core/pbuf.c", "lwip_pbuf"),
    ("../external/lwip/src/core/stats.c", "lwip_stats"),
    ("../external/lwip/src/core/sys.c", "lwip_core_sys"),
    ("../external/lwip/src/core/timeouts.c", "lwip_timeouts"),
    ("../external/lwip/src/core/udp.c", "lwip_udp"),
    ("../external/lwip/src/core/tcp.c", "lwip_tcp"),
    ("../external/lwip/src/core/tcp_in.c", "lwip_tcp_in"),
    ("../external/lwip/src/core/tcp_out.c", "lwip_tcp_out"),
    ("../external/lwip/src/core/dns.c", "lwip_dns"),
    ("../external/lwip/src/core/raw.c", "lwip_raw"),
    ("../external/lwip/src/core/ipv4/etharp.c", "lwip_etharp"),
    ("../external/lwip/src/core/ipv4/icmp.c", "lwip_icmp"),
    ("../external/lwip/src/core/ipv4/ip4.c", "lwip_ip4"),
    ("../external/lwip/src/core/ipv4/ip4_addr.c", "lwip_ip4_addr"),
    ("../external/lwip/src/core/ipv4/ip4_frag.c", "lwip_ip4_frag"),
    ("../external/lwip/src/core/ipv4/dhcp.c", "lwip_dhcp"),
    ("../external/lwip/src/netif/ethernet.c", "lwip_ethernet"),
];

/// Floor units — shared after platform-specific files (order matches link).
/// Empty-boot skeleton (see `registration_rethink` plan, Phase A): only what
/// `bringup.c`'s slimmed floor + harvest detectors need. `PRODUCT_COMMON` /
/// `LWIP` hold the rest, disabled from default build (not deleted — revive
/// module by module in later phases).
const FLOOR_COMMON: &[Unit] = &[
    ("boot/platform/private/bringup.c", "bringup"),
    ("util/fourcc/__init__.c", "fourcc"),
    ("util/eightcc/__init__.c", "eightcc"),
    ("bus/pci/_cfg.c", "cfg"),
    ("bus/virtio/_detect.c", "virtio_detect"),
    ("bus/virtio/_pci.c", "virtio_pci"),
    ("dev/blk/_detect.c", "blk_detect"),
    ("libc/string.c", "string"),
    ("libc/stdlib.c", "stdlib"),
    ("libc/stdio.c", "stdio"),
];

/// Product units — disabled from the empty-boot skeleton; revive per module
/// (Phase B/E) once their Rust faces + Cargo deps are back.
#[allow(dead_code)]
const PRODUCT_COMMON: &[Unit] = &[
    ("boot/rootfs/_root_fat.c", "root_fat"),
    ("dev/net/_virtio_net.c", "virtio_net"),
    ("dev/blk/_virtio_blk.c", "virtio_blk"),
    ("dev/stream/__init__.c", "stream"),
    ("auth/__init__.c", "auth"),
    ("../external/monocypher/src/monocypher.c", "monocypher"),
];

/// The net protocol clients are Rust now and always live in boot.a; only the
/// harness itself is stress-only. Disabled from default build (empty-boot
/// skeleton) alongside `PRODUCT_COMMON` / `LWIP`.
#[allow(dead_code)]
const STRESS: &[Unit] = &[("../../stress/stress.c", "stress")];

const BIOS_PLAT: &[Unit] = &[
    ("boot/platform/bios/uart.c", "uart"),
    ("boot/platform/bios/io_port.c", "io_port"),
    ("boot/platform/bios/mem_map.c", "mem_map"),
    ("boot/platform/bios/power.c", "power"),
    ("boot/platform/bios/handoff.c", "handoff"),
    ("boot/platform/bios/time.c", "time"),
    ("boot/platform/bios/virtio_pages.c", "virtio_pages"),
    ("boot/platform/bios/main.c", "main"),
];

const EFI_PLAT: &[Unit] = &[
    ("boot/platform/efi/efi_ctx.c", "efi_ctx"),
    ("boot/platform/efi/uart.c", "uart"),
    ("boot/platform/efi/io_port.c", "io_port"),
    ("boot/platform/efi/mem_map.c", "mem_map"),
    ("boot/platform/efi/power.c", "power"),
    ("boot/platform/efi/handoff.c", "handoff"),
    ("boot/platform/efi/time.c", "time"),
    ("boot/platform/efi/acpi_rsdp.c", "acpi_rsdp"),
    ("boot/platform/efi/virtio_pages.c", "virtio_pages"),
    ("boot/platform/efi/main.c", "main"),
];

const BIOS_CFLAGS: &[&str] = &[
    "-std=c11",
    "-ffreestanding",
    "-nostdinc",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-pie",
    "-m64",
    "-mno-red-zone",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-macro-redefined",
    "-O2",
    "-DPM_METAL_BOOT_TARGET_BIOS=1",
];

const EFI_CFLAGS: &[&str] = &[
    "-std=c11",
    "--target=x86_64-unknown-windows",
    "-ffreestanding",
    "-nostdinc",
    "-fno-stack-protector",
    "-fno-pic",
    "-fshort-wchar",
    "-mno-red-zone",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-macro-redefined",
    "-O2",
    "-DPM_METAL_BOOT_TARGET_EFI=1",
];

/// The NTP mock port moved to the `exp2_stress` cargo feature with net.ntp.
const STRESS_CFLAGS: &[&str] = &["-DEXP2_STRESS=1"];

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

struct Tree {
    root: PathBuf,
    metal: PathBuf,
    cc: PathBuf,
    ld: PathBuf,
    lld_link: Option<PathBuf>,
    stress: bool,
}

impl Tree {
    fn open(metal_root: &str, stress: bool) -> Result<Self, String> {
        let root = PathBuf::from(metal_root);
        let metal = root.join("src/pymergetic/metal");
        if !root.join("external/lwip/src/include").is_dir() {
            return Err(String::from(
                "missing external/lwip - git submodule update --init external/lwip",
            ));
        }
        let cc = which("clang")
            .or_else(|| which("gcc"))
            .ok_or_else(|| String::from("need clang or gcc"))?;
        let ld = which("ld.lld")
            .or_else(|| which("ld"))
            .ok_or_else(|| String::from("need ld.lld or ld"))?;
        let lld_link = which("rustc").and_then(|_| {
            let out = Command::new("rustc").args(["--print", "sysroot"]).output().ok()?;
            if !out.status.success() {
                return None;
            }
            let sys = String::from_utf8_lossy(&out.stdout).trim().to_string();
            let p = PathBuf::from(sys)
                .join("lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/lld-link");
            p.is_file().then_some(p)
        });
        Ok(Self {
            root,
            metal,
            cc,
            ld,
            lld_link,
            stress,
        })
    }

    fn resolve(&self, rel: &str) -> PathBuf {
        if let Some(rest) = rel.strip_prefix("../external/") {
            self.root.join("external").join(rest)
        } else if let Some(rest) = rel.strip_prefix("../../stress/") {
            self.root.join("stress").join(rest)
        } else {
            self.metal.join(rel)
        }
    }

    fn prep(&self) -> Result<(), String> {
        let root = self.root.to_str().unwrap();
        if crate::_config::config_gen(root) != 0 {
            return Err(String::from("forge config gen failed"));
        }
        #[cfg(feature = "builders")]
        {
            crate::_img::embed_rootfs(root)?;
        }
        #[cfg(not(feature = "builders"))]
        {
            return Err(String::from("forge build needs feature builders"));
        }
        let mut store = crate::SoloStore::new();
        let mut sess = crate::SoloSession::from_args(alloc::vec::Vec::new());
        let rc = crate::_sync::cmd_sync(&mut store, &mut sess, root, false);
        if rc != 0 {
            return Err(String::from("forge mod sync failed"));
        }
        /* Wasm packs must exist before cargo boot (include_bytes in wasm proof). */
        match crate::_pack::pack_all(root) {
            Ok(n) => eprintln!("forge build: packed {n} wasm package(s)"),
            Err(e) => return Err(alloc::format!("forge pack: {e}")),
        }
        Ok(())
    }

    fn cargo_boot(&self, target: &str) -> Result<(), String> {
        let boot = self.metal.join("boot");
        eprintln!("forge build: cargo boot ({target})");
        let mut cargo = Command::new("cargo");
        cargo
            .current_dir(&boot)
            .env("CARGO_TARGET_DIR", boot.join(".target"))
            .args([
                "build",
                "--manifest-path",
                ".pm/Cargo.toml",
                "--lib",
                "--release",
                "--target",
                target,
            ]);
        if self.stress {
            /* Mirrors STRESS_CFLAGS for the Rust half (net.ntp host mock port). */
            cargo.args(["--features", "exp2_stress"]);
        }
        run(&mut cargo)
    }

    fn cflags(&self, base: &[&str], plat_includes: &[PathBuf]) -> Vec<String> {
        let mut f = Vec::new();
        push_flags(&mut f, base);
        if self.stress {
            push_flags(&mut f, STRESS_CFLAGS);
        }
        f.push(String::from("-include"));
        f.push(self.root.join("build/autoconf.h").display().to_string());
        f.push(alloc::format!("-I{}", self.root.join("build").display()));
        let mut incs = vec![
            self.metal.join("libc"),
            self.root.join("src"),
            // Generated cross-module faces (`metal mod sync`) -- every
            // `<pymergetic/metal/<mod>/...>` include that isn't a human C
            // header colocated under `src/` resolves here instead.
            self.root.join("include"),
            self.metal.join("net/ip"),
            self.metal.join("net/ip/cfg"),
            self.root.join("external/lwip/src/include"),
            self.root.join("external/monocypher/src"),
        ];
        incs.extend_from_slice(plat_includes);
        push_includes(&mut f, &incs);
        f
    }

    /// Package-relative `-I` / `file`. `directory` is the absolute package
    /// root — clangd 22 does not match `"directory": "."` + relative `file`
    /// (falls back to Generic with cwd = source dir, so `-Isrc/...` misses
    /// metal/libc and `-nostdinc` surfaces as `'stddef.h' file not found`).
    fn cdb_includes_bios(&self) -> Vec<&'static str> {
        alloc::vec![
            "build",
            "src/pymergetic/metal/libc",
            "src",
            "include",
            "src/pymergetic/metal/net/ip",
            "src/pymergetic/metal/net/ip/cfg",
            "external/lwip/src/include",
            "external/monocypher/src",
            "src/pymergetic/metal/boot/platform/bios",
        ]
    }

    fn cdb_includes_efi(&self) -> Vec<&'static str> {
        alloc::vec![
            "build",
            "src/pymergetic/metal/libc",
            "src",
            "include",
            "src/pymergetic/metal/net/ip",
            "src/pymergetic/metal/net/ip/cfg",
            "external/lwip/src/include",
            "external/monocypher/src",
            "src/pymergetic/metal/boot/platform/efi",
            "external/edk2/MdePkg/Include",
            "external/edk2/MdePkg/Include/X64",
            "external/edk2/MdePkg/Include/Protocol",
            "external/edk2/MdePkg/Include/Guid",
        ]
    }

    /// Dropbear glue: stubs before metal/libc (matches net/ssh/.pm/build.rs).
    fn cdb_includes_ssh(&self) -> Vec<&'static str> {
        alloc::vec![
            "src/pymergetic/metal/net/ssh/dropbear_stubs",
            "src/pymergetic/metal/libc",
            "src/pymergetic/metal/net/ssh",
            "src/pymergetic/metal/net/ssh/dropbear_metal",
            "external/dropbear/src",
            "src",
            "include",
            "src/pymergetic/metal",
            "src/pymergetic/metal/net/ip",
            "src/pymergetic/metal/net/ip/cfg",
            "external/lwip/src/include",
        ]
    }

    /// WAMR Metal port glue (matches wasm/.pm/build.rs freestanding includes).
    fn cdb_includes_wasm(&self) -> Vec<&'static str> {
        alloc::vec![
            "src/pymergetic/metal/wasm/port/platform",
            "src/pymergetic/metal/wasm/port",
            "src/pymergetic/metal/libc",
            "src",
            "include",
            "external/wamr/core/iwasm/include",
            "external/wamr/core/iwasm/interpreter",
            "external/wamr/core/iwasm/common",
            "external/wamr/core/shared/platform/include",
            "external/wamr/core/shared/mem-alloc",
            "external/wamr/core/shared/utils",
            "external/wamr/core/shared/utils/uncommon",
            "external/wamr/core",
        ]
    }

    fn unit_pkg_rel(rel: &str) -> String {
        if let Some(rest) = rel.strip_prefix("../external/") {
            alloc::format!("external/{rest}")
        } else if let Some(rest) = rel.strip_prefix("../../stress/") {
            alloc::format!("stress/{rest}")
        } else {
            alloc::format!("src/pymergetic/metal/{rel}")
        }
    }

    fn cdb_push_unit(
        body: &mut String,
        first: &mut bool,
        dir_s: &str,
        file: &str,
        cflags: &[&str],
        extra_cflags: &[&str],
        incs: &[&str],
        include_autoconf: bool,
    ) {
        if !*first {
            body.push_str(",\n");
        }
        *first = false;
        body.push_str("  {\n");
        body.push_str("    \"directory\": \"");
        body.push_str(dir_s);
        body.push_str("\",\n");
        body.push_str("    \"arguments\": [\n");
        body.push_str("      \"clang\"");
        for fl in cflags {
            body.push_str(",\n      \"");
            body.push_str(fl);
            body.push('"');
        }
        for fl in extra_cflags {
            body.push_str(",\n      \"");
            body.push_str(fl);
            body.push('"');
        }
        if include_autoconf {
            body.push_str(",\n      \"-include\",\n      \"build/autoconf.h\"");
        }
        for inc in incs {
            body.push_str(",\n      \"-I");
            body.push_str(inc);
            body.push('"');
        }
        body.push_str(",\n      \"-c\",\n      \"-o\",\n      \"/dev/null\",\n      \"");
        body.push_str(file);
        body.push_str("\"\n    ],\n");
        body.push_str("    \"file\": \"");
        body.push_str(file);
        body.push_str("\"\n  }");
    }

    fn write_compile_commands(&self) -> Result<(), String> {
        ensure_dir(&self.root.join("build"))?;
        let bios_incs = self.cdb_includes_bios();
        let efi_incs = self.cdb_includes_efi();
        let dir = self
            .root
            .canonicalize()
            .unwrap_or_else(|_| self.root.clone());
        let dir_s = dir.to_str().ok_or_else(|| String::from("cdb directory utf8"))?;
        let mut body = String::from("[\n");
        let mut first = true;
        let stress_extra: &[&str] = if self.stress { STRESS_CFLAGS } else { &[] };

        // Empty-boot skeleton: only floor units in the CDB (product/LWIP/
        // stress stay disabled + out of clangd until revived — see
        // `registration_rethink` plan Phase A/B).
        let bios_units: Vec<&[Unit]> = alloc::vec![BIOS_PLAT, FLOOR_COMMON];
        for group in bios_units {
            for &(rel, _) in group {
                let file = Self::unit_pkg_rel(rel);
                Self::cdb_push_unit(
                    &mut body,
                    &mut first,
                    dir_s,
                    &file,
                    BIOS_CFLAGS,
                    stress_extra,
                    &bios_incs,
                    true,
                );
            }
        }

        // EFI ports need their own CDB rows or clangd falls back without Uefi.h
        // (UINT32 / EFI_* look unknown under boot/platform/efi/).
        for &(rel, _) in EFI_PLAT {
            let file = Self::unit_pkg_rel(rel);
            Self::cdb_push_unit(
                &mut body,
                &mut first,
                dir_s,
                &file,
                EFI_CFLAGS,
                &[],
                &efi_incs,
                true,
            );
        }

        let extras = [
            "src/pymergetic/metal/libc/stdint.h",
            "src/pymergetic/metal/net/ip/__init__.h",
        ];
        for file in extras {
            Self::cdb_push_unit(
                &mut body,
                &mut first,
                dir_s,
                file,
                &[
                    "-std=c11",
                    "-ffreestanding",
                    "-nostdinc",
                    "-fno-stack-protector",
                    "-m64",
                    "-mno-red-zone",
                ],
                &[],
                &bios_incs,
                false,
            );
        }

        let ssh_incs = self.cdb_includes_ssh();
        let ssh_cflags: &[&str] = &[
            "-std=gnu11",
            "-ffreestanding",
            "-nostdinc",
            "-fno-stack-protector",
            "-m64",
            "-mno-red-zone",
            "-DDROPBEAR_SERVER=1",
            "-DDROPBEAR_CLIENT=0",
            "-DDROPBEAR_METAL=1",
            "-DLOCALOPTIONS_H_EXISTS=1",
            "-DBUNDLED_LIBTOM=1",
            "-DUSE_LTM",
            "-DLTM_DESC",
            "-U__linux__",
            "-Ulinux",
            "-U__gnu_linux__",
        ];
        let ssh_glue = [
            "src/pymergetic/metal/net/ssh/dropbear_posix.c",
            "src/pymergetic/metal/net/ssh/dropbear_fd.c",
            "src/pymergetic/metal/net/ssh/dropbear_crt.c",
            "src/pymergetic/metal/net/ssh/ssh_dropbear.c",
            "src/pymergetic/metal/net/ssh/ssh_config.c",
            "src/pymergetic/metal/net/ssh/ssh_server.c",
        ];
        for file in ssh_glue {
            Self::cdb_push_unit(
                &mut body,
                &mut first,
                dir_s,
                file,
                ssh_cflags,
                &[],
                &ssh_incs,
                false,
            );
        }

        let wasm_incs = self.cdb_includes_wasm();
        let wasm_cflags: &[&str] = &[
            "-std=c11",
            "-ffreestanding",
            "-nostdinc",
            "-fno-stack-protector",
            "-m64",
            "-mno-red-zone",
            "-DBH_PLATFORM_METAL",
            "-DBUILD_TARGET_X86_64",
            "-DWASM_ENABLE_INTERP=1",
            "-DWASM_ENABLE_FAST_INTERP=1",
            "-DWASM_ENABLE_AOT=0",
            "-DWASM_ENABLE_JIT=0",
            "-DWASM_ENABLE_FAST_JIT=0",
            "-DWASM_ENABLE_LIBC_BUILTIN=0",
            "-DWASM_ENABLE_LIBC_WASI=0",
            "-DWASM_ENABLE_SIMD=0",
            "-DWASM_ENABLE_MULTI_MODULE=0",
            "-DWASM_ENABLE_SHARED_MEMORY=0",
            "-DWASM_ENABLE_MINI_LOADER=0",
            "-DWASM_DISABLE_HW_BOUND_CHECK=1",
            "-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1",
            "-DWASM_ENABLE_BULK_MEMORY=1",
            "-DBH_MALLOC=wasm_runtime_malloc",
            "-DBH_FREE=wasm_runtime_free",
            "-U__linux__",
            "-Ulinux",
            "-U__gnu_linux__",
        ];
        let wasm_glue = [
            "src/pymergetic/metal/wasm/port/platform/metal_platform.c",
            "src/pymergetic/metal/wasm/port/runtime_host.c",
        ];
        for file in wasm_glue {
            Self::cdb_push_unit(
                &mut body,
                &mut first,
                dir_s,
                file,
                wasm_cflags,
                &[],
                &wasm_incs,
                false,
            );
        }
        body.push_str("\n]\n");
        /* Package-root CDB + build/ mirror; directory = abs package root. */
        let root_cdb = self.root.join("compile_commands.json");
        let build_cdb = self.root.join("build/compile_commands.json");
        std::fs::write(&root_cdb, body.as_bytes())
            .map_err(|_| String::from("write compile_commands.json"))?;
        std::fs::write(&build_cdb, body.as_bytes())
            .map_err(|_| String::from("write build/compile_commands.json"))?;
        eprintln!("forge build: compile_commands.json (directory=abs package root)");
        Ok(())
    }

    fn compile_units(
        &self,
        plat: &[Unit],
        objdir: &Path,
        ext: &str,
        cflags: &[String],
    ) -> Result<Vec<String>, String> {
        // Empty-boot skeleton: floor only (product/LWIP/stress disabled).
        let groups: Vec<&[Unit]> = alloc::vec![plat, FLOOR_COMMON];
        let mut stems = Vec::new();
        for group in groups {
            for &(rel, stem) in group {
                let src = self.resolve(rel);
                let obj = objdir.join(alloc::format!("{stem}.{ext}"));
                eprintln!("  cc  {stem}.{ext}");
                run(Command::new(&self.cc)
                    .args(cflags.iter().map(String::as_str))
                    .arg("-c")
                    .arg(&src)
                    .arg("-o")
                    .arg(&obj))?;
                stems.push(String::from(stem));
            }
        }
        Ok(stems)
    }

    fn build_bios(&self) -> Result<(), String> {
        self.cargo_boot("x86_64-unknown-none")?;
        let boot_a = self.metal.join(
            "boot/.target/x86_64-unknown-none/release/libpymergetic_metal_boot.a",
        );
        ensure_file(&boot_a, "cargo boot")?;
        let out = self.root.join("build/x86_64_bios");
        let obj = out.join("obj");
        ensure_dir(&obj)?;
        let _ = std::fs::remove_file(out.join("metal.elf"));
        let _ = std::fs::remove_file(out.join("metal.qemu.elf"));

        let bios = self.metal.join("boot/platform/bios");
        let cflags = self.cflags(BIOS_CFLAGS, &[bios.clone()]);
        let mut stems = self.compile_units(BIOS_PLAT, &obj, "o", &cflags)?;

        eprintln!("  as  crt0.S");
        run(Command::new(&self.cc)
            .args(cflags.iter().map(String::as_str))
            .args(["-c"])
            .arg(bios.join("crt0.S"))
            .arg("-o")
            .arg(obj.join("crt0.o")))?;
        stems.insert(0, String::from("crt0"));

        let elf = out.join("metal.elf");
        eprintln!("forge build: link {}", elf.display());
        let mut link = Command::new(&self.ld);
        link.args(["-m", "elf_x86_64", "-nostdlib", "-static", "-z", "noexecstack", "-T"])
            .arg(bios.join("link.ld"))
            .arg("-o")
            .arg(&elf);
        // crt0 … main last before archive (match prior order: main before boot.a)
        let (main, rest): (Vec<_>, Vec<_>) =
            stems.into_iter().partition(|s| s.as_str() == "main");
        for s in rest {
            link.arg(obj.join(alloc::format!("{s}.o")));
        }
        for s in main {
            link.arg(obj.join(alloc::format!("{s}.o")));
        }
        link.arg(&boot_a);
        run(&mut link)?;

        self.link_bios_trampoline(&bios, &obj, &elf, &out.join("metal.qemu.elf"))?;
        eprintln!(
            "forge build: ok bios -> {} + {}",
            elf.display(),
            out.join("metal.qemu.elf").display()
        );
        Ok(())
    }

    fn link_bios_trampoline(
        &self,
        bios: &Path,
        obj: &Path,
        elf: &Path,
        tramp: &Path,
    ) -> Result<(), String> {
        eprintln!("forge build: trampoline {}", tramp.display());
        run(Command::new(&self.cc).args([
            "-m32",
            "-ffreestanding",
            "-fno-pic",
            "-fno-stack-protector",
            "-Wall",
            "-O2",
            "-c",
        ])
        .arg(bios.join("trampoline_load.c"))
        .arg("-o")
        .arg(obj.join("trampoline_load.o")))?;
        run(Command::new(&self.cc)
            .args(["-m32", "-c"])
            .arg(bios.join("trampoline32.S"))
            .arg("-o")
            .arg(obj.join("trampoline32.o")))?;
        run(Command::new(&self.cc)
            .args(["-m64", "-c"])
            .arg(bios.join("trampoline64.S"))
            .arg("-o")
            .arg(obj.join("trampoline64_64.o")))?;
        run(Command::new("objcopy")
            .args(["-O", "elf32-i386"])
            .arg(obj.join("trampoline64_64.o"))
            .arg(obj.join("trampoline64.o")))?;
        run(Command::new(&self.ld)
            .args(["-m", "elf_i386", "-r", "-b", "binary", "-o"])
            .arg(obj.join("metal_bin.o"))
            .arg(elf))?;

        let nm = crate::_host::output(Command::new("nm").arg(obj.join("metal_bin.o")))?;
        let txt = String::from_utf8_lossy(&nm.stdout);
        let mut start = None;
        let mut end = None;
        for line in txt.lines() {
            let p: Vec<_> = line.split_whitespace().collect();
            if p.len() >= 3 {
                if p[2].ends_with("_start") && start.is_none() {
                    start = Some(p[2].to_string());
                }
                if p[2].ends_with("_end") && end.is_none() {
                    end = Some(p[2].to_string());
                }
            }
        }
        let start = start.ok_or_else(|| String::from("blob _start missing"))?;
        let end = end.ok_or_else(|| String::from("blob _end missing"))?;
        run(Command::new("objcopy")
            .arg(alloc::format!("--redefine-sym={start}=_binary_metal_elf_start"))
            .arg(alloc::format!("--redefine-sym={end}=_binary_metal_elf_end"))
            .arg(obj.join("metal_bin.o"))
            .arg(obj.join("metal_bin_named.o")))?;
        run(Command::new(&self.ld)
            .args(["-m", "elf_i386", "-nostdlib", "-static", "-z", "noexecstack", "-T"])
            .arg(bios.join("link32.ld"))
            .arg("-o")
            .arg(tramp)
            .args([
                obj.join("trampoline32.o"),
                obj.join("trampoline64.o"),
                obj.join("trampoline_load.o"),
                obj.join("metal_bin_named.o"),
            ]))
    }

    fn build_efi(&self) -> Result<(), String> {
        ensure_file(
            &self.root.join("external/edk2/MdePkg/Include/Uefi.h"),
            "missing external/edk2 (EFI headers). Add submodule:\n  git submodule update --init --recursive external/edk2\n(or: git clone --depth 1 --branch edk2-stable202502 https://github.com/tianocore/edk2.git external/edk2 && git -C external/edk2 submodule update --init --depth 1 --recursive)",
        )?;
        let lld = self
            .lld_link
            .as_ref()
            .ok_or_else(|| String::from("missing lld-link (rustup)"))?;
        self.cargo_boot("x86_64-unknown-uefi")?;
        let boot_a = self.metal.join(
            "boot/.target/x86_64-unknown-uefi/release/libpymergetic_metal_boot.a",
        );
        ensure_file(&boot_a, "cargo boot")?;
        let out = self.root.join("build/x86_64_efi");
        let obj = out.join("obj");
        ensure_dir(&obj)?;
        let _ = std::fs::remove_file(out.join("metal.efi"));

        let efi = self.metal.join("boot/platform/efi");
        let edk2_inc = efi.join("edk2_inc");
        let _ = std::fs::remove_file(&edk2_inc);
        let _ = std::os::unix::fs::symlink(
            "../../../../../../external/edk2/MdePkg/Include",
            &edk2_inc,
        );

        let cflags = self.cflags(
            EFI_CFLAGS,
            &[
                efi.clone(),
                edk2_inc.clone(),
                edk2_inc.join("X64"),
                edk2_inc.join("Protocol"),
                edk2_inc.join("Guid"),
            ],
        );
        let stems = self.compile_units(EFI_PLAT, &obj, "obj", &cflags)?;

        let efi_img = out.join("metal.efi");
        eprintln!("forge build: link {}", efi_img.display());
        let mut link = Command::new(lld);
        link.args([
            "-subsystem:efi_application",
            "-entry:UefiMain",
            "-base:0",
            "-dynamicbase:no",
            "-highentropyva:no",
            "-nxcompat:no",
            "-tsaware:no",
        ])
        .arg(alloc::format!("-out:{}", efi_img.display()));
        let (main, rest): (Vec<_>, Vec<_>) =
            stems.into_iter().partition(|s| s.as_str() == "main");
        for s in rest {
            link.arg(obj.join(alloc::format!("{s}.obj")));
        }
        for s in main {
            link.arg(obj.join(alloc::format!("{s}.obj")));
        }
        link.arg(&boot_a);
        run(&mut link)?;
        eprintln!("forge build: ok efi -> {}", efi_img.display());
        Ok(())
    }
}

pub fn build_targets(metal_root: &str, target: &str, stress: bool) -> Result<(), String> {
    let tree = Tree::open(metal_root, stress)?;
    tree.prep()?;
    match target {
        "bios" | "x86_64" => tree.build_bios()?,
        "efi" => tree.build_efi()?,
        "all" | "both" => {
            tree.build_bios()?;
            tree.build_efi()?;
        }
        other => return Err(alloc::format!("unsupported target {other} (bios|efi|all)")),
    }
    tree.write_compile_commands()
}

pub fn run_build(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let stress = sess_flag(sess, "--stress")
        || std::env::var("EXP2_STRESS").map(|v| v == "1").unwrap_or(false);
    let target = sess_positional(sess, 1, "all");
    match build_targets(metal_root, &target, stress) {
        Ok(()) => {
            sess.set_exit(0);
            0
        }
        Err(e) => {
            let _ = block_on(|| sess.err_line(&e));
            sess.set_exit(1);
            1
        }
    }
}
