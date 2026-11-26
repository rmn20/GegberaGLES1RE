#!/usr/bin/env bash
# Cross-compiles the Win32 (i386 PE32) Release build of:
#   1. demo/win32/libGLES_CM.dll (+ libGLES_CM.lib + include/)
#   2. demo/win32/ClassiCube.exe (interactive Win32 GUI game using libGLES_CM.dll)
#   3. demo/ClassiCube-Gerbera-GLES1-Win32.zip
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEMO_DIR="${REPO_ROOT}/demo"
OUT_DIR="${DEMO_DIR}/win32"
CC_DIR="${CLASSICUBE_DIR:-/tmp/ClassiCube}"

# Locate `zig` (install portable wheel into /tmp/zigpkg if not already on PATH)
if command -v zig >/dev/null 2>&1; then
    ZIG_BIN="$(command -v zig)"
elif [ -x /tmp/zigpkg/ziglang/zig ]; then
    ZIG_BIN="/tmp/zigpkg/ziglang/zig"
else
    echo "[0/4] Installing zig toolchain into /tmp/zigpkg..."
    python3 -m pip install --quiet --target /tmp/zigpkg ziglang==0.13.0.post1
    ZIG_BIN="/tmp/zigpkg/ziglang/zig"
fi

mkdir -p "${OUT_DIR}/texpacks" "${OUT_DIR}/include/GLES" "${OUT_DIR}/include/EGL"

LIB_SRCS="$(make -s -C "${REPO_ROOT}" library-sources)"

echo "[1/4] Building Win32 Release libGLES_CM.dll (-O3 -DNDEBUG -DHG_NO_ASSERTS)..."
(
    cd "${REPO_ROOT}"
    "${ZIG_BIN}" cc -target x86-windows-gnu -O3 -DNDEBUG -DHG_NO_ASSERTS -DGERBERA_DLL_EXPORTS \
        -std=c99 -Wall -Wextra -Wno-unused-parameter -Isrc/include \
        -shared ${LIB_SRCS} \
        -Wl,--out-implib,"${OUT_DIR}/libGLES_CM.lib" -Wl,-s \
        -o "${OUT_DIR}/libGLES_CM.dll"
)

cp "${REPO_ROOT}/src/include/GLES/gl.h" "${REPO_ROOT}/src/include/GLES/egl.h" "${OUT_DIR}/include/GLES/"
cp "${REPO_ROOT}/src/include/EGL/egl.h" "${OUT_DIR}/include/EGL/"
cp "${REPO_ROOT}/src/gerbera/egl/libGLES_CM.def" "${OUT_DIR}/libGLES_CM.def"

if [ ! -d "${CC_DIR}/.git" ]; then
    echo "[2/4] Cloning ClassiCube into ${CC_DIR}..."
    rm -rf "${CC_DIR}"
    git clone --depth 1 https://github.com/ClassiCube/ClassiCube.git "${CC_DIR}"
fi

echo "[2/4] Applying Gerbera OpenGL ES 1.1 + Win32 EGL patch..."
rm -f "${CC_DIR}/src/Window_Gerbera.c" "${CC_DIR}/build/windows/src/Window_Gerbera.o"
git -C "${CC_DIR}" checkout -- misc/opengl/GLCommon.h src/Graphics_GL1.c src/_GLShared.h src/Server.c src/Window_Win.c
git -C "${CC_DIR}" apply --ignore-space-change --ignore-whitespace "${DEMO_DIR}/classicube_gles1.patch"

WRAPPER="/tmp/zig_win32_cc.sh"
printf '#!/usr/bin/env bash\nexec "%s" cc -target x86-windows-gnu "$@"\n' "${ZIG_BIN}" > "${WRAPPER}"
chmod +x "${WRAPPER}"

echo "[3/4] Building Win32 Release ClassiCube.exe (-O3, WINDOWS_GUI) linked against libGLES_CM.dll..."
make -C "${CC_DIR}" -f misc/makefiles/windows.mk -j"$(nproc)" \
    CC="${WRAPPER}" \
    WINDRES="" \
    RELEASE=1 \
    EXTRA_CFLAGS="-O3 -DNDEBUG -I${REPO_ROOT}/src/include -DCC_GFX_BACKEND=CC_GFX_BACKEND_GL1 -DCC_BUILD_GLES -DCC_BUILD_GERBERA_WIN32 -DGERBERA_DLL_IMPORTS -Wno-unused-command-line-argument" \
    LDFLAGS="-Wl,--subsystem,windows -Wl,-s" \
    LIBS="${OUT_DIR}/libGLES_CM.lib -lgdi32 -luser32 -lcomdlg32 -lwinmm -lws2_32 -lcrypt32"

cp "${CC_DIR}/ClassiCube.exe" "${OUT_DIR}/ClassiCube.exe"
cp "${CC_DIR}/misc/cc_textures.zip" "${OUT_DIR}/texpacks/default.zip"

echo "[4/4] Packaging ${DEMO_DIR}/ClassiCube-Gerbera-GLES1-Win32.zip..."
python3 - "${OUT_DIR}" "${DEMO_DIR}/ClassiCube-Gerbera-GLES1-Win32.zip" <<'PY'
import os, sys, zipfile
src_dir, zip_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, _, files in sorted(os.walk(src_dir)):
        for f in sorted(files):
            full = os.path.join(root, f)
            rel = os.path.relpath(full, os.path.dirname(src_dir))
            zf.write(full, rel)
print("Created", zip_path, f"({os.path.getsize(zip_path):,} bytes)")
PY

ls -lh "${OUT_DIR}" "${DEMO_DIR}/ClassiCube-Gerbera-GLES1-Win32.zip"
