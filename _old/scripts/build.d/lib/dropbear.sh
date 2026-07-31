#!/usr/bin/env bash
# Build freestanding Dropbear static lib for Metal BIOS/EFI link.
# Usage: dropbear.sh <i386|x86_64>  -> build/dropbear/<arch>/libdropbear_metal.a
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
ARCH="${1:-i386}"
case "${ARCH}" in
i386|x86_64) ;;
*)
	echo "dropbear: unknown ARCH=${ARCH}" >&2
	exit 1
	;;
esac

DB="${ROOT}/external/dropbear"
SRC="${DB}/src"
METAL_DB="${ROOT}/src/pymergetic/metal/net/ssh/dropbear_metal"
STUBS="${ROOT}/src/pymergetic/metal/net/ssh/dropbear_stubs"
# EFI X64 needs PIC; BIOS freestanding uses no-pic. Keep separate OUT dirs.
PIC_TAG=""
if [[ "${PM_METAL_DROPBEAR_PIC:-0}" == "1" ]]; then
	PIC_TAG="-pic"
	PICFLAGS=(-fPIC)
else
	PICFLAGS=(-fno-pic -fno-pie)
fi
OUT="${ROOT}/build/dropbear/${ARCH}${PIC_TAG}"
OBJ="${OUT}/obj"
CC="${CC:-clang}"

if [[ ! -d "${DB}/.git" && ! -f "${SRC}/session.h" ]]; then
	echo "dropbear: missing external/dropbear — run ./scripts/setup dropbear" >&2
	exit 1
fi

# Ensure Metal patches applied (idempotent check).
if ! grep -q 'DROPBEAR_METAL' "${SRC}/session.h"; then
	shopt -s nullglob
	for patch in "${ROOT}"/patches/dropbear/*.patch; do
		echo "dropbear: apply $(basename "${patch}")"
		git -C "${DB}" apply "${patch}" 2>/dev/null || patch -d "${DB}" -p1 <"${patch}"
	done
	shopt -u nullglob
fi

if [[ "${ARCH}" == "i386" ]]; then
	MFLAG="-m32"
	MARCH=(-march=i686)
else
	MFLAG="-m64"
	MARCH=()
fi

mkdir -p "${OBJ}"
# Generate default_options_guard if missing in metal dir
if [[ ! -f "${METAL_DB}/default_options_guard.h" ]]; then
	"${SRC}/ifndef_wrapper.sh" <"${SRC}/default_options.h" >"${METAL_DB}/default_options_guard.h.tmp"
	{
		printf '%s\n' '/* generated for Metal */'
		cat "${METAL_DB}/default_options_guard.h.tmp"
	} >"${METAL_DB}/default_options_guard.h"
	rm -f "${METAL_DB}/default_options_guard.h.tmp"
fi

CLANG_RES="$("${CC}" "${MFLAG}" -print-resource-dir)/include"
HOST_STUBS="${ROOT}/src/pymergetic/metal/runtime/mem/host_stubs"

CFLAGS=(
	-std=gnu11
	-ffreestanding
	-fno-stack-protector
	"${PICFLAGS[@]}"
	"${MFLAG}"
	"${MARCH[@]}"
	-Os
	-Wall
	-Wno-error
	-Wno-unused-parameter
	-Wno-unused-variable
	-Wno-unused-function
	-Wno-invalid-noreturn
	-Wno-sign-compare
	-Wno-missing-field-initializers
	-Wno-pointer-sign
	-Wno-format
	-fno-strict-aliasing
	-DDROPBEAR_SERVER=1
	-DDROPBEAR_CLIENT=0
	-DLOCALOPTIONS_H_EXISTS=1
	-DDROPBEAR_METAL=1
	-DBUNDLED_LIBTOM=1
	-DUSE_LTM
	-DLTM_DESC
	-U__linux__
	-Ulinux
	-U__gnu_linux__
	-nostdinc
	-isystem "${STUBS}"
	-isystem "${HOST_STUBS}"
	-isystem "${CLANG_RES}"
	-I"${METAL_DB}"
	-I"${SRC}"
	-I"${DB}/libtomcrypt/src/headers"
	-I"${DB}/libtommath"
	-I"${ROOT}/include"
	-I"${ROOT}/src/pymergetic/metal"
	-I"${ROOT}"
)

# LTC library TUs need LTC_SOURCE so mp_* macros in tomcrypt_math.h expand.
LTC_CFLAGS=("${CFLAGS[@]}" -DLTC_SOURCE)

DB_SRCS=(
	atomicio.c buffer.c dbhelpers.c dbmalloc.c dbutil.c dbrandom.c
	bignum.c curve25519.c ed25519.c sk-ed25519.c
	signkey.c rsa.c dss.c ecc.c ecdsa.c sk-ecdsa.c
	ltc_prng.c crypto_desc.c gensignkey.c gened25519.c genrsa.c gendss.c
	queue.c compat.c fake-rfc2553.c
	common-session.c packet.c common-algo.c common-kex.c
	common-channel.c common-chansession.c termcodes.c loginrec.c
	tcp-accept.c listener.c process-packet.c dh_groups.c
	common-runopts.c circbuffer.c list.c netio.c chachapoly.c gcm.c
	svr-kex.c svr-auth.c sshpty.c
	svr-authpasswd.c
	svr-authpubkey.c
	svr-authsslcert.c
	svr-session.c svr-service.c svr-chansession.c svr-runopts.c
	svr-tcpfwd.c
)

OBJS=()
echo "dropbear (${ARCH}): compiling Dropbear sources"
for f in "${DB_SRCS[@]}"; do
	src="${SRC}/${f}"
	[[ -f "${src}" ]] || continue
	base="$(basename "${f}" .c)"
	obj="${OBJ}/db-${base}.o"
	if [[ ! -f "${obj}" || "${src}" -nt "${obj}" || "${METAL_DB}/localoptions.h" -nt "${obj}" ]]; then
		if ! "${CC}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"; then
			echo "dropbear: compile failed: ${src}" >&2
			exit 1
		fi
	fi
	OBJS+=("${obj}")
done

# libtommath — all bn_*.c
echo "dropbear (${ARCH}): compiling libtommath"
while IFS= read -r -d '' src; do
	base="$(basename "${src}" .c)"
	obj="${OBJ}/ltm-${base}.o"
	if [[ ! -f "${obj}" || "${src}" -nt "${obj}" ]]; then
		"${CC}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
	fi
	OBJS+=("${obj}")
done < <(find "${DB}/libtommath" -maxdepth 1 -name 'bn_*.c' -print0 | sort -z)

# libtomcrypt — all .c under src (ifdef-empty when disabled).
# Skip *tab.c: tables #include'd by sibling sources (no headers of their own).
echo "dropbear (${ARCH}): compiling libtomcrypt"
while IFS= read -r -d '' src; do
	base="$(basename "${src}" .c)"
	case "${base}" in
	*tab) continue ;;
	esac
	# disambiguate path collisions
	hash="$(printf '%s' "${src}" | md5sum | cut -c1-6)"
	obj="${OBJ}/ltc-${base}-${hash}.o"
	if [[ ! -f "${obj}" || "${src}" -nt "${obj}" ]]; then
		if ! "${CC}" "${LTC_CFLAGS[@]}" -c "${src}" -o "${obj}"; then
			echo "dropbear: compile failed: ${src}" >&2
			exit 1
		fi
	fi
	OBJS+=("${obj}")
done < <(find "${DB}/libtomcrypt/src" -name '*.c' -print0 | sort -z)

AR="${AR:-ar}"
LIB="${OUT}/libdropbear_metal.a"
rm -f "${LIB}"
"${AR}" rcs "${LIB}" "${OBJS[@]}"

# Avoid collisions with MicroPython (m_malloc, mp_init) / hashlib sha256_*.
echo "dropbear (${ARCH}): renaming conflicting symbols in archive"
TMPA="${OUT}/redef"
rm -rf "${TMPA}"
mkdir -p "${TMPA}"
(
	cd "${TMPA}"
	"${AR}" x "${LIB}"
	for o in *.o; do
		objcopy \
			--redefine-sym m_malloc=db_m_malloc \
			--redefine-sym m_realloc=db_m_realloc \
			--redefine-sym m_free=db_m_free \
			--redefine-sym m_strdup=db_m_strdup \
			--redefine-sym m_calloc=db_m_calloc \
			--redefine-sym sha256_init=ltc_sha256_init \
			--redefine-sym sha256_process=ltc_sha256_process \
			--redefine-sym sha256_done=ltc_sha256_done \
			--redefine-sym sha256_final=ltc_sha256_final \
			--redefine-sym mp_init=ltm_mp_init \
			"${o}" "${o}.new" && mv "${o}.new" "${o}"
	done
	rm -f "${LIB}"
	"${AR}" rcs "${LIB}" *.o
)
rm -rf "${TMPA}"

echo "dropbear: ok -> ${LIB} (${#OBJS[@]} objs)"
ls -la "${LIB}"
