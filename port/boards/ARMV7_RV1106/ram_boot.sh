#!/bin/sh
# Luckfox metal — Rockchip Maskrom burn. No Linux, no ADB, no TFTP.
# 1) 471 DDR + official 472 usbplug
# 2) write metal.itb to NAND boot (LBA 0x800), env boot_fit
# 3) reset → U-Boot boot_fit (kernel_addr_r 0x8000, zImage + resource)
#
# Required:
#   LUCKFOX_IP     board IPv4 after metal boots
#   RKBIN          rockchip rkbin checkout
#   RKTOOLS        tree with rkdeveloptool (and mkimage for pack)
# Optional:
#   LUCKFOX_ETHADDR   default 72:00:00:00:00:01
#   METAL_MINILOADER  ddr usbplug blob (default $RKBIN/rv1106_download_*.bin)
set -eu

METAL_BIN=${1:?metal.bin}
IMG_DIR=$(CDPATH= cd -- "$(dirname -- "$METAL_BIN")" && pwd)
METAL_ITB=$IMG_DIR/metal.itb
METAL_UIMG=$IMG_DIR/metal.uimg
if [ -f "$METAL_ITB" ]; then
	IMG=$METAL_ITB
else
	IMG=$METAL_UIMG
fi

LUCKFOX_IP=${LUCKFOX_IP:?set LUCKFOX_IP to the board IPv4 after metal boots}
RKBIN=${RKBIN:?set RKBIN to a rockchip rkbin checkout}
RKTOOLS=${RKTOOLS:?set RKTOOLS to a tree that contains rkdeveloptool}

MINI=${METAL_MINILOADER:-}
if [ -z "$MINI" ]; then
	MINI=$(ls -1 "$RKBIN"/rv1106_download_*.bin 2>/dev/null | head -1 || true)
fi
RKTOOL=${RKTOOL:-}
if [ -z "$RKTOOL" ]; then
	for c in \
		"$RKTOOLS/root/usr/bin/rkdeveloptool" \
		"$RKTOOLS/usr/bin/rkdeveloptool" \
		"$RKTOOLS/rkdeveloptool"; do
		if [ -x "$c" ]; then
			RKTOOL=$c
			break
		fi
	done
fi

if [ ! -f "$IMG" ]; then
	echo "missing $IMG" >&2
	exit 1
fi
if [ -z "$RKTOOL" ] || [ ! -x "$RKTOOL" ] || [ -z "$MINI" ] || [ ! -f "$MINI" ]; then
	echo "need rkdeveloptool (RKTOOLS) and a usbplug blob (METAL_MINILOADER or RKBIN/rv1106_download_*.bin)" >&2
	exit 3
fi

echo "Maskrom usbplug, then NAND-write $IMG → boot @ LBA 0x800"
echo "prove: ping $LUCKFOX_IP"

n=0
while ! lsusb -d 2207:110c >/dev/null 2>&1 && ! lsusb -d 2207:110b >/dev/null 2>&1; do
	if [ $((n % 5)) -eq 0 ]; then
		echo "waiting for Maskrom 2207:110c (hold BOOT)"
	fi
	n=$((n + 1))
	sleep 1
done
"$RKTOOL" ld || true

echo "1/3 usbplug"
timeout 25 "$RKTOOL" boot "$MINI"
i=0
while [ "$i" -lt 15 ]; do
	if timeout 3 "$RKTOOL" test-device >/dev/null 2>&1; then
		echo "usbplug ready"
		break
	fi
	i=$((i + 1))
	sleep 1
done
if ! timeout 3 "$RKTOOL" test-device >/dev/null 2>&1; then
	echo "usbplug did not reach loader" >&2
	exit 4
fi

echo "2/3 write metal.itb + env"
BOARD_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$BOARD_DIR/pack_uboot_env.py" "$BOARD_DIR/metal.env" /tmp/luckfox-env-metal.bin
timeout 60 "$RKTOOL" write 0x800 "$IMG"
timeout 20 "$RKTOOL" write 0 /tmp/luckfox-env-metal.bin

echo "3/3 reset into U-Boot boot_fit"
timeout 8 "$RKTOOL" reboot || timeout 8 "$RKTOOL" reset || true

ok=0
i=0
while [ "$i" -lt 30 ]; do
	i=$((i + 1))
	if adb devices 2>/dev/null | grep -q '	device' || timeout 1 bash -c "echo >/dev/tcp/${LUCKFOX_IP}/22" >/dev/null 2>&1; then
		ok=0
		echo "wait $i: Linux"
		if [ "$i" -ge 20 ]; then
			exit 5
		fi
	elif ping -c 1 -W 1 "$LUCKFOX_IP" >/dev/null 2>&1; then
		ok=$((ok + 1))
		echo "wait $i: ping $ok"
		if [ "$ok" -ge 8 ]; then
			echo "metal up: ping $LUCKFOX_IP"
			exit 0
		fi
	else
		ok=0
		echo "wait $i: no ping"
	fi
	sleep 1
done
echo "NAND wrote metal.uimg; no ping after reset" >&2
exit 6
