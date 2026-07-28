#!/usr/bin/env python3
"""Boot Metal EFI in QEMU, run a short REPL introspection demo, screendump PNG.

Usage (from packages/metal):
  python3 scripts/capture-readme-shot.py
"""
from __future__ import annotations

import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_PNG = ROOT / "screenshots" / "py-introspect.png"
SER_SOCK = Path("/tmp/metal-readme-serial.sock")
MON_PORT = 4547
VNC_DISP = 7
LOG = Path("/tmp/metal-readme-shot.log")

# Keep this SHORT — 1280x800 + MetalPython banner + feature bullets leave
# only ~15 rows. console()/limits/about scrolls the banner off the hero.
REPL_DEMO = [
    ("import pymergetic.metal.mem.limit as L", None),
    ("L.get('net.asgi.ASGI_IO_MAX')", "ASGI_IO_MAX"),
    ("import pymergetic.metal.externals as E", None),
    ("[(r['id'], r['version']) for r in E.list()]", "micropython"),
]


def stage_esp() -> tuple[Path, Path, Path]:
    script = f"""
set -euo pipefail
ROOT="{ROOT}"
source "{ROOT}/scripts/lib/efi-qemu.sh"
EFI="{ROOT}/build/x86_64_efi/metal.efi"
ESP="{ROOT}/build/x86_64_efi/esp-readme-shot"
pm_metal_efi_stage_esp "$EFI" "$ESP"
VBLK="$(pm_metal_efi_stage_vblk)"
OVMF="$(pm_metal_efi_ovmf)"
printf '%s\\n' "$ESP" "$VBLK" "$OVMF"
"""
    out = subprocess.check_output(["bash", "-c", script], text=True).strip().splitlines()
    if len(out) < 3:
        raise RuntimeError(f"stage failed: {out!r}")
    return Path(out[-3]), Path(out[-2]), Path(out[-1])


def wait_sock(path: Path, timeout: float = 30.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.05)
    raise TimeoutError(f"missing socket {path}")


def mon_cmd(cmd: str, timeout: float = 5.0) -> str:
    with socket.create_connection(("127.0.0.1", MON_PORT), timeout=timeout) as s:
        s.settimeout(timeout)
        try:
            s.recv(4096)
        except socket.timeout:
            pass
        s.sendall((cmd.strip() + "\n").encode())
        time.sleep(0.25)
        try:
            return s.recv(65536).decode(errors="replace")
        except socket.timeout:
            return ""


class SerialSession:
    def __init__(self, sock: socket.socket) -> None:
        self.sock = sock
        self.buf = b""

    def poll(self, seconds: float = 0.2) -> None:
        deadline = time.time() + seconds
        self.sock.settimeout(0.1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(4096)
                if chunk:
                    self.buf += chunk
                    LOG.write_bytes(self.buf)
            except socket.timeout:
                pass

    def wait_boot_ready(self, timeout: float = 120.0) -> None:
        """Wait for first >>> then for services to finish noisy boot logs."""
        deadline = time.time() + timeout
        saw_prompt = False
        while time.time() < deadline:
            self.poll(0.2)
            text = self.buf.decode(errors="replace")
            if ">>>" in text:
                saw_prompt = True
            if saw_prompt and "asgi: listening" in text:
                # Quiet period after last boot chatter.
                self.poll(2.0)
                return
        if not saw_prompt:
            raise TimeoutError("REPL >>> never appeared")
        # asgi line optional — still proceed after prompt + settle
        self.poll(3.0)

    def run_repl(self, line: str, expect: str | None = None, timeout: float = 25.0) -> None:
        """Feed one REPL line; wait for expect substring and/or a new >>>."""
        before = self.buf.count(b">>>")
        busy_retries = 0
        self.sock.sendall((line + "\r").encode())
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.poll(0.15)
            text = self.buf.decode(errors="replace")
            tail = text[-600:]
            if "repl: busy" in tail and busy_retries < 4:
                busy_retries += 1
                time.sleep(0.8)
                self.sock.sendall((line + "\r").encode())
                continue
            ok_expect = expect is None or expect in text[-(len(expect) + 1200) :]
            ok_prompt = self.buf.count(b">>>") > before
            if ok_expect and ok_prompt and "repl: busy" not in text[-120:]:
                self.poll(0.35)
                return
        raise TimeoutError(f"no REPL completion after: {line!r} (expect={expect!r})")

def main() -> int:
    if not (ROOT / "build/x86_64_efi/metal.efi").is_file():
        print("missing build/x86_64_efi/metal.efi — run ./scripts/build efi", file=sys.stderr)
        return 1

    if SER_SOCK.exists():
        SER_SOCK.unlink()

    esp, vblk, ovmf = stage_esp()
    ppm = Path("/tmp/metal-readme-shot.ppm")
    if ppm.exists():
        ppm.unlink()
    LOG.write_bytes(b"")

    qemu = [
        "qemu-system-x86_64",
        "-machine",
        "q35,accel=kvm:tcg",
        "-smp",
        "4",
        "-m",
        "512",
        "-audiodev",
        "none,id=a0",
        "-netdev",
        "user,id=n0",
        "-device",
        "virtio-net-pci,netdev=n0",
        "-device",
        "virtio-sound-pci,audiodev=a0",
        "-drive",
        f"if=none,id=vd0,format=raw,file={vblk}",
        "-device",
        "virtio-blk-pci,drive=vd0",
        "-chardev",
        "null,id=vcon",
        "-device",
        "virtio-serial-pci,max_ports=1",
        "-device",
        "virtconsole,chardev=vcon",
        "-device",
        "virtio-tablet-pci",
        "-chardev",
        f"socket,id=ser0,path={SER_SOCK},server=on,wait=off",
        "-serial",
        "chardev:ser0",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={ovmf}",
        "-drive",
        f"format=raw,file=fat:rw:{esp}",
        "-boot",
        "order=d",
        "-vga",
        "std",
        "-global",
        "VGA.vgamem_mb=64",
        "-display",
        "none",
        "-vnc",
        f"127.0.0.1:{VNC_DISP}",
        "-monitor",
        f"telnet:127.0.0.1:{MON_PORT},server,nowait",
    ]

    print(f"capture: starting qemu (vnc :{VNC_DISP}, monitor {MON_PORT})", flush=True)
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        wait_sock(SER_SOCK, 20)
        time.sleep(0.3)
        raw = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        raw.connect(str(SER_SOCK))
        ser = SerialSession(raw)

        print("capture: waiting for REPL + asgi", flush=True)
        ser.wait_boot_ready(timeout=120.0)

        for line, expect in REPL_DEMO:
            print(f"capture: >>> {line}", flush=True)
            ser.run_repl(line, expect=expect)
            ser.poll(0.4)

        text = ser.buf.decode(errors="replace")
        if "net: iface" in text:
            print("capture: net iface spam still present", file=sys.stderr)
            return 1
        if "ASGI_IO_MAX" not in text or "micropython" not in text.lower():
            print("capture: demo did not look clean; serial tail:", file=sys.stderr)
            print(text[-2000:], file=sys.stderr)
            return 1

        print("capture: screendump", flush=True)
        mon_cmd(f"screendump {ppm}")
        for _ in range(50):
            if ppm.is_file() and ppm.stat().st_size > 1000:
                break
            time.sleep(0.1)
        if not ppm.is_file():
            print("capture: screendump missing", file=sys.stderr)
            return 1

        from PIL import Image

        img = Image.open(ppm)
        OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
        img.save(OUT_PNG, format="PNG", optimize=True)
        print(f"capture: wrote {OUT_PNG} ({OUT_PNG.stat().st_size} bytes, {img.size})")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        if SER_SOCK.exists():
            SER_SOCK.unlink()


if __name__ == "__main__":
    raise SystemExit(main())
