#!/usr/bin/env python3
"""Build current Gerbera with its Symbian unity flags as an ELF for Unicorn."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import shlex
import subprocess


HERE = Path(__file__).resolve().parent


def run(args: list[str]) -> str:
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(" ".join(args) + "\n" + result.stdout + result.stderr)
    if result.stderr:
        print(result.stderr, end="")
    return result.stdout


def build() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=HERE.parents[1])
    parser.add_argument("--build", type=Path, default=HERE / "build" / "gerbera")
    parser.add_argument("--compiler", default="arm-none-eabi-gcc")
    parser.add_argument("--runner", type=shlex.split, default=[], metavar="COMMAND",
                        help="Run compiler tools through a command prefix; paths must be visible unchanged")
    parser.add_argument("--march", default="armv6j", choices=("armv5t", "armv5te", "armv6j"))
    parser.add_argument("--include-armv5", action="store_true",
                        help="Allow building armv5t/armv5te targets")
    parser.add_argument("--define", action="append", default=[], metavar="NAME[=VALUE]")
    parser.add_argument("--cflag", action="append", default=[], metavar="FLAG",
                        help="Additional C compiler option, for example --cflag=-fno-schedule-insns")
    args = parser.parse_args()
    if args.march in ("armv5t", "armv5te") and not args.include_armv5:
        parser.error(f"--march {args.march} requires --include-armv5")
    cc = args.compiler if args.runner else shutil.which(args.compiler)
    if not cc:
        raise SystemExit(f"Compiler not found: {args.compiler}")
    def tool(command):
        return run(args.runner + command)

    nm = tool([cc, "-print-prog-name=nm"]).strip()
    root = args.source.resolve()
    out = args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    objdir = out / "obj"
    objdir.mkdir(exist_ok=True)
    arch = ["-march=" + args.march, "-mtune=arm1136jf-s", "-marm", "-mfloat-abi=soft"]
    flags = arch + ["-std=c99", "-O2", "-fno-pic", "-fno-PIE",
                    "-fvisibility=hidden", "-fdata-sections", "-ffunction-sections",
                    "-fno-builtin-printf", "-fno-builtin-fprintf",
                    "-fno-strict-aliasing", "-fno-asynchronous-unwind-tables",
                    "-funwind-tables", "-fomit-frame-pointer", "-DNDEBUG",
                    "-D__SYMBIAN32__", "-D__EPOC32__", "-D__MARM__", "-D__EABI__",
                    "-I" + str(root), "-I" + str(root / "symbian/include"),
                    "-I" + str(root / "src/include")] + ["-D" + value for value in args.define] + args.cflag
    sources = sorted(source for source in (root / "src").rglob("*.c")
                     if source.name != "hcX86Backend.c")
    unity = out / "gerbera_unity.c"
    unity.write_text('#include "GLES/gl.h"\n#include "GLES/egl.h"\n' +
                     "".join(f'#include "{source.relative_to(root)}"\n'
                             for source in sources))
    objects = []
    for name in ("symbian_entry.S", "arm_divmod.S", "symbian_rt.c"):
        source = root / "symbian" / name
        obj = objdir / (name + ".o")
        compile_flags = flags if source.suffix == ".c" else arch + ["-O2", "-fno-pic"]
        if name == "symbian_rt.c":
            # This file implements libc: do not turn its tail loops into calls
            # back to the very memset/memcpy function being compiled.
            compile_flags = compile_flags + ["-ffreestanding", "-fno-tree-loop-distribute-patterns"]
        tool([cc] + compile_flags + ["-c", str(source), "-o", str(obj)])
        objects.append(obj)
    obj = objdir / "gerbera_unity.o"
    print(f"Compiling {len(sources)} C files as one {args.march} unity with {cc}", flush=True)
    tool([cc] + flags + ["-finline-functions", "-fgcse-after-reload",
                       "-fpredictive-commoning", "-funswitch-loops", "-fwhole-program",
                       "-c", str(unity), "-o", str(obj)])
    objects.append(obj)
    combined = out / "renderer.o"
    tool([cc] + arch + ["-nostdlib", "-r", *map(str, objects), "-o", str(combined)])
    unresolved = sorted({line.split()[-1] for line in tool([nm, "-u", str(combined)]).splitlines()})
    linker_symbols = {"Image$$ER_RO$$Base", "Image$$ER_RO$$Limit",
                      ".ARM.exidx$$Base", ".ARM.exidx$$Limit"}
    exception_symbols = {"__aeabi_unwind_cpp_pr0", "__aeabi_unwind_cpp_pr1",
                         "__aeabi_unwind_cpp_pr2", "__cxa_begin_catch",
                         "__cxa_end_catch", "__cxa_end_cleanup", "_ZTI15XLeaveException"}
    imports = [name for name in unresolved if name not in linker_symbols]
    unexpected = [name for name in imports
                  if not name.startswith("sym_") and name not in exception_symbols]
    if unexpected:
        raise RuntimeError("Unresolved non-OS symbols: " + ", ".join(unexpected))
    stub = out / "os_stubs.S"
    lines = [".syntax unified", ".arch " + args.march, ".arm", '.section .bench_imports,"ax",%progbits',
             ".global bench_import_start", "bench_import_start:"]
    for name in imports:
        lines += [".align 2", f".global {name}", f".type {name}, %function",
                  f"{name}:", "bx lr", f".size {name}, . - {name}"]
    lines += [".global bench_import_end", "bench_import_end:"]
    stub.write_text("\n".join(lines) + "\n")
    stub_obj = objdir / "os_stubs.o"
    tool([cc] + arch + ["-c", str(stub), "-o", str(stub_obj)])
    linker = out / "elf.ld"
    linker.write_text('''OUTPUT_ARCH(arm)
ENTRY(_E32Dll)
SECTIONS {
  . = 0x10000;
  Image$$ER_RO$$Base = .;
  .text : { *(.init) *(.text .text.*) *(.glue_7t) *(.glue_7) }
  .bench_imports : { *(.bench_imports) }
  .rodata : { *(.rodata .rodata.*) *(.data.rel.ro .data.rel.ro.*) }
  .ARM.extab : { *(.ARM.extab*) }
  .ARM.exidx$$Base = .;
  .ARM.exidx : { *(.ARM.exidx*) }
  .ARM.exidx$$Limit = .;
  Image$$ER_RO$$Limit = .;
  . = ALIGN(4096);
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
  .init_array : { KEEP(*(.init_array .init_array.*)) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
    elf = out / "gerbera.elf"
    tool([cc] + arch + ["-nostdlib", "-static", "-Wl,-T," + str(linker),
                        "-Wl,-Map," + str(out / "gerbera.map"),
                        str(combined), str(stub_obj), "-o", str(elf)])
    print(f"Built {elf}; {len(imports)} named OS/exception stubs require emulator hooks")


if __name__ == "__main__":
    build()
