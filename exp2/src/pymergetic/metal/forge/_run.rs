//! forge run / stress — QEMU peers over shared host helpers.

use alloc::string::String;
use alloc::vec::Vec;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Duration;

use crate::_host::{ensure_dir, ensure_file, null_stdio, run, sess_positional};
use crate::_port::{block_on, ForgeSession};

const OVMF_CANDIDATES: &[&str] = &[
    "/usr/share/ovmf/OVMF.fd",
    "/usr/share/OVMF/OVMF.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
];

const QEMU_COMMON: &[&str] = &[
    "-machine",
    "q35,accel=kvm:tcg",
    "-m",
    "512",
    "-display",
    "none",
    "-device",
    "isa-debug-exit,iobase=0x501,iosize=0x02",
    "-serial",
    "stdio",
];

const VIRTIO: &[&str] = &[
    "-netdev",
    "user,id=n0",
    "-device",
    "virtio-net-pci,netdev=n0",
    "-chardev",
    "null,id=vcon",
    "-device",
    "virtio-serial-pci,max_ports=1",
    "-device",
    "virtconsole,chardev=vcon",
    "-device",
    "virtio-tablet-pci",
];

fn smp() -> String {
    std::env::var("EXP2_SMP").unwrap_or_else(|_| String::from("4"))
}

fn find_ovmf() -> Option<PathBuf> {
    OVMF_CANDIDATES.iter().map(PathBuf::from).find(|p| p.is_file())
}

fn ensure_vblk(exp2: &Path) -> Result<PathBuf, String> {
    let vblk = exp2.join("build/vblk.img");
    ensure_dir(vblk.parent().unwrap())?;
    if !vblk.is_file() {
        run(Command::new("dd").args([
            "if=/dev/zero",
            &alloc::format!("of={}", vblk.display()),
            "bs=1M",
            "count=8",
            "status=none",
        ]))?;
    }
    Ok(vblk)
}

fn virtio_drive(exp2: &Path) -> Result<Vec<String>, String> {
    let vblk = ensure_vblk(exp2)?;
    let mut a: Vec<String> = VIRTIO.iter().map(|s| String::from(*s)).collect();
    a.extend([
        String::from("-drive"),
        alloc::format!("if=none,id=vd0,format=raw,file={}", vblk.display()),
        String::from("-device"),
        String::from("virtio-blk-pci,drive=vd0"),
    ]);
    Ok(a)
}

fn qemu_ec(ec: i32) -> i32 {
    if ec == 1 {
        0
    } else {
        ec
    }
}

fn run_bios(exp2: &Path) -> Result<(), String> {
    let elf = exp2.join("build/x86_64_bios/metal.qemu.elf");
    ensure_file(&elf, "forge build bios")?;
    let vio = virtio_drive(exp2)?;
    let smp = smp();
    eprintln!("forge run: bios {}", elf.display());
    let mut cmd = Command::new("qemu-system-x86_64");
    cmd.args(QEMU_COMMON).args(["-smp", &smp]).args(&vio).arg("-kernel").arg(&elf);
    let status = cmd.status().map_err(|_| String::from("qemu spawn failed"))?;
    let code = qemu_ec(status.code().unwrap_or(1));
    if code == 0 {
        Ok(())
    } else {
        Err(alloc::format!("qemu bios exit {code}"))
    }
}

fn run_efi(exp2: &Path) -> Result<(), String> {
    let efi_img = exp2.join("build/x86_64_efi/metal.efi");
    ensure_file(&efi_img, "forge build efi")?;
    let ovmf = find_ovmf().ok_or_else(|| String::from("OVMF not found (apt: ovmf)"))?;
    let esp = exp2.join("build/x86_64_efi/esp");
    let _ = std::fs::remove_dir_all(&esp);
    ensure_dir(&esp.join("EFI/BOOT"))?;
    std::fs::copy(&efi_img, esp.join("EFI/BOOT/BOOTX64.EFI")).map_err(|_| String::from("copy efi"))?;
    let vio = virtio_drive(exp2)?;
    let smp = smp();
    eprintln!("forge run: efi {} (OVMF {})", efi_img.display(), ovmf.display());
    let mut cmd = Command::new("timeout");
    cmd.args(["--signal=KILL", "25s", "qemu-system-x86_64"])
        .args(QEMU_COMMON)
        .args(["-smp", &smp])
        .args(&vio)
        .args([
            "-drive",
            &alloc::format!("if=pflash,format=raw,readonly=on,file={}", ovmf.display()),
            "-drive",
            &alloc::format!("format=raw,file=fat:rw:{}", esp.display()),
            "-boot",
            "order=d",
        ]);
    let status = cmd.status().map_err(|_| String::from("qemu spawn failed"))?;
    let ec = status.code().unwrap_or(1);
    if ec == 124 || ec == 137 {
        return Err(String::from("efi qemu timed out"));
    }
    let code = qemu_ec(ec);
    if code == 0 {
        Ok(())
    } else {
        Err(alloc::format!("qemu efi exit {code}"))
    }
}

fn run_target(exp2: &Path, target: &str) -> Result<(), String> {
    match target {
        "bios" | "x86_64" => run_bios(exp2),
        "efi" => run_efi(exp2),
        "all" | "both" => {
            run_bios(exp2)?;
            run_efi(exp2)
        }
        other => Err(alloc::format!("unsupported target {other} (bios|efi|all)")),
    }
}

pub fn run_qemu(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let exp2 = PathBuf::from(metal_root).join("exp2");
    let target = sess_positional(sess, 1, "all");
    match run_target(&exp2, &target) {
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

fn log_has(path: &Path, needle: &str) -> bool {
    std::fs::read_to_string(path).map(|s| s.contains(needle)).unwrap_or(false)
}

const STRESS_NEEDLES: &[&str] = &["ready        ok", "net: eth0 dhcp ok", "stress ok"];

pub fn run_stress(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let exp2 = PathBuf::from(metal_root).join("exp2");
    let logdir = exp2.join("build/stress");
    let http_root = logdir.join("http_root");
    let _ = ensure_dir(&http_root);
    let _ = std::fs::write(http_root.join("index.html"), b"exp2 stress\n");

    eprintln!("forge stress: building opt-in stress images");
    if let Err(e) = crate::_build::build_targets(metal_root, "all", true) {
        let _ = block_on(|| sess.err_line(&e));
        sess.set_exit(1);
        return 1;
    }

    let mut http = match null_stdio(
        Command::new("python3").args([
            "-m",
            "http.server",
            "18080",
            "--bind",
            "127.0.0.1",
            "--directory",
        ])
        .arg(&http_root),
    )
    .spawn()
    {
        Ok(c) => c,
        Err(_) => {
            let _ = block_on(|| sess.err_line("http.server failed"));
            sess.set_exit(1);
            return 1;
        }
    };

    let ntp = r#"
import socket, struct, sys, time
log = open(sys.argv[1], "w", encoding="utf-8")
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("127.0.0.1", 18123))
log.write("ntp mock listening\n"); log.flush()
while True:
    data, addr = sock.recvfrom(512)
    if len(data) < 48: continue
    ntp = int(time.time()) + 2208988800
    pkt = bytearray(48); pkt[0]=0x24; pkt[1]=1; pkt[2]=4; pkt[3]=0xEC
    struct.pack_into("!I", pkt, 40, ntp); struct.pack_into("!I", pkt, 44, 0)
    sock.sendto(pkt, addr)
"#;
    let mut ntp_child = match null_stdio(
        Command::new("python3")
            .arg("-c")
            .arg(ntp)
            .arg(logdir.join("ntp.server.log")),
    )
    .spawn()
    {
        Ok(c) => c,
        Err(_) => {
            let _ = http.kill();
            let _ = block_on(|| sess.err_line("ntp mock failed"));
            sess.set_exit(1);
            return 1;
        }
    };

    std::thread::sleep(Duration::from_millis(300));
    let metal = PathBuf::from(metal_root).join("tools/metal/metal");
    eprintln!("forge stress: host mem smoke");
    if !Command::new(&metal)
        .args(["mod", "test", "pymergetic/metal/mem"])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
    {
        let _ = http.kill();
        let _ = ntp_child.kill();
        let _ = crate::_build::build_targets(metal_root, "all", false);
        let _ = block_on(|| sess.err_line("host mem smoke FAIL"));
        sess.set_exit(1);
        return 1;
    }

    let forge = PathBuf::from(metal_root).join("tools/forge");
    let mut failed = false;
    for (target, secs) in [("bios", "45"), ("efi", "45")] {
        let log = logdir.join(alloc::format!("{target}.serial.log"));
        eprintln!("forge stress: running {target}");
        let out = Command::new("timeout")
            .args(["--signal=KILL", &alloc::format!("{secs}s"), forge.to_str().unwrap(), "run", target])
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output();
        match out {
            Ok(o) => {
                let mut buf = o.stdout;
                buf.extend_from_slice(&o.stderr);
                let _ = std::fs::write(&log, &buf);
            }
            Err(_) => {
                failed = true;
                break;
            }
        }
        if log_has(&log, "stress FAIL") || STRESS_NEEDLES.iter().any(|n| !log_has(&log, n)) {
            eprintln!("forge stress: {target} FAIL ({})", log.display());
            failed = true;
            break;
        }
        eprintln!("forge stress: {target} PASS");
    }

    let _ = http.kill();
    let _ = ntp_child.kill();
    eprintln!("forge stress: restoring normal images");
    let _ = crate::_build::build_targets(metal_root, "all", false);

    if failed {
        let _ = block_on(|| sess.err_line("forge stress: FAIL"));
        sess.set_exit(1);
        1
    } else {
        eprintln!("forge stress: PASS bios+efi");
        sess.set_exit(0);
        0
    }
}
