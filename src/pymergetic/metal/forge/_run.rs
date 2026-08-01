//! forge run / stress — QEMU peers over shared host helpers.

use alloc::string::String;
use alloc::vec::Vec;
use std::io::{Read, Write};
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

const QEMU_MACHINE: &[&str] = &[
    "-machine",
    "q35,accel=kvm:tcg",
    "-m",
    "512",
    "-display",
    "none",
    "-device",
    "isa-debug-exit,iobase=0x501,iosize=0x02",
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

fn ensure_vblk(tree: &Path) -> Result<PathBuf, String> {
    let vblk = tree.join("build/vblk.img");
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

/// `bootindex`: None = omit (BIOS -kernel); Some(n) for EFI disk order.
fn virtio_drive(tree: &Path, bootindex: Option<u32>) -> Result<Vec<String>, String> {
    let vblk = ensure_vblk(tree)?;
    let mut a: Vec<String> = VIRTIO.iter().map(|s| String::from(*s)).collect();
    a.extend([
        String::from("-drive"),
        alloc::format!("if=none,id=vd0,format=raw,file={}", vblk.display()),
        String::from("-device"),
        match bootindex {
            Some(i) => alloc::format!("virtio-blk-pci,drive=vd0,bootindex={i}"),
            None => String::from("virtio-blk-pci,drive=vd0"),
        },
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

/// True when `b` can still grow into a filtered CSI (clear / mode / cursor home).
fn csi_prefix_hold(b: &[u8]) -> bool {
    if b.is_empty() {
        return false;
    }
    if b[0] != 0x1b {
        return false;
    }
    if b.len() == 1 {
        return true;
    }
    if b[1] != b'[' {
        return false;
    }
    let rest = &b[2..];
    if rest.is_empty() {
        return true;
    }
    if b"2J".starts_with(rest) || b"=3h".starts_with(rest) || b"H".starts_with(rest) {
        return true;
    }
    /* ESC [ digits/';  then maybe H — hold while still in the param run */
    let mut j = 0usize;
    while j < rest.len() && (rest[j].is_ascii_digit() || rest[j] == b';') {
        j += 1;
    }
    j > 0 && j == rest.len()
}

/// Drop OVMF ConOut clears that wipe the host TTY (same UART Metal uses).
/// Incomplete CSI at the end of `chunk` is left in `hold` for the next call.
fn write_serial_filtered(out: &mut dyn Write, hold: &mut Vec<u8>, chunk: &[u8]) -> std::io::Result<()> {
    /* Drop machine ready mark from the host TTY (still kept in raw log). */
    const READY: &[u8] = b"#pm-metal/boot-tree/ready"; /* = boot/tree/_impl/ready.rs READY_MARK */
    hold.extend_from_slice(chunk);
    while let Some(pos) = hold.windows(READY.len()).position(|w| w == READY) {
        hold.drain(pos..pos + READY.len());
    }
    /* Keep a proper prefix of READY so a split mark is not partially printed. */
    let mut keep = 0usize;
    let max_keep = READY.len().saturating_sub(1).min(hold.len());
    for n in (1..=max_keep).rev() {
        if READY.starts_with(&hold[hold.len() - n..]) {
            keep = n;
            break;
        }
    }
    let limit = hold.len() - keep;
    let mut i = 0usize;
    while i < limit {
        if hold[i] == 0x1b {
            let avail = &hold[i..limit];
            if csi_prefix_hold(avail) {
                /* Incomplete CSI — keep from i through the READY prefix tail. */
                hold.drain(..i);
                return out.flush();
            }
            if avail.len() >= 2 && avail[1] == b'[' {
                let rest = &avail[2..];
                if rest.starts_with(b"2J") {
                    i += 4;
                    continue;
                }
                if rest.starts_with(b"=3h") {
                    i += 5;
                    continue;
                }
                if rest.starts_with(b"H") {
                    i += 3;
                    continue;
                }
                let mut j = 0usize;
                while j < rest.len() && (rest[j].is_ascii_digit() || rest[j] == b';') {
                    j += 1;
                }
                if j > 0 && j < rest.len() && rest[j] == b'H' {
                    i += 2 + j + 1;
                    continue;
                }
            }
        }
        out.write_all(&hold[i..i + 1])?;
        i += 1;
    }
    hold.drain(..limit);
    out.flush()
}

fn pump_serial_filtered(mut r: impl Read, log_path: &Path) -> Result<(), String> {
    let mut raw = Vec::new();
    let mut hold = Vec::new();
    let mut buf = [0u8; 4096];
    let mut stdout = std::io::stdout();
    /* Same bytes as boot/tree/_impl/ready.rs READY_MARK — do not scrape human tree text. */
    const READY: &[u8] = b"#pm-metal/boot-tree/ready"; /* = boot/tree/_impl/ready.rs READY_MARK */
    loop {
        let n = r.read(&mut buf).map_err(|_| String::from("serial read failed"))?;
        if n == 0 {
            break;
        }
        raw.extend_from_slice(&buf[..n]);
        write_serial_filtered(&mut stdout, &mut hold, &buf[..n])
            .map_err(|_| String::from("serial write failed"))?;
        if memchr_slice(&raw, READY) {
            break;
        }
    }
    /* Flush a trailing incomplete ESC as literal (should be rare). */
    if !hold.is_empty() {
        stdout
            .write_all(&hold)
            .map_err(|_| String::from("serial write failed"))?;
        let _ = stdout.flush();
    }
    if let Some(parent) = log_path.parent() {
        let _ = ensure_dir(parent);
    }
    let _ = std::fs::write(log_path, &raw);
    Ok(())
}

fn memchr_slice(hay: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() || hay.len() < needle.len() {
        return false;
    }
    hay.windows(needle.len()).any(|w| w == needle)
}

fn run_bios(tree: &Path) -> Result<(), String> {
    let elf = tree.join("build/x86_64_bios/metal.qemu.elf");
    ensure_file(&elf, "forge build bios")?;
    let vio = virtio_drive(tree, None)?;
    let smp = smp();
    let serial_log = tree.join("build/x86_64_bios/last-serial.log");
    eprintln!("forge run: bios {}", elf.display());
    let mut cmd = Command::new("stdbuf");
    cmd.args(["-o0", "-e0", "qemu-system-x86_64"])
        .args(QEMU_MACHINE)
        .args(["-serial", "stdio"])
        .args(["-smp", &smp])
        .args(&vio)
        .arg("-kernel")
        .arg(&elf)
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit());
    let mut child = cmd.spawn().map_err(|_| String::from("qemu spawn failed"))?;
    let pump = if let Some(out) = child.stdout.take() {
        pump_serial_filtered(out, &serial_log)
    } else {
        Ok(())
    };
    let _ = child.kill();
    let _ = child.wait();
    pump?;
    eprintln!();
    eprintln!("forge run: serial log {}", serial_log.display());
    Ok(())
}

fn run_efi(tree: &Path) -> Result<(), String> {
    let efi_img = tree.join("build/x86_64_efi/metal.efi");
    ensure_file(&efi_img, "forge build efi")?;
    let ovmf = find_ovmf().ok_or_else(|| String::from("OVMF not found (apt: ovmf)"))?;
    let esp = tree.join("build/x86_64_efi/esp");
    let serial_log = tree.join("build/x86_64_efi/last-serial.log");
    let _ = std::fs::remove_dir_all(&esp);
    ensure_dir(&esp.join("EFI/BOOT"))?;
    std::fs::copy(&efi_img, esp.join("EFI/BOOT/BOOTX64.EFI")).map_err(|_| String::from("copy efi"))?;
    /* ESP boots first; virtio-blk rootfs is not an EFI system partition. */
    let vio = virtio_drive(tree, Some(1))?;
    let smp = smp();
    eprintln!("forge run: efi {} (OVMF {})", efi_img.display(), ovmf.display());
    /* stdout is a pipe for the CSI filter — force unbuffered or QEMU's stdio
     * block-buffers (~4K) and the host TTY looks hung until the child exits. */
    let mut cmd = Command::new("stdbuf");
    cmd.args(["-o0", "-e0", "qemu-system-x86_64"])
        .args(QEMU_MACHINE)
        .args(["-serial", "stdio"])
        .args(["-smp", &smp])
        .args(&vio)
        .args([
            "-drive",
            &alloc::format!("if=pflash,format=raw,readonly=on,file={}", ovmf.display()),
            "-drive",
            &alloc::format!("if=none,id=esp,format=raw,file=fat:rw:{}", esp.display()),
            "-device",
            "ide-hd,drive=esp,bootindex=0",
        ])
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit());
    let mut child = cmd.spawn().map_err(|_| String::from("qemu spawn failed"))?;
    let pump = if let Some(out) = child.stdout.take() {
        pump_serial_filtered(out, &serial_log)
    } else {
        Ok(())
    };
    /* Ctrl+C / early return must not leave QEMU behind. */
    let _ = child.kill();
    let _ = child.wait();
    pump?;
    eprintln!();
    eprintln!("forge run: serial log {}", serial_log.display());
    Ok(())
}

fn run_target(tree: &Path, target: &str) -> Result<(), String> {
    match target {
        "bios" | "x86_64" => run_bios(tree),
        "efi" => run_efi(tree),
        "all" | "both" => {
            run_bios(tree)?;
            run_efi(tree)
        }
        other => Err(alloc::format!("unsupported target {other} (bios|efi|all)")),
    }
}

pub fn run_qemu(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let tree = PathBuf::from(metal_root);
    let target = sess_positional(sess, 1, "all");
    match run_target(&tree, &target) {
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

const STRESS_NEEDLES: &[&str] = &["ready        ok", "+-- net          ok", "stress ok"];

pub fn run_stress(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let tree = PathBuf::from(metal_root);
    let logdir = tree.join("build/stress");
    let http_root = logdir.join("http_root");
    let _ = ensure_dir(&http_root);
    let _ = std::fs::write(http_root.join("index.html"), b"metal stress\n");

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

    let forge = PathBuf::from(metal_root).join("forge-cli");
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
