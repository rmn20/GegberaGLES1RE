#!/usr/bin/env python3
"""Cross-compile and execute the production 32-bit JIT dispatch on ARM and x86.

Run with make native-dispatch-check; requires Clang, ld.lld and Unicorn.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_ARCH_X86, UC_MODE_ARM, UC_MODE_32
from unicorn import arm_const as ARM, x86_const as X86
from differential_fault_guard import install

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#include "gerbera/grState.h"
static Gerbera_s state;
static unsigned int stack[(GR_RENDER_STACK_SIZE + 3) / 4];
static void *observed;
static int failed;
static void marker(void *argument) { observed = argument; }
#undef grAssert
int grAssert(const char *expr, const char *file, int line) { failed = 1; return 1; }
void grDie(int value) { failed = 1; }
int test_dispatch(void)
{
#if defined(__arm__)
    const int scratch = 64;
#else
    const int scratch = 44;
#endif
    /* Pixel scratch offset in both original dispatch ABIs. */
    char *base = (char *)stack + 14644;
    state.renderStack = stack;
    state.pixelPipeline = base + scratch;
    state.pixelPipelineCode = marker;
    grExecutePixelPipeline(&state);
    return failed || observed != base;
}
"""


def run(target, work):
    cc = os.environ.get("CLANG", "clang")
    linker = os.environ.get("LLD", "ld.lld")
    flags = [cc, "-target", target, "-O2", "-std=c99", "-ffreestanding",
             "-fno-pic", "-fno-stack-protector", "-ffunction-sections", "-fdata-sections",
             "-Isymbian/include", "-Isrc/include"]
    fixture, dispatch, elf = (work / (target + suffix) for suffix in ("-fixture.o", "-dispatch.o", ".elf"))
    subprocess.run(flags + ["-x", "c", "-c", "-", "-o", str(fixture)],
                   input=FIXTURE, text=True, cwd=ROOT, check=True, timeout=10)
    subprocess.run(flags + ["-c", "src/gerbera/grPixelPipeline.c", "-o", str(dispatch)],
                   cwd=ROOT, check=True, timeout=10)
    subprocess.run([linker, "--gc-sections", "--entry=test_dispatch", "--image-base=0x10000",
                    str(fixture), str(dispatch), "-o", str(elf)], check=True, timeout=10)
    data = elf.read_bytes()
    assert data[:6] == b"\x7fELF\x01\x01"
    entry, phoff = struct.unpack_from("<II", data, 24)
    phsize, phcount = struct.unpack_from("<HH", data, 42)
    segments = [struct.unpack_from("<8I", data, phoff + index * phsize) for index in range(phcount)]
    segments = [segment for segment in segments if segment[0] == 1 and segment[5]]
    low = min(segment[2] for segment in segments) & ~4095
    high = (max(segment[2] + segment[5] for segment in segments) + 4095) & ~4095
    is_arm = target.startswith("arm")
    machine = Uc(UC_ARCH_ARM if is_arm else UC_ARCH_X86, UC_MODE_ARM if is_arm else UC_MODE_32)
    if is_arm:
        machine.ctl_set_cpu_model(ARM.UC_CPU_ARM_926 if target.startswith("armv5") else ARM.UC_CPU_ARM_1136_R2)
    machine.mem_map(low, high - low)
    for _, offset, address, _, size, _, _, _ in segments:
        machine.mem_write(address, data[offset:offset + size])
    stack, sentinel = 0x20000000, 0x30000000
    machine.mem_map(stack, 65536)
    machine.mem_map(sentinel, 4096)
    sp = stack + 65536 - 4096
    if is_arm:
        machine.reg_write(ARM.UC_ARM_REG_SP, sp)
        machine.reg_write(ARM.UC_ARM_REG_LR, sentinel)
        result_register, pc_register = ARM.UC_ARM_REG_R0, ARM.UC_ARM_REG_PC
    else:
        machine.reg_write(X86.UC_X86_REG_ESP, sp)
        machine.mem_write(sp, struct.pack("<I", sentinel))
        result_register, pc_register = X86.UC_X86_REG_EAX, X86.UC_X86_REG_EIP
    machine.emu_start(entry, sentinel, timeout=1000000, count=10000)
    assert machine.reg_read(pc_register) == sentinel, target + " did not return"
    assert machine.reg_read(result_register) == 0, target + " passed the wrong pixel stack pointer"
    print(target + ": production JIT dispatch uses the original scratch base")


if __name__ == "__main__":
    guard = install()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-armv5", action="store_true",
                        help="Also check the armv5-none-eabi target")
    args = parser.parse_args()
    targets = ("armv6j-none-eabi", "armv5-none-eabi", "i386-unknown-none") if args.include_armv5 else (
        "armv6j-none-eabi", "i386-unknown-none")
    with tempfile.TemporaryDirectory(prefix="gerbera-dispatch-") as directory:
        for architecture in targets:
            run(architecture, Path(directory))
