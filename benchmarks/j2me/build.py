#!/usr/bin/env python3
"""Build a desktop JNI host for the Nokia M3G implementation in J2ME-Loader."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "benchmarks/arm/vendor/j2me-loader"
REVISION = "e270ed91e67b5cafc18c1991b92ba5249cf41c50"
BUILD = HERE / "build"


def run(args, **kwargs):
    subprocess.run([str(v) for v in args], check=True, **kwargs)


def java_home():
    if os.environ.get("JAVA_HOME"):
        home = Path(os.environ["JAVA_HOME"])
    elif platform.system() == "Darwin":
        home = Path(subprocess.check_output(
            ["/usr/libexec/java_home", "-a", platform.machine()], text=True).strip())
    else:
        executable = shutil.which("javac")
        if not executable:
            raise SystemExit("Install a JDK or set JAVA_HOME")
        home = Path(executable).resolve().parents[1]
    if not (home / "bin/javac").is_file():
        raise SystemExit("JAVA_HOME must select a JDK with javac, not only a JRE")
    return home


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SOURCE)
    args = parser.parse_args()
    source = args.source.resolve()
    if not source.is_dir():
        raise SystemExit("Clone https://github.com/nikita36078/J2ME-Loader.git into " + str(source) +
                         " and check out " + REVISION)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if revision != REVISION:
        raise SystemExit("Expected J2ME-Loader revision " + REVISION + "; got " + revision)
    BUILD.mkdir(parents=True, exist_ok=True)
    native = BUILD / "m3g-native"
    shutil.copytree(source / "app/src/main/cpp/m3g", native, dirs_exist_ok=True)
    # GL texture names remain 32-bit even when native object pointers are 64-bit.
    for relative in ("inc/m3g_interface.h", "src/m3g_interface.c"):
        path = native / relative
        text = path.read_text()
        old = "m3gDeleteGLTextures(Interface *m3g, M3Gsizei n, M3Gpointer *t)"
        if old not in text:
            raise SystemExit("Unexpected texture deletion declaration in " + relative)
        path.write_text(text.replace(old, "m3gDeleteGLTextures(Interface *m3g, M3Gsizei n, GLuint *t)"))
    home = java_home()
    system = platform.system()
    if system not in ("Darwin", "Linux"):
        raise SystemExit("This JNI host supports macOS and Linux")
    cc, cxx = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++")
    common = ["-O2", "-fPIC", "-DNDEBUG", "-DM3G_TARGET_ANDROID", "-DEGL_MATCH_NATIVE_PIXMAP=0x3041",
              "-I" + str(native / "inc"), "-I" + str(ROOT / "src/include"),
              "-I" + str(HERE / "include"), "-I" + str(home / "include"),
              "-I" + str(home / "include" / ("darwin" if system == "Darwin" else "linux"))]
    host_build = BUILD / "gerbera"
    run(["make", "-C", ROOT, "-j8", "BUILD=" + str(host_build),
         "CFLAGS=-O2 -fPIC -DNDEBUG -std=c99 -Isrc/include", "all"])
    objects = []
    inputs = [(native / "src/m3g_core.c", cc, ["-Wno-int-conversion", "-include", HERE / "trace_gl.h"]),
              (native / "src/m3g_android.cpp", cxx, []),
              (native / "src/m3g_android_gl.cpp", cxx, []),
              (native / "m3g_android_java_api.cpp", cxx, []),
              (native / "CSynchronization.cpp", cxx, []),
              (HERE / "trace_gl.c", cc, []), (HERE / "bench_jni.cpp", cxx, [])]
    for path, compiler, extra in inputs:
        obj = BUILD / (path.stem + ".o")
        run([compiler, *common, *extra, "-c", path, "-o", obj])
        objects.append(obj)
    extension = ".dylib" if system == "Darwin" else ".so"
    run([cxx, "-dynamiclib" if system == "Darwin" else "-shared", *objects,
         host_build / "libgerbera.a", "-lz", "-lm", "-o", BUILD / ("libjavam3g" + extension)])
    java_source = BUILD / "m3g-java"
    java_source.mkdir(exist_ok=True)
    for path in (source / "app/src/main/java/javax/microedition/m3g").glob("*.java"):
        text = path.read_text()
        if path.name == "Platform.java":
            text = text.replace('System.loadLibrary("c++_shared");', '').replace('System.loadLibrary("javam3g");', 'System.load(System.getProperty("bench.library"));')
        (java_source / path.name).write_text(text)
    classes = BUILD / "classes"
    classes.mkdir(exist_ok=True)
    javac = home / "bin/javac"
    version = subprocess.check_output([str(javac), "-version"], stderr=subprocess.STDOUT, text=True)
    java_flags = ["-source", "8", "-target", "8"] if version.startswith("javac 1.8.") else ["--release", "8"]
    run([javac, *java_flags, "-d", classes,
         *sorted((HERE / "java").rglob("*.java")), *sorted(java_source.glob("*.java")),
         HERE / "JarWorkloads.java", HERE / "BenchMain.java"])
    print("Built headless Nokia M3G JNI host")


if __name__ == "__main__":
    main()
