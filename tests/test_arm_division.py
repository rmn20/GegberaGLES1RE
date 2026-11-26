#!/usr/bin/env python3
"""Check ARM division, remainder and edge-slope rounding against integer arithmetic."""
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as A
from differential_fault_guard import install

ROOT = Path(__file__).resolve().parents[1]
PREFIX = os.environ.get("ARM_TOOL_PREFIX", "arm-none-eabi-")
MASK = 0xffffffff
PROBE = r"""
.syntax unified
.arm
.text
.global division_probe
.type division_probe, %function
division_probe:
    push {r4-r11, lr}
    sub sp, sp, #28
    stm sp, {r0-r3}
1:
    ldr r2, [sp, #4]
    ldm r2, {r0, r1}
    mov r4, #4
    mov r5, #5
    mov r6, #6
    mov r7, #7
    mov r8, #8
    mov r9, #9
    mov r10, #10
    mov r11, #11
    ldr ip, [sp]
    blx ip
    cmp r4, #4
    cmpeq r5, #5
    cmpeq r6, #6
    cmpeq r7, #7
    cmpeq r8, #8
    cmpeq r9, #9
    cmpeq r10, #10
    cmpeq r11, #11
    bne 2f
    ldr r2, [sp, #8]
    stmia r2!, {r0, r1}
    str r2, [sp, #8]
    ldr r2, [sp, #4]
    add r2, r2, #8
    str r2, [sp, #4]
    ldr r3, [sp, #12]
    subs r3, r3, #1
    str r3, [sp, #12]
    bne 1b
    add sp, sp, #28
    pop {r4-r11, pc}
2:  udf #0
"""


def build(work):
    source, probe = work / "division.c", work / "probe.S"
    source.write_text('''#include "src/hg/hgInt32.c"
#include "src/imath/imInline.c"
#include "src/gerbera/grRasterizer.c"
''')
    probe.write_text(PROBE)
    flags = [PREFIX + "gcc", "-march=armv6j", "-mtune=arm1136jf-s", "-marm",
             "-mfloat-abi=soft", "-O2", "-ffreestanding", "-fno-unwind-tables",
             "-ffunction-sections", "-fdata-sections", "-DNDEBUG", "-I" + str(ROOT),
             "-I" + str(ROOT / "symbian/include"), "-I" + str(ROOT / "src/include")]
    objects = []
    for index, path in enumerate((source, probe, ROOT / "symbian/arm_divmod.S")):
        obj = work / f"{index}.o"
        subprocess.run(flags + ["-c", str(path), "-o", str(obj)], check=True, timeout=15)
        objects.append(str(obj))
    functions = ("__aeabi_uidiv", "__aeabi_uidivmod", "__aeabi_idiv", "__aeabi_idivmod",
                 "grDivsu", "imRcpu")
    elf, binary = work / "division.elf", work / "division.bin"
    subprocess.run([PREFIX + "ld", "-Ttext=0x10000", "--entry=division_probe", "--gc-sections",
                    *["--undefined=" + name for name in functions], *objects, "-o", str(elf)],
                   check=True, timeout=10)
    subprocess.run([PREFIX + "objcopy", "-O", "binary", str(elf), str(binary)], check=True, timeout=10)
    symbols = {fields[2]: int(fields[0], 16) for line in
               subprocess.check_output([PREFIX + "nm", str(elf)], text=True, timeout=10).splitlines()
               if len(fields := line.split()) == 3}
    return binary.read_bytes(), symbols


def signed(value):
    return value - (1 << 32) if value & (1 << 31) else value


def main():
    with tempfile.TemporaryDirectory(prefix="gerbera-division-") as directory:
        code, symbols = build(Path(directory))
    machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    machine.ctl_set_cpu_model(A.UC_CPU_ARM_1136)
    machine.mem_map(0x10000, (len(code) + 4095) & ~4095)
    machine.mem_write(0x10000, code)
    machine.mem_map(0x100000, 0x40000)
    source, output, stack, sentinel = 0x100000, 0x110000, 0x13f000, 0x13fff0

    def execute(name, pairs):
        for start in range(0, len(pairs), 8192):
            batch = pairs[start:start + 8192]
            machine.mem_write(source, b"".join(struct.pack("<II", a & MASK, b & MASK) for a, b in batch))
            for register, value in ((A.UC_ARM_REG_R0, symbols[name]), (A.UC_ARM_REG_R1, source),
                                    (A.UC_ARM_REG_R2, output), (A.UC_ARM_REG_R3, len(batch)),
                                    (A.UC_ARM_REG_SP, stack), (A.UC_ARM_REG_LR, sentinel)):
                machine.reg_write(register, value)
            machine.emu_start(symbols["division_probe"], sentinel, timeout=2_000_000, count=2_000_000)
            assert machine.reg_read(A.UC_ARM_REG_PC) == sentinel, name + " did not return"
            assert machine.reg_read(A.UC_ARM_REG_SP) == stack, name + " changed SP"
            yield from zip(batch, struct.iter_unpack("<II", machine.mem_read(output, len(batch) * 8)))

    rng = random.Random(1136)
    edges = sorted({0, MASK, *(value & MASK for bit in range(32)
                              for value in ((1 << bit) - 1, 1 << bit, (1 << bit) + 1))})
    pairs = [(a, b) for a in edges for b in edges]
    pairs += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(4096)]
    for name in ("__aeabi_uidiv", "__aeabi_uidivmod", "__aeabi_idiv", "__aeabi_idivmod"):
        for (a, b), (q, r) in execute(name, pairs):
            if "uidiv" not in name:
                a, b = signed(a), signed(b)
            expected = abs(a) // abs(b) if b else 0
            if (a < 0) != (b < 0):
                expected = -expected
            remainder = a - expected * b if b else 0
            assert q == expected & MASK, (name, a, b, q, expected)
            if name.endswith("mod"):
                assert r == remainder & MASK, (name, a, b, r, remainder)

    denominators = sorted({*range(1, 65537), *(b for b in edges if 0 < b <= 0x7fffffff),
                           *(rng.randrange(1, 0x80000000) for _ in range(4096))})
    slopes = [(a, b) for b in denominators for a in
              (-0x80000000, 0x7fffffff, -(b - 1), b - 1, b)]
    slopes += [(signed(rng.getrandbits(32)), rng.randrange(1, 0x80000000)) for _ in range(8192)]
    for (a, b), (q, _) in execute("grDivsu", slopes):
        expected = abs(a) // b * (-1 if a < 0 else 1)
        assert q == expected & MASK, ("grDivsu", a, b, q, expected)
    for (b, _), (shift, reciprocal) in execute("imRcpu", [(b, 0) for b in denominators]):
        assert shift == 32 - b.bit_length()
        assert reciprocal * (b << shift) <= 1 << 62, (b, shift, reciprocal)
    print(f"ARM division: {4 * len(pairs):,} ABI cases, {len(slopes):,} exact slopes and "
          f"{len(denominators):,} reciprocal bounds passed")


if __name__ == "__main__":
    guard = install()
    main()
