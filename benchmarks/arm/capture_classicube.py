#!/usr/bin/env python3
"""Capture a real headless ClassiCube landscape frame for ARM GLES replay."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import os


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REVISION = "c34b15782965e32a75a788ffcbe5c5a5a316e161"


def run(args: list[str], **kwargs: object) -> None:
    subprocess.run(args, check=True, **kwargs)


def make_recorder(source: Path, out: Path) -> tuple[Path, Path]:
    functions = set(re.findall(r"GL_FUNC\([^,]+,\s*(gl\w+)",
                              (source / "misc/opengl/GL1Funcs.h").read_text()))
    functions |= {"glBindBuffer", "glDeleteBuffers", "glGenBuffers",
                  "glBufferData", "glBufferSubData"}
    prototypes = re.findall(r"GL_API\s+(.+?)\s+GL_APIENTRY\s+(gl\w+)\((.*?)\);",
                            (ROOT / "src/include/GLES/gl.h").read_text())
    header = out / "trace_names.h"
    recorder = out / "trace_gl.c"
    text = r'''#include <GLES/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *events, *payload;
static unsigned frame;
static GLuint array_buffer, element_buffer;
static uint32_t float_word(float value) { uint32_t word; memcpy(&word, &value, 4); return word; }
static void init_trace(void) {
    if (events) return;
    events = fopen(getenv("CC_TRACE_EVENTS"), "wb");
    payload = fopen(getenv("CC_TRACE_PAYLOAD"), "wb");
    if (!events || !payload) { perror("trace output"); exit(2); }
}
static void record(const char* fn, unsigned argc, const uint32_t* args,
                   int blob_arg, const void* data, size_t size,
                   unsigned generated_count, const GLuint* generated) {
    init_trace();
    fprintf(events, "{\"fn\":\"%s\",\"frame\":%u,\"args\":[", fn, frame);
    for (unsigned i = 0; i < argc; ++i) fprintf(events, "%s%u", i ? "," : "", args[i]);
    fputs("]", events);
    if (data && size) {
        long offset = ftell(payload);
        if (offset < 0 || fwrite(data, 1, size, payload) != size) { perror("trace payload"); exit(2); }
        fprintf(events, ",\"blob\":[%d,%ld,%zu]", blob_arg, offset, size);
    }
    if (generated) {
        fputs(",\"generated\":[", events);
        for (unsigned i = 0; i < generated_count; ++i)
            fprintf(events, "%s%u", i ? "," : "", generated[i]);
        fputs("]", events);
    }
    fputs("}\n", events);
}
void cc_trace_mark(void) { record("capture", 0, NULL, -1, NULL, 0, 0, NULL); }
void cc_trace_frame_end(void) {
    record("frame_end", 0, NULL, -1, NULL, 0, 0, NULL);
    fflush(events); fflush(payload); frame++;
}
static size_t texture_size(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        fprintf(stderr, "Unexpected captured texture format %x/%x\n", format, type); exit(3);
    }
    return (size_t)width * height * 4;
}
'''
    macros = []
    for result, name, arguments in prototypes:
        if name not in functions:
            continue
        macros.append(f"#define {name} trace_{name}\n")
        params = [] if arguments.strip() == "void" else arguments.split(",")
        names = [re.search(r"(\w+)\s*$", param).group(1) for param in params]
        call = f'{name}({", ".join(names)})'
        text += f"\n{result} trace_{name}({arguments}) {{\n"
        if name.startswith("glGet") or name == "glReadPixels":
            text += f"    {'return ' if result != 'void' else ''}{call};\n}}\n"
            continue
        values = []
        for param, pname in zip(params, names):
            if "*" in param:
                values.append(f"(uint32_t)(uintptr_t){pname}")
            elif "GLfloat" in param or "GLclampf" in param:
                values.append(f"float_word({pname})")
            else:
                values.append(f"(uint32_t){pname}")
        blob = "-1, NULL, 0"
        generated = "0, NULL"
        pointer_index = None
        if name == "glLoadMatrixf":
            blob = "0, m, 64"; pointer_index = 0
        elif name == "glFogfv":
            blob = "1, params, pname == GL_FOG_COLOR ? 16 : 4"; pointer_index = 1
        elif name in ("glDeleteTextures", "glDeleteBuffers"):
            blob = f"1, {names[1]}, (size_t)n * 4"; pointer_index = 1
        elif name in ("glGenTextures", "glGenBuffers"):
            generated = f"n, {names[1]}"; pointer_index = 1
            text += f"    {call};\n"
        elif name == "glBufferData":
            blob = "2, data, (size_t)size"; pointer_index = 2
        elif name == "glBufferSubData":
            blob = "3, data, (size_t)size"; pointer_index = 3
        elif name in ("glTexImage2D", "glTexSubImage2D"):
            blob = "8, pixels, texture_size(width, height, format, type)"; pointer_index = 8
        elif name in ("glVertexPointer", "glColorPointer", "glTexCoordPointer"):
            text += '    if (!array_buffer) { fputs("Client arrays need capture support\\n", stderr); exit(3); }\n'
        elif name == "glDrawElements":
            text += '    if (!element_buffer) { fputs("Client indices need capture support\\n", stderr); exit(3); }\n'
        elif name == "glBindBuffer":
            text += "    if (target == GL_ARRAY_BUFFER) array_buffer = buffer;\n"
            text += "    if (target == GL_ELEMENT_ARRAY_BUFFER) element_buffer = buffer;\n"
        if pointer_index is not None:
            values[pointer_index] = "0"
        text += f'    uint32_t args[] = {{{", ".join(values) if values else "0"}}};\n'
        text += f'    record("{name}", {len(names)}, args, {blob}, {generated});\n'
        if not name.startswith("glGen"):
            text += f"    {call};\n"
        text += "}\n"
    header.write_text("".join(macros))
    recorder.write_text(text)
    return header, recorder


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=HERE / "vendor/classicube")
    parser.add_argument("--build", type=Path, default=HERE / "build/classicube")
    parser.add_argument("--capture", type=Path, default=HERE / "classicube")
    parser.add_argument("--seed", type=int, default=20261122)
    args = parser.parse_args()
    source, out, capture = args.source.resolve(), args.build.resolve(), args.capture.resolve()
    out.mkdir(parents=True, exist_ok=True)
    capture.mkdir(parents=True, exist_ok=True)
    if not (source / ".git").exists():
        run(["git", "clone", "--depth", "1", "https://github.com/ClassiCube/ClassiCube.git", str(source)])
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if revision != REVISION:
        raise SystemExit(f"Expected reviewed ClassiCube revision {REVISION}, got {revision}")
    paths = ["misc/opengl/GLCommon.h", "src/Graphics_GL1.c", "src/Server.c",
             "src/Window_Win.c", "src/_GLShared.h", "src/Core.h", "src/Platform_Posix.c",
             "src/Http_Worker.c"]
    run(["git", "restore", "--", *paths], cwd=source)
    run(["git", "apply", "--ignore-space-change", "--ignore-whitespace",
         str(ROOT / "demo/classicube_gles1.patch")], cwd=source)
    core = source / "src/Core.h"
    text = core.read_text().replace("#define CC_BUILD_PLUGINS\n", "")
    core.write_text(text)
    http = source / "src/Http_Worker.c"
    http.write_text(http.read_text().replace("#elif !defined CC_BUILD_NETWORKING", "#elif 1"))
    platform = source / "src/Platform_Posix.c"
    text = platform.read_text().replace("TransformProcessType(&psn, kProcessTransformToForegroundApplication);", "(void)psn;")
    platform.write_text(text)
    window = (ROOT / "demo/Window_Gerbera.c").read_text()
    window = window.replace("static int g_winWidth  = 960;", "static int g_winWidth  = 320;")
    window = window.replace("static int g_winHeight = 540;", "static int g_winHeight = 240;")
    window = window.replace("Game_SetViewDistance(128);", "Game_SetViewDistance(64);")
    window = window.replace("g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);",
                            "egliSetDefaultDisplayMode(EGL_SYMBIAN_ECOLOR16MU);\n\tg_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);")
    window = window.replace("static int     g_worldFrames;", "extern void cc_trace_mark(void);\nextern void cc_trace_frame_end(void);\nstatic int     g_worldFrames;")
    window = window.replace("g_pendingShotPath = pathBuf;", "g_pendingShotPath = pathBuf;\n\tcc_trace_mark();")
    window = window.replace("\tFlushPendingScreenshot();\n\treturn eglSwapBuffers", "\tcc_trace_frame_end();\n\tif (g_pendingShotPath) { puts(\"ClassiCube frame captured\"); exit(0); }\n\treturn eglSwapBuffers")
    (source / "src/Window_Gerbera.c").write_text(window)
    header, recorder = make_recorder(source, out)
    run(["make", "-C", str(ROOT), "BUILD=benchmarks/arm/build/host_gerbera", "-j8",
         "CFLAGS=-O2 -DNDEBUG -std=c99 -Isrc/include", "all"])
    recorder_obj = out / "trace_gl.o"
    run(["clang", "-O2", "-DNDEBUG", "-I" + str(ROOT / "src/include"),
         "-c", str(recorder), "-o", str(recorder_obj)])
    flags = (f"-O2 -DNDEBUG -I{ROOT / 'src/include'} -include {header} "
             "-DCC_WIN_BACKEND=99 -DCC_GFX_BACKEND=CC_GFX_BACKEND_GL1 "
             "-DCC_BUILD_GLES -DCC_BUILD_EGL -DCC_AUD_BACKEND=CC_AUD_BACKEND_NULL")
    exe = out / "ClassiCube"
    run(["make", "-f", "misc/makefiles/macos.mk", "-j8", str(exe),
         "TARGET=" + str(exe), "BUILD_DIR=" + str(out / "obj"), "CC=clang", "RELEASE=1",
         "EXTRA_CFLAGS=" + flags, "OBJECTS=" + str(recorder_obj),
         "LIBS=" + str(ROOT / "benchmarks/arm/build/host_gerbera/libgerbera.a") +
         " -framework Security -framework Cocoa -framework IOKit -lobjc"], cwd=source)
    (out / "texpacks").mkdir(exist_ok=True)
    for name in ("default.zip", "classicube.zip"):
        shutil.copyfile(source / "misc/cc_textures.zip", out / "texpacks" / name)
    env = dict(os.environ, CC_SEED=str(args.seed), CC_DEMO_DIR=str(capture),
               CC_TRACE_EVENTS=str(capture / "landscape.jsonl"),
               CC_TRACE_PAYLOAD=str(capture / "landscape.bin"))
    run([str(exe), "--singleplayer"], cwd=out, env=env, timeout=300)
    print(f"Captured ClassiCube {revision}, seed {args.seed}, 320x240, view distance 64 to {capture}")


if __name__ == "__main__":
    main()
