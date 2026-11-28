#!/usr/bin/env python3
"""Execute DLL entry and native TRAP landing pads under bounded ARM emulation.

Run with make symbian-entry-check; requires arm-none-eabi tools and Unicorn.
Select handlers from the linked EHABI descriptors using the OS return-address ranges.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP,
    UC_CPU_ARM_926, UC_CPU_ARM_1136_R2,
)
from differential_fault_guard import install

ROOT = Path(__file__).resolve().parents[1]
PREFIX = os.environ.get("ARM_TOOL_PREFIX", "arm-none-eabi-")
IMPORTS = (
    "sym_User_MarkCleanupStack", "sym_CCoeEnv_ConstructL",
    "sym_User_UnMarkCleanupStack", "__cxa_begin_catch",
    "sym_XLeaveException_GetReason", "sym_User_Invariant",
    "__cxa_end_catch", "__cxa_end_cleanup", "__aeabi_unwind_cpp_pr0",
    "sym_UserSvr_DllTls", "sym_UserSvr_DllSetTls", "sym_UserSvr_DllFreeTls",
)


def check_march(march):
    with tempfile.TemporaryDirectory(prefix="gerbera-entry-") as directory:
        work = Path(directory)
        stub = work / "imports.S"
        stub.write_text(".syntax unified\n.arm\n.text\n" + "".join(
            f".global {name}\n{name}: bx lr\n" for name in IMPORTS
        ) + ".section .rodata\n.global _ZTI15XLeaveException\n"
            "_ZTI15XLeaveException: .word 0,0\n")
        linker = work / "entry.ld"
        linker.write_text("""
SECTIONS {
 . = 0x8000;
 Image$$ER_RO$$Base = .;
 .init : { *(.init) }
 .text : { *(.text*) }
 .rodata : { *(.rodata*) }
 .ARM.extab : { *(.ARM.extab*) }
 .ARM.exidx : {
   .ARM.exidx$$Base = .;
   *(.ARM.exidx*)
   .ARM.exidx$$Limit = .;
 }
 Image$$ER_RO$$Limit = .;
}
""")
        for source, output in ((ROOT / "symbian/symbian_entry.S", "entry.o"), (stub, "imports.o")):
            subprocess.run([PREFIX + "gcc", f"-march={march}", "-marm", "-c", str(source),
                            "-o", str(work / output)], check=True, timeout=10)
        elf, binary = work / "entry.elf", work / "entry.bin"
        subprocess.run([PREFIX + "ld", "-T", str(linker), str(work / "entry.o"),
                        str(work / "imports.o"), "-o", str(elf)], check=True, timeout=10)
        subprocess.run([PREFIX + "objcopy", "-O", "binary", str(elf), str(binary)], check=True, timeout=10)
        symbols = {}
        for line in subprocess.check_output([PREFIX + "nm", str(elf)], text=True, timeout=10).splitlines():
            fields = line.split()
            if len(fields) == 3:
                symbols[fields[2]] = int(fields[0], 16)
        code = binary.read_bytes()

    def word(address):
        return struct.unpack_from("<I", code, address - 0x8000)[0]

    def prel31(address):
        displacement = word(address) & 0x7fffffff
        if displacement & 0x40000000:
            displacement -= 0x80000000
        return address + displacement

    entry = symbols["sym_CCoeEnv_ConstructTrapped"]
    descriptors = []
    for address in range(symbols[".ARM.exidx$$Base"], symbols[".ARM.exidx$$Limit"], 8):
        if prel31(address) != entry:
            continue
        table = prel31(address + 4)
        assert word(table) == 0x80aab0b0  # PR0: pop r4-r6/lr, finish.
        cursor = table + 4
        while word(cursor):
            length, offset = struct.unpack_from("<HH", code, cursor - 0x8000)
            assert offset & 1 == 0
            catch = length & 1
            length &= ~1
            landing = prel31(cursor + 4)
            reference = bool(word(cursor + 4) & 0x80000000)
            typeinfo = word(cursor + 8) if catch else None
            descriptors.append((entry + offset, length, landing, reference, typeinfo))
            cursor += 12 if catch else 8
    assert len(descriptors) == 3
    assert descriptors[0][3:] == (True, symbols["_ZTI15XLeaveException"])
    assert descriptors[1][3:] == (False, 0xffffffff)
    assert descriptors[2][4] is None

    def run(mode, reason=None):
        machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        machine.ctl_set_cpu_model(UC_CPU_ARM_926 if march == "armv5t" else UC_CPU_ARM_1136_R2)
        machine.mem_map(0x8000, 0x10000)
        machine.mem_write(0x8000, code)
        machine.mem_map(0x20000, 0x10000)
        machine.reg_write(UC_ARM_REG_SP, 0x2fff0)
        machine.reg_write(UC_ARM_REG_LR, 0x17000)
        machine.reg_write(UC_ARM_REG_R0, 0x23456 if reason is None else reason)
        for register in range(UC_ARM_REG_R4, UC_ARM_REG_R4 + 8):
            machine.reg_write(register, 0x400 + register)
        calls = []
        by_address = {symbols[name]: name for name in IMPORTS}

        def dispatch_exception(index):
            start, length, landing, _, _ = descriptors[index]
            return_address = machine.reg_read(UC_ARM_REG_LR)
            assert start <= return_address < start + length
            machine.reg_write(UC_ARM_REG_R0, 0x45678)
            machine.reg_write(UC_ARM_REG_PC, landing)

        def hook(uc, address, size, data):
            name = by_address.get(address)
            if not name:
                return
            calls.append(name)
            if name == "sym_User_MarkCleanupStack":
                uc.reg_write(UC_ARM_REG_R0, 0x34567)
            elif name == "sym_CCoeEnv_ConstructL":
                assert uc.reg_read(UC_ARM_REG_R0) == 0x23456
                if mode != "success":
                    dispatch_exception(1 if mode == "foreign" else 0)
                    return
            elif name == "sym_User_UnMarkCleanupStack":
                assert uc.reg_read(UC_ARM_REG_R0) == 0x34567
            elif name == "sym_XLeaveException_GetReason" and mode == "cleanup":
                dispatch_exception(2)
                return
            elif name in ("sym_User_Invariant", "__cxa_end_cleanup"):
                uc.emu_stop()
                return
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

        machine.hook_add(UC_HOOK_CODE, hook)
        machine.emu_start(symbols["_E32Dll"] if reason is not None else entry,
                          0x17000, timeout=1000000, count=1000)
        if mode not in ("foreign", "cleanup"):
            assert machine.reg_read(UC_ARM_REG_PC) == 0x17000
            assert machine.reg_read(UC_ARM_REG_SP) == 0x2fff0
            for register in range(UC_ARM_REG_R4, UC_ARM_REG_R4 + 8):
                assert machine.reg_read(register) == 0x400 + register
        if reason is not None:
            assert machine.reg_read(UC_ARM_REG_R0) == 0
        return calls

    for reason in range(5):
        assert run("success", reason) == []
    prefix = ["sym_User_MarkCleanupStack", "sym_CCoeEnv_ConstructL"]
    assert run("success") == prefix + ["sym_User_UnMarkCleanupStack"]
    assert run("leave") == prefix + ["__cxa_begin_catch", "sym_XLeaveException_GetReason", "__cxa_end_catch"]
    assert run("foreign") == prefix + ["__cxa_begin_catch", "sym_User_Invariant"]
    assert run("cleanup") == prefix + ["__cxa_begin_catch", "sym_XLeaveException_GetReason", "__cxa_end_catch", "__cxa_end_cleanup"]
    print(f"Symbian entry ({march}): 5 lifecycle reasons, success, leave, foreign catch and cleanup passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-armv5", action="store_true",
                        help="Also check the armv5t / ARM926 target")
    args = parser.parse_args()
    arches = ("armv6j", "armv5t") if args.include_armv5 else ("armv6j",)
    for march in arches:
        check_march(march)


if __name__ == "__main__":
    guard = install()
    main()
