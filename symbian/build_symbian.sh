#!/usr/bin/env bash
# build_symbian.sh - Build Gerbera OpenGL ES 1.1 + EGL 1.1 as a native Symbian
# OS v9.x (EKA2 / S60 3rd Ed+) ARM EABI DLL (libgles_cm.dll + libgles_cm.dso).
#
# Uses klaxons1/gcc-symbian and SymbianRevive/elf2e32 on Linux.
# Missing tools are downloaded or built under SYMBIAN_TOOLS_DIR.
# The packaged compiler needs x86-64 Linux with glibc 2.38 or newer.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="${REPO_ROOT}/symbian_tools"
GCC_DIR="${SYMBIAN_GCC_DIR:-${TOOLS_DIR}/gcc}"
ELF2E32="${SYMBIAN_ELF2E32:-${TOOLS_DIR}/elf2e32_bin}"
BUILD_DIR="${SYMBIAN_BUILD_DIR:-/tmp/gerbera_symbian_build}"
OUT_DIR="${SYMBIAN_OUT_DIR:-${REPO_ROOT}/symbian/out}"
ARM_ARCH="${SYMBIAN_ARM_ARCH:-armv6j}"
case "${ARM_ARCH}" in
    armv5t|armv5te) ARCHIVE_ARCH=ARMv5 ;;
    armv6j) ARCHIVE_ARCH=ARMv6 ;;
    *) echo "Unsupported SYMBIAN_ARM_ARCH: ${ARM_ARCH}" >&2; exit 2 ;;
esac

# Bootstrap missing compiler and image-conversion tools.
if [[ ! -x "${GCC_DIR}/bin/arm-none-symbianelf-gcc" ]]; then
    echo "[symbian] Downloading arm-none-symbianelf-gcc 14.2.0..."
    mkdir -p "${TOOLS_DIR}"
    python - "${TOOLS_DIR}" "${GCC_DIR}" <<'PY'
import hashlib
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import urllib.request

tools_dir, gcc_dir = map(Path, sys.argv[1:])
revision = "2a59df9e2824ed6721988240e506f52d7cf8591f"
name = "cc3d-symbian-gcc-14.2.0-linux-x86_64.tar.xz"
expected = "6bc3a4d3c7eec317c1dfe21b5cb71cefed8c27b55683a950ff4bede478c3c6c9"
url = f"https://raw.githubusercontent.com/klaxons1/gcc-symbian/{revision}/bin/{name}"
archive = tools_dir / name
if gcc_dir.exists():
    raise SystemExit(f"Compiler directory exists without arm-none-symbianelf-gcc: {gcc_dir}")
if not archive.exists():
    with tempfile.NamedTemporaryFile(dir=tools_dir) as output:
        with urllib.request.urlopen(url) as response:
            shutil.copyfileobj(response, output)
        output.flush()
        shutil.copyfile(output.name, archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    raise SystemExit(f"Compiler archive checksum mismatch: {archive}")
with tempfile.TemporaryDirectory(prefix="gcc-extract-", dir=tools_dir) as temporary:
    with tarfile.open(archive) as package:
        package.extractall(temporary)
    gcc_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(Path(temporary) / "gcc"), gcc_dir)

PY
fi

if [[ ! -x "${ELF2E32}" ]]; then
    echo "[symbian] Building SymbianRevive/elf2e32..."
    if [[ ! -d "${TOOLS_DIR}/elf2e32" ]]; then
        git clone --depth 1 https://github.com/SymbianRevive/elf2e32.git "${TOOLS_DIR}/elf2e32"
    fi
    g++ -std=c++14 -O2 -D__LINUX__ -fpermissive -w \
        -I"${TOOLS_DIR}/elf2e32/include" -I"${TOOLS_DIR}/elf2e32/source" \
        "${TOOLS_DIR}/elf2e32"/source/*.cpp -o "${ELF2E32}"
fi

CC="${GCC_DIR}/bin/arm-none-symbianelf-gcc"
SIZE="${GCC_DIR}/bin/arm-none-symbianelf-size"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/obj" "${BUILD_DIR}/implibs" "${OUT_DIR}"

# Generate Symbian OS import libraries from the ordinal table.
echo "[symbian] Generating Symbian EKA2 OS import libraries (.dso)..."
python "${REPO_ROOT}/symbian/gen_implibs.py" "${ELF2E32}" "${BUILD_DIR}/implibs"

# Compile the freestanding runtime and startup assembly.
COMMON_FLAGS=(
    -std=c99
    -march="${ARM_ARCH}"
    -mtune=arm1136jf-s
    -marm
    -mfloat-abi=soft
    -fno-pic
    -fno-PIE
    -fvisibility=hidden
    -fdata-sections
    -ffunction-sections
    -fno-builtin-printf
    -fno-builtin-fprintf
    -fno-strict-aliasing
    -fno-asynchronous-unwind-tables
    -funwind-tables
    -fomit-frame-pointer
    -DNDEBUG
    -D__SYMBIAN32__
    -D__EPOC32__
    -D__MARM__
    -D__EABI__
    -I"${REPO_ROOT}"
    -I"${REPO_ROOT}/symbian/include"
    -I"${REPO_ROOT}/src/include"
)

echo "[symbian] Assembling EKA2 startup, TLS, setjmp & CLZ divmod..."
"${CC}" -march="${ARM_ARCH}" -marm -mfloat-abi=soft -O2 -fno-pic \
    -c "${REPO_ROOT}/symbian/symbian_entry.S" -o "${BUILD_DIR}/obj/symbian_entry.o"
"${CC}" -march="${ARM_ARCH}" -marm -mfloat-abi=soft -O2 -fno-pic \
    -c "${REPO_ROOT}/symbian/arm_divmod.S" -o "${BUILD_DIR}/obj/arm_divmod.o"
"${CC}" "${COMMON_FLAGS[@]}" -O2 -ffreestanding -fno-tree-loop-distribute-patterns \
    -c "${REPO_ROOT}/symbian/symbian_rt.c" -o "${BUILD_DIR}/obj/symbian_rt.o"

# Combine the renderer sources for whole-program optimization.
UNITY_C="${BUILD_DIR}/gerbera_symbian_unity.c"
{
    echo '#include "GLES/gl.h"'
    echo '#include "GLES/egl.h"'
    while IFS= read -r src; do
        if [[ "$(basename "${src}")" == "hcX86Backend.c" ]]; then
            continue
        fi
        rel="${src#"${REPO_ROOT}"/}"
        echo "#include \"${rel}\""
    done < <(find "${REPO_ROOT}/src" -name '*.c' | sort)
} > "${UNITY_C}"

echo "[symbian] Compiling Gerbera unity for ${ARM_ARCH}, tuned for ARM1136JF-S..."
"${CC}" "${COMMON_FLAGS[@]}" -O2 -finline-functions -fgcse-after-reload -fpredictive-commoning -funswitch-loops -fwhole-program \
    -c "${UNITY_C}" -o "${BUILD_DIR}/obj/gerbera_unity.o"

# Reject writable static data and unresolved symbols at link time.
echo "[symbian] Linking Symbian EABI ELF..."
DSOS=(
    "${BUILD_DIR}/implibs/euser{000a0000}.dso"
    "${BUILD_DIR}/implibs/drtaeabi{000a0000}.dso"
    "${BUILD_DIR}/implibs/ws32{000a0000}.dso"
    "${BUILD_DIR}/implibs/fbscli{000a0000}.dso"
    "${BUILD_DIR}/implibs/cone{000a0000}.dso"
    "${BUILD_DIR}/implibs/gdi{000a0000}.dso"
    "${BUILD_DIR}/implibs/hal{000a0000}.dso"
)

"${CC}" \
    -march="${ARM_ARCH}" -mtune=arm1136jf-s -marm -mfloat-abi=soft \
    -O2 -fno-pic \
    -nostdlib -shared \
    -Wl,-T,"${REPO_ROOT}/symbian/symbian_dll.ld" \
    -Wl,--entry,_E32Dll \
    -Wl,--version-script="${REPO_ROOT}/symbian/libgles_cm.ver" \
    -Wl,-Bsymbolic \
    -Wl,--gc-sections \
    -Wl,--no-undefined \
    -Wl,-Map="${BUILD_DIR}/libgles_cm.map" \
    "${BUILD_DIR}/obj/symbian_entry.o" \
    "${BUILD_DIR}/obj/arm_divmod.o" \
    "${BUILD_DIR}/obj/symbian_rt.o" \
    "${BUILD_DIR}/obj/gerbera_unity.o" \
    "${DSOS[@]}" \
    -o "${BUILD_DIR}/libgles_cm.sym"

"${SIZE}" "${BUILD_DIR}/libgles_cm.sym"

# Convert the ELF shared object to an E32Image and import library.
echo "[symbian] Running elf2e32 -> libgles_cm.dll & libgles_cm.dso..."
"${ELF2E32}" \
    --targettype=DLL \
    --elfinput="${BUILD_DIR}/libgles_cm.sym" \
    --definput="${REPO_ROOT}/symbian/libgles_cm.def" \
    --output="${OUT_DIR}/libgles_cm.dll" \
    --dso="${OUT_DIR}/libgles_cm{000a0000}.dso" \
    --defoutput="${OUT_DIR}/libgles_cm.def" \
    --libpath="${BUILD_DIR}" \
    --linkas="libgles_cm{000a0000}.dll" \
    --uid1=0x10000079 \
    --uid2=0x1000008d \
    --uid3=0x101fcabd \
    --sid=0x101fcabd \
    --vid=0x00000000 \
    --version=10.0 \
    --capability=ALL-TCB \
    --fpu=softvfp \
    --uncompressed

cp -f "${OUT_DIR}/libgles_cm{000a0000}.dso" "${OUT_DIR}/libgles_cm.dso"

python - "${OUT_DIR}" "${REPO_ROOT}" "${ARCHIVE_ARCH}" <<'PY'
import os, sys, zipfile
out_dir, repo_root, arch = sys.argv[1:]
zip_path = os.path.join(out_dir, f"Gerbera-GLES1-Symbian-EKA2-{arch}.zip")
with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    zf.write(os.path.join(out_dir, "libgles_cm.dll"), "sys/bin/libgles_cm.dll")
    zf.write(os.path.join(out_dir, "libgles_cm.dso"), "epoc32/release/armv5/lib/libgles_cm.dso")
    zf.write(os.path.join(out_dir, "libgles_cm{000a0000}.dso"), "epoc32/release/armv5/lib/libgles_cm{000a0000}.dso")
    zf.write(os.path.join(out_dir, "libgles_cm.def"), "epoc32/include/def/eabi/libgles_cmU.def")
    zf.write(os.path.join(repo_root, "src/include/GLES/gl.h"), "epoc32/include/GLES/gl.h")
    zf.write(os.path.join(repo_root, "src/include/GLES/egl.h"), "epoc32/include/GLES/egl.h")
PY

echo "[symbian] Build complete:"
ls -lh "${OUT_DIR}/libgles_cm.dll" "${OUT_DIR}/libgles_cm.dso" "${OUT_DIR}/Gerbera-GLES1-Symbian-EKA2-${ARCHIVE_ARCH}.zip"
