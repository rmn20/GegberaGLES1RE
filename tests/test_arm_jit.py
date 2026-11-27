#!/usr/bin/env python3
"""Check ARM JIT instruction selection, arithmetic and register preservation."""

import argparse
import ctypes as C
from itertools import product
from pathlib import Path
import random
import re
import struct
from types import SimpleNamespace

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as ARM

from differential_fault_guard import install
from differential_raster import buffer, compare_pixel_span, pixel_texture_record, words

ROOT = Path(__file__).resolve().parents[1]
OP = {name: int(value) for name, value in re.findall(
    r"HCOP_(\w+)\s*=\s*(\d+)", (ROOT / "src/include/gerbera/hc/hcOpcode.h").read_text())}


def instruction(op, dst=0, src=0, rhs=0, immediate=False):
    return (OP[op] | int(immediate) << 8 | dst << 10 | src << 21, rhs & 0xFFFFFFFF)


def signed(value):
    return value if value < 0x80000000 else value - 0x100000000


def packed_color(op, a, b):
    alpha = (a if op.endswith("_A") else b) >> 24
    result = 0
    for shift in range(0, 24 if "_RGB" in op else 32, 8):
        x, y = a >> shift & 255, b >> shift & 255
        if op.startswith("UQADD8"):
            value = min(x + y, 255)
        elif op.startswith("UQSUB8"):
            value = max(x - y, 0)
        else:
            value = (x * (alpha + 1) >> 8) + (y * (256 - alpha) >> 8)
        result |= value << shift
    return result


def scalar_blend(op, lanes=4, wrong_bias=False):
    body, next_reg = [], 7

    def emit(opcode, src, rhs, immediate=False):
        nonlocal next_reg
        reg = next_reg
        next_reg += 1
        body.append(instruction(opcode, reg, src, rhs, immediate))
        return reg

    alpha = emit("LSR", 1 if op.endswith("_A") else 2, 24, True)
    inverse = emit("XOR", alpha, 255, True)
    result = None
    for lane in range(lanes):
        a = emit("AND", emit("LSR", 1, lane * 8, True), 255, True)
        b = emit("AND", emit("LSR", 2, lane * 8, True), 255, True)
        if op.startswith("PACKED_ALPHA"):
            left = emit("LSR", emit("ADD", emit("MULU", a, alpha), alpha if wrong_bias else a), 8, True)
            right = emit("LSR", emit("ADD", emit("MULU", b, inverse), inverse if wrong_bias else b), 8, True)
            value = emit("ADD", left, right)
        elif op.startswith("UQADD8"):
            value = emit("ADD", a, b)
        else:
            value = emit("MAXS", emit("SUB", a, b), 0, True)
        value = emit("MINU", value, 255, True)
        value = emit("LSL", value, lane * 8, True)
        result = value if result is None else emit("OR", result, value)
    body.append(instruction("MOV", 4, result))
    return body


def check_spans(lib, machine, code, data, stack, stop):
    oracle = SimpleNamespace(uc=machine, STACK=stack, STACK_SIZE=65536, cursor=data)
    oracle.write = lambda address, payload: machine.mem_write(address, bytes(payload))
    oracle.readback = lambda address, size: bytes(machine.mem_read(address, size))
    oracle.watch = lambda *args: None
    oracle.unwatch = lambda *args: None

    def allocate(size, payload=b""):
        address = oracle.cursor
        oracle.cursor += (size + 15) & ~15
        assert oracle.cursor <= data + 65536
        oracle.write(address, payload + bytes(size - len(payload)))
        return address

    def install_code(payload):
        oracle.write(code, payload)
        machine.ctl_remove_cache(code, code + 65536)
        return code

    def call(entry, args, initial_registers):
        machine.reg_write(ARM.UC_ARM_REG_R0, args[0])
        machine.reg_write(ARM.UC_ARM_REG_SP, stack + 65536 - 4096)
        machine.reg_write(ARM.UC_ARM_REG_LR, stop)
        for reg, value in initial_registers.items():
            machine.reg_write(reg, value & 0xFFFFFFFF)
        machine.emu_start(entry, stop, timeout=100000, count=100000)
        assert machine.reg_read(ARM.UC_ARM_REG_PC) == stop

    def compare(name, case, actual, expected):
        if name == "compiledPixelSpan" and case[-1] == "hosted pixels" and not bounded_reference:
            return
        assert actual == expected, (name, case)

    oracle.alloc, oracle.install_code, oracle.call = allocate, install_code, call
    suite = SimpleNamespace(oracle=oracle, lib=lib, compare=compare,
                            confirm_code_semantics=lambda *args: None)
    descriptors = []
    lib.grrGetTextureFormatDescriptor.argtypes = [C.c_void_p, C.c_int]
    lib.grExecuteProgram.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    lib.differential_raster_compile.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_int, C.c_void_p]
    lib.grCompileARMPixelPipeline.argtypes = [C.c_void_p, C.c_int, C.c_void_p, C.c_uint32, C.c_void_p]
    lib.grCompileARMPixelPipelineEx.argtypes = [C.c_void_p, C.c_int, C.c_void_p, C.c_uint32, C.c_void_p, C.c_int]
    for fmt in range(18):
        desc = buffer(bytes(16))
        lib.grrGetTextureFormatDescriptor(desc, fmt)
        descriptors.append(bytes(desc))
    signature = bytearray(144)
    signature[:16] = descriptors[15]
    signature[10] = 2
    for offset, value in ((16, 1), (17, 7), (18, 1), (21, 1), (22, 3), (42, 1), (47, 1)):
        signature[offset] = value
    signature[56:100] = pixel_texture_record(descriptors)
    inputs = [0xFF204060, 0xFFA08020, 0xFF10D0A0, 0xFFE03090] + [0] * 4 + [0, 0, 0, 2, 2, 0, 0]
    patterns = ([0] * 8, [0xFFFF] * 8, [0, 0xFFFF] * 4)
    color_steps = (0, 1 << 16, -(11 << 16), 0x80000000)
    depth_steps = (0, 0x01000000, -0x01000000, 0x80000000)
    checks = 0
    for dither, less, depth_write, mode in product((0, 1), repeat=4):
        signature[21], signature[22], signature[45] = depth_write, 1 if less else 3, dither
        texture_mode = 3 if mode else 0
        signature[56:100] = pixel_texture_record(descriptors, mode=texture_mode)
        inputs[13] = texture_mode
        program = buffer(bytes(32768))
        assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
        emitted = []
        for mask in (0x3FF, 0x7FF, 0xFFF):
            output, parameters = buffer(bytes(65536)), buffer(bytes(16))
            length = lib.differential_raster_compile(program, mask, output, 65536, parameters)
            assert 0 < length <= 65536
            emitted.append(bytes(output)[:length])
        assert emitted[2] != emitted[1], "depth-reject optimization did not execute"
        if not mode:
            assert emitted[0] != emitted[1], "gradient rematerialization did not execute"
        optimized_codes = [item for item in dict.fromkeys(emitted[1:]) if item != emitted[0]]
        direct, mask = buffer(bytes(4096)), C.c_uint32()
        length = lib.grCompileARMPixelPipeline(direct, 4096, buffer(bytes(signature)), 0, C.byref(mask))
        assert length > 0
        direct_bytes = bytes(direct)[:length]
        optimized_codes.append(direct_bytes)
        for (pattern, depths), step, depth_step in product(enumerate(patterns), color_steps, depth_steps):
            # Out-of-range interpolants use the ordinary JIT as reference.
            bounded_reference = step in (0, 1 << 16, -(11 << 16)) and depth_step in (0, 0x01000000, -0x01000000)
            gradients = [0x10000000, 127 << 16, 80 << 16, 40 << 16, 180 << 16,
                         255 << 16, 0x10000000, 0x40000000, 0, 0]
            gradients += [depth_step, 0, step, step, step, 0, 0x70000000, 0xA0000000, 0, 0]
            gradients += [0x02000000, 0, 0, 0, 0, 0, 0xC0000000, 0x90000000, 0, 0]
            initial = (words([0] * 8) + b"\xA5" * 8,
                       struct.pack("<8H", *depths) + b"\xA5" * 8,
                       bytes(8) + b"\xA5" * 8)
            for optimized in optimized_codes:
                oracle.cursor = data
                compare_pixel_span(suite, bytes(signature), (emitted[0], optimized),
                                   (dither, less, depth_write, mode, pattern, step, depth_step),
                                   inputs=inputs, gradients=gradients, initial=initial)
                checks += 1
    print(f"ARM JIT: {checks} optimized spans match ordinary JIT; bounded interpolants also match C")
    bounded_reference = True
    checks = 0
    for width, height in ((0, 0), (0, 2), (2, 0), (1, 1)):
        for wrap_s, wrap_t in product((0, 2), repeat=2):
            for fmt in (0, 2, 4):
                signature = bytearray(144)
                signature[:16] = descriptors[2]
                for offset, value in ((16, 1), (17, 15), (42, 1), (47, 1)):
                    signature[offset] = value
                texture = bytearray(pixel_texture_record(descriptors, fmt=fmt, mode=3))
                texture[17], texture[21], texture[22] = int(width == height), wrap_s, wrap_t
                signature[56:100] = texture
                program = buffer(bytes(32768))
                assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
                emitted = []
                shift_s = (32 if wrap_s == 0 else 31) - width if width else 0
                shift_t = (32 if wrap_t == 0 else 31) - height if height else 0
                values = ((1 << width) - 1, (1 << height) - 1, shift_s, shift_t, width)
                for specialize in (False, True):
                    if specialize:
                        for offset, value in zip((16, 20, 24, 28, 32), values):
                            lib.HC_setConstantParameter(program, offset, value)
                    output = buffer(bytes(65536))
                    length = lib.differential_raster_compile(program, 0x3FF, output, 65536, buffer(bytes(16)))
                    assert 0 < length <= 65536
                    emitted.append(bytes(output)[:length])
                inputs = [0xFF204060, 0xFFA08020, 0xFF10D0A0, 0xFFE03090] + [0] * 7 + [fmt, fmt, 3, 0]
                for s, t in ((0, 0), (1, 255), (-257, 128), (511, 768)):
                    gradients = [0, 0, 32 << 16, 64 << 16, 128 << 16, 255 << 16,
                                 s * (1 << shift_s) // 256 if width else 0,
                                 t * (1 << shift_t) // 256 if height else 0, 0, 0] + [0] * 20
                    oracle.cursor = data
                    compare_pixel_span(suite, bytes(signature), emitted,
                                       ("texture layout", width, height, wrap_s, wrap_t, fmt, s, t),
                                       inputs=inputs, gradients=gradients, sampling=[*values, 0] * 2)
                    checks += 1
    print(f"ARM JIT: {checks} specialized texture spans match generic JIT and C")

    destinations = [descriptors[15], descriptors[1], descriptors[2], descriptors[6]]
    rng = random.Random(1136)
    checks = 0
    saved = {reg: 0x71360000 + reg for reg in range(ARM.UC_ARM_REG_R4, ARM.UC_ARM_REG_R11 + 1)}
    for output_format, texture_format, modulate, dither, fog in product(
            range(4), (1, 2, 10, 11), (0, 1), (0, 1), (0, 1)):
        if dither and output_format != 0:
            continue
        signature = bytearray(144)
        signature[:16] = destinations[output_format]
        signature[10] = 2
        depth = (output_format + texture_format + modulate) % 3
        for offset, value in ((16, 1), (17, signature[8]), (18, bool(depth)),
                              (21, modulate), (22, 1 if depth == 1 else 3),
                              (40, fog), (42, 1), (45, dither), (47, 1)):
            signature[offset] = value
        signature[56:100] = pixel_texture_record(descriptors, fmt=texture_format, mode=0 if modulate else 3)
        signature[73] = 0
        if (not modulate and not fog and dither and signature[9] == 4 and
                signature[:3] == signature[56:59]):
            signature[79] = 1
            signature[47] = 0
        direct = buffer(bytes(4096))
        mask = C.c_uint32()
        length = lib.grCompileARMPixelPipeline(direct, 4096, buffer(bytes(signature)), 0, C.byref(mask))
        if signature[7] and not signature[63]:
            assert length == 0
            continue
        assert (length > 0) == (not fog), (output_format, texture_format, modulate, dither, fog)
        dynamic = bytes(direct)[:length]
        program, output = buffer(bytes(32768)), buffer(bytes(65536))
        assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
        size = lib.differential_raster_compile(program, 0x3FF, output, 65536, buffer(bytes(16)))
        assert size > 0
        generic = bytes(output)[:size]
        size = lib.differential_raster_compile(program, 0x7FF, output, 65536, buffer(bytes(16)))
        assert size > 0
        rematerialized = bytes(output)[:size]
        for width, height in ((0, 0), (0, 2), (2, 0), (1, 1)):
            layout = 1 + 11 * width + height
            compiled = [generic, rematerialized]
            if length:
                size = lib.grCompileARMPixelPipeline(direct, 4096, buffer(bytes(signature)), layout, C.byref(mask))
                assert size > 0
                compiled += [dynamic, bytes(direct)[:size]]
            oracle.cursor = data
            initial = [bytes(rng.getrandbits(8) for _ in range(128)) + b"\xA5" * 16,
                       struct.pack("<32H", *([0, 0xFFFF, 0x4000, 0x8000] * 8)) + b"\xA5" * 16]
            targets = [allocate(len(value), value) for value in initial]
            scratch = allocate(16384)
            pipe = scratch + 64
            spans = allocate(8, words([(13 << 16) | 1, (7 << 16) | 3]))
            kernel = (C.c_uint32 * 4).in_dll(getattr(lib, "library", lib), "g_orderedDitherKernel")
            palette = allocate(1024, words([rng.getrandbits(32) for _ in range(256)]))
            texels = allocate(4 << (width + height), words([rng.getrandbits(32) for _ in range(1 << (width + height))]))
            shift_s, shift_t = 32 - width if width else 0, 32 - height if height else 0
            oracle.write(pipe, words([0, texels, palette, 0, (1 << width) - 1,
                                     (1 << height) - 1, shift_s, shift_t, width]))
            oracle.write(pipe + 128, words([allocate(16, bytes(kernel))]))
            oracle.write(pipe + 132, words([16 * signature[9], 32, 16, 0xFF7090B0, 64, 3, 3, 255, 255]))
            oracle.write(pipe + 168, words([*targets, 0, 2, spans, 1]))
            gradients = [0x10000000, 127 << 16, 80 << 16, 40 << 16, 180 << 16, 200 << 16,
                         0x91234567 if width else 0, 0xFEDCBA98 if height else 0, 0, 0]
            gradients += [0x01000000, -0x5234, 0x12345, -0x45678, 0xABCDEF, -0x76543,
                          0x13456789 if width else 0, 0xABCDEF01 if height else 0, 0, 0]
            gradients += [0x02000000, 0x2134, 0x10000, 0x30000, -0x20000, -0x10000,
                          0x17234567 if width else 0, 0xDBECA987 if height else 0, 0, 0]
            oracle.write(pipe + 192, words(gradients))
            original_stack = oracle.readback(scratch, 16384)
            reference = None
            for emitted in compiled:
                for address, value in zip(targets, initial):
                    oracle.write(address, value)
                oracle.write(scratch, original_stack)
                entry = install_code(emitted)
                call(entry, [scratch], saved)
                result = tuple(oracle.readback(address, len(value)) for address, value in zip(targets, initial))
                assert all(machine.reg_read(reg) == value for reg, value in saved.items())
                assert machine.reg_read(ARM.UC_ARM_REG_SP) == stack + 65536 - 4096
                if reference is None:
                    reference = result
                else:
                    assert result == reference, ("pixel format", output_format, texture_format, modulate,
                                                 dither, fog, width, height, compiled.index(emitted))
                    checks += 1
    print(f"ARM JIT: {checks} direct and rematerialized format comparisons passed")

    ext_checks = 0
    kernel = (C.c_uint32 * 4).in_dll(getattr(lib, "library", lib), "g_orderedDitherKernel")
    color_variants = (
        ("const_255", [255 << 16] * 4, [0] * 4, [0] * 4),
        ("const_256", [(256 << 16) + 0x7F00] * 4, [0] * 4, [0] * 4),
        ("const_mid", [180 << 16, 80 << 16, 200 << 16, 160 << 16], [0] * 4, [0] * 4),
        ("gouraud_mixed_clamp", [256 << 16, 128 << 16, 200 << 16, 256 << 16], [0] * 4, [0] * 4),
        ("gouraud", [260 << 16, 80 << 16, 40 << 16, 180 << 16],
         [-0x5234, 0x12345, -0x45678, 0x10000], [0xABCDEF, -0x76543, 0x20000, -0x10000]),
    )
    for out_idx, tex_fmt, modulate, fog, alpha_test, depth in product(
            range(4), (2, 11), (0, 1), (0, 1), (0, 1), (0, 1)):
        if alpha_test and modulate and out_idx not in (0, 2):
            continue
        signature = bytearray(144)
        signature[:16] = destinations[out_idx]
        signature[10] = 2
        dither = int(out_idx == 0 and (fog ^ alpha_test))
        mono_fog = int(fog and (out_idx ^ tex_fmt) & 1)
        alpha_func = (1, 3, 4, 5)[(out_idx + fog + depth + modulate) & 3]
        for offset, value in ((16, 1), (17, signature[8]), (18, depth),
                              (19, alpha_test), (20, alpha_func),
                              (21, depth), (22, 1 if (out_idx & 1) else 3),
                              (30, 0),
                              (40, fog), (41, mono_fog), (42, 1), (45, dither), (47, 1)):
            signature[offset] = value
        tex_rec = bytearray(pixel_texture_record(descriptors, fmt=tex_fmt, mode=0 if modulate else 3))
        tex_rec[17] = 0
        # Exercise alphaMode 1 (tex), 2 (prev) and 3 (modulate) when dst has alpha or alphaTest is on.
        alpha_mode = ((out_idx + fog + depth) % 3) + 1
        if signature[7] or alpha_test:
            if alpha_mode == 1:
                tex_rec[25], tex_rec[37] = 0, 1
            elif alpha_mode == 2:
                tex_rec[25], tex_rec[37] = 3, 1
            else:
                tex_rec[25], tex_rec[29], tex_rec[37] = 3, 0, 0
        signature[56:100] = tex_rec
        layout = 0 if (out_idx == 0 and fog and depth) else (1 + 11 * 2 + 2)
        direct = buffer(bytes(4096))
        mask = C.c_uint32()
        length = lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(signature)), layout, C.byref(mask), 1)
        assert length > 0, (out_idx, tex_fmt, modulate, fog, alpha_test, depth)
        ext_code = bytes(direct)[:length]
        program, output = buffer(bytes(32768)), buffer(bytes(65536))
        assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
        size = lib.differential_raster_compile(program, 0x3FF, output, 65536, buffer(bytes(16)))
        assert size > 0
        hc_code = bytes(output)[:size]
        for cname, cref, cxadd, cyadd in color_variants:
            for alpha_ref in ((140,) if alpha_test else (0,)):
                oracle.cursor = data
                initial = [bytes(rng.getrandbits(8) for _ in range(128)) + b"\xA5" * 16,
                           struct.pack("<32H", *([0, 0xFFFF, 0x4000, 0x8000] * 8)) + b"\xA5" * 16]
                targets = [allocate(len(value), value) for value in initial]
                scratch = allocate(16384)
                pipe = scratch + 64
                spans = allocate(8, words([(13 << 16) | 1, (7 << 16) | 3]))
                palette = allocate(1024, words([rng.getrandbits(32) for _ in range(256)]))
                texels = allocate(4 << 4, words([
                    ((a & 0xFF) << 24) | (rng.getrandbits(24))
                    for a in (0, 64, 140, 141, 200, 254, 255, 128, 32, 220, 10, 250, 180, 90, 255, 0)
                ]))
                oracle.write(pipe, words([0, texels, palette, 0, 3, 3, 30, 30, 2]))
                oracle.write(pipe + 128, words([allocate(16, bytes(kernel))]))
                fog_color = 0xB0 if mono_fog else 0xFF7090B0
                oracle.write(pipe + 132, words([16 * signature[9], 32, 16, fog_color, alpha_ref, 3, 3, 255, 255]))
                oracle.write(pipe + 168, words([*targets, 0, 2, spans, 1]))
                gradients = [
                    0x10000000, 127 << 16, *cref, 0x91234567, 0xFEDCBA98, 0, 0,
                    0x01000000, -0x5234, *cxadd, 0x13456789, 0xABCDEF01, 0, 0,
                    0x02000000, 0x2134, *cyadd, 0x17234567, 0xDBECA987, 0, 0,
                ]
                oracle.write(pipe + 192, words(gradients))
                original_stack = oracle.readback(scratch, 16384)
                results = []
                for emitted in (hc_code, ext_code):
                    for address, value in zip(targets, initial):
                        oracle.write(address, value)
                    oracle.write(scratch, original_stack)
                    entry = install_code(emitted)
                    call(entry, [scratch], saved)
                    results.append(tuple(oracle.readback(address, len(value)) for address, value in zip(targets, initial)))
                    assert all(machine.reg_read(reg) == value for reg, value in saved.items())
                    assert machine.reg_read(ARM.UC_ARM_REG_SP) == stack + 65536 - 4096
                assert results[0] == results[1], (
                    "extended pixel pipeline", out_idx, tex_fmt, modulate, fog, alpha_test,
                    alpha_mode, depth, dither, mono_fog, cname)
                ext_checks += 1
    assert lib.grCompileARMPixelPipelineEx(direct, 100, buffer(bytes(signature)), 0, C.byref(mask), 1) == 0
    assert lib.grCompileARMPixelPipelineEx(direct, 2000, buffer(bytes(signature)), 0, C.byref(mask), 1) > 0
    print(f"ARM JIT: {ext_checks} extended pixel pipeline comparisons passed")

    blend_checks = 0
    blend_destinations = (descriptors[1], descriptors[5])
    for out_idx, tex_fmt, modulate, fog, depth, alpha_mode in product(
            range(2), (2, 11), (0, 1), (0, 1), (0, 1), (1, 2, 3)):
        signature = bytearray(144)
        signature[:16] = blend_destinations[out_idx]
        signature[10] = 2
        mono_fog = int(fog and (out_idx ^ tex_fmt ^ depth) & 1)
        for offset, value in ((16, 1), (17, signature[8]), (18, depth),
                              (19, 0), (20, 0),
                              (21, depth), (22, 1 if (out_idx & 1) else 3),
                              (27, 1), (28, 4), (29, 5), (30, 0),
                              (40, fog), (41, mono_fog), (42, 1), (45, 0), (47, 1)):
            signature[offset] = value
        tex_rec = bytearray(pixel_texture_record(descriptors, fmt=tex_fmt, mode=0 if modulate else 3))
        tex_rec[17] = 0
        if alpha_mode == 1:
            tex_rec[25], tex_rec[37] = 0, 1
        elif alpha_mode == 2:
            tex_rec[25], tex_rec[37] = 3, 1
        else:
            tex_rec[25], tex_rec[29], tex_rec[37] = 3, 0, 0
        signature[56:100] = tex_rec
        layout = 0 if (out_idx == 0 and fog and depth) else (1 + 11 * 2 + 2)
        direct = buffer(bytes(4096))
        mask = C.c_uint32()
        assert lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(signature)), layout, C.byref(mask), 0) == 0
        length = lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(signature)), layout, C.byref(mask), 1)
        assert length > 0, (out_idx, tex_fmt, modulate, fog, depth, alpha_mode)
        ext_code = bytes(direct)[:length]
        program, output = buffer(bytes(32768)), buffer(bytes(65536))
        assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
        size = lib.differential_raster_compile(program, 0x3FF, output, 65536, buffer(bytes(16)))
        assert size > 0
        hc_code = bytes(output)[:size]
        for cname, cref, cxadd, cyadd in color_variants:
            oracle.cursor = data
            initial = [bytes(rng.getrandbits(8) for _ in range(128)) + b"\xA5" * 16,
                       struct.pack("<32H", *([0, 0xFFFF, 0x4000, 0x8000] * 8)) + b"\xA5" * 16]
            targets = [allocate(len(value), value) for value in initial]
            scratch = allocate(16384)
            pipe = scratch + 64
            spans = allocate(8, words([(13 << 16) | 1, (7 << 16) | 3]))
            palette = allocate(1024, words([rng.getrandbits(32) for _ in range(256)]))
            texels = allocate(4 << 4, words([
                ((a & 0xFF) << 24) | (rng.getrandbits(24))
                for a in (0, 64, 140, 141, 200, 254, 255, 128, 32, 220, 10, 250, 180, 90, 255, 0)
            ]))
            oracle.write(pipe, words([0, texels, palette, 0, 3, 3, 30, 30, 2]))
            oracle.write(pipe + 128, words([allocate(16, bytes(kernel))]))
            fog_color = 0xB0 if mono_fog else 0xFF7090B0
            oracle.write(pipe + 132, words([16 * signature[9], 32, 16, fog_color, 0, 3, 3, 255, 255]))
            oracle.write(pipe + 168, words([*targets, 0, 2, spans, 1]))
            gradients = [
                0x10000000, 127 << 16, *cref, 0x91234567, 0xFEDCBA98, 0, 0,
                0x01000000, -0x5234, *cxadd, 0x13456789, 0xABCDEF01, 0, 0,
                0x02000000, 0x2134, *cyadd, 0x17234567, 0xDBECA987, 0, 0,
            ]
            oracle.write(pipe + 192, words(gradients))
            original_stack = oracle.readback(scratch, 16384)
            results = []
            for emitted in (hc_code, ext_code):
                for address, value in zip(targets, initial):
                    oracle.write(address, value)
                oracle.write(scratch, original_stack)
                entry = install_code(emitted)
                call(entry, [scratch], saved)
                results.append(tuple(oracle.readback(address, len(value)) for address, value in zip(targets, initial)))
                assert all(machine.reg_read(reg) == value for reg, value in saved.items())
                assert machine.reg_read(ARM.UC_ARM_REG_SP) == stack + 65536 - 4096
            assert results[0] == results[1], (
                "blendSrcAlpha pixel pipeline", out_idx, tex_fmt, modulate, fog, depth, alpha_mode, mono_fog, cname)
            blend_checks += 1
    print(f"ARM JIT: {blend_checks} textured SRC_ALPHA blend pixel pipeline comparisons passed")

    untex_color_variants = (
        ("const_255", [255 << 16] * 4, [0] * 4, [0] * 4),
        ("const_mid", [180 << 16, 80 << 16, 200 << 16, 160 << 16], [0] * 4, [0] * 4),
        ("gouraud_bounded", [200 << 16, 80 << 16, 120 << 16, 180 << 16],
         [-0x5234, 0x12345, -0x45678, 0x10000], [0x20000, -0x10000, 0x20000, -0x10000]),
        ("gouraud_low", [12 << 16, 240 << 16, 100 << 16, 90 << 16],
         [0x20000, -0x30000, -0x10000, 0x20000], [0x10000, -0x20000, 0x30000, -0x10000]),
    )
    untex_checks = 0
    for out_idx, blend, fog, alpha_test, depth, dither in product(
            range(4), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1)):
        if dither and out_idx != 0:
            continue
        if blend and (out_idx not in (0, 1, 3) or alpha_test):
            continue
        signature = bytearray(144)
        signature[:16] = destinations[out_idx]
        signature[10] = 2
        mono_fog = int(fog and (out_idx ^ depth ^ dither ^ blend) & 1)
        alpha_func = (1, 3, 4, 5)[(out_idx + fog + depth + blend) & 3]
        for offset, value in ((16, 1), (17, signature[8]), (18, depth),
                              (19, alpha_test), (20, alpha_func),
                              (21, depth & ((out_idx ^ fog) | 1)), (22, 1 if (out_idx & 1) else 3),
                              (27, blend), (28, 0), (29, 2 if blend else 0),
                              (30, int(blend and fog and not depth)),
                              (40, fog), (41, mono_fog), (42, 1), (45, dither), (47, 1)):
            signature[offset] = value
        direct = buffer(bytes(4096))
        mask = C.c_uint32()
        assert lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(signature)), 0, C.byref(mask), 0) == 0
        length = lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(signature)), 0, C.byref(mask), 1)
        assert length > 0, (out_idx, blend, fog, alpha_test, depth, dither)
        ext_code = bytes(direct)[:length]
        program, output = buffer(bytes(32768)), buffer(bytes(65536))
        assert lib.grExecuteProgram(program, buffer(bytes(signature)), 4096) > 0
        size = lib.differential_raster_compile(program, 0x3FF, output, 65536, buffer(bytes(16)))
        assert size > 0
        hc_code = bytes(output)[:size]
        for cname, cref, cxadd, cyadd in untex_color_variants:
            if signature[30] and (any(cxadd) or any(cyadd)):
                continue
            for alpha_ref in ((140,) if alpha_test else (0,)):
                oracle.cursor = data
                initial = [bytes(rng.getrandbits(8) for _ in range(128)) + b"\xA5" * 16,
                           struct.pack("<32H", *([0, 0xFFFF, 0x4000, 0x8000] * 8)) + b"\xA5" * 16]
                targets = [allocate(len(value), value) for value in initial]
                scratch = allocate(16384)
                pipe = scratch + 64
                spans = allocate(8, words([(13 << 16) | 1, (7 << 16) | 3]))
                oracle.write(pipe, words([0, 0, 0, 0, 0, 0, 0, 0, 0]))
                oracle.write(pipe + 128, words([allocate(16, bytes(kernel))]))
                fog_color = 0xB0 if mono_fog else 0xFF7090B0
                oracle.write(pipe + 132, words([16 * signature[9], 32, 16, fog_color, alpha_ref, 3, 3, 255, 255]))
                oracle.write(pipe + 168, words([*targets, 0, 2, spans, 1]))
                gradients = [
                    0x10000000, 127 << 16, *cref, 0, 0, 0, 0,
                    0x01000000, -0x5234, *cxadd, 0, 0, 0, 0,
                    0x02000000, 0x2134, *cyadd, 0, 0, 0, 0,
                ]
                oracle.write(pipe + 192, words(gradients))
                original_stack = oracle.readback(scratch, 16384)
                results = []
                for emitted in (hc_code, ext_code):
                    for address, value in zip(targets, initial):
                        oracle.write(address, value)
                    oracle.write(scratch, original_stack)
                    entry = install_code(emitted)
                    call(entry, [scratch], saved)
                    results.append(tuple(oracle.readback(address, len(value)) for address, value in zip(targets, initial)))
                    assert all(machine.reg_read(reg) == value for reg, value in saved.items())
                    assert machine.reg_read(ARM.UC_ARM_REG_SP) == stack + 65536 - 4096
                assert results[0] == results[1], (
                    "untextured pixel pipeline", out_idx, blend, fog, alpha_test, depth, dither, mono_fog, cname)
                untex_checks += 1
    bad_blend = bytearray(144)
    bad_blend[:16] = destinations[0]
    bad_blend[10] = 2
    for offset, value in ((16, 1), (17, bad_blend[8]), (27, 1), (28, 0), (29, 3), (42, 1), (47, 1)):
        bad_blend[offset] = value
    bad_blend[29] = 3
    assert lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(bad_blend)), 0, C.byref(mask), 1) == 0
    bad_blend[29] = 2
    bad_blend[19] = 1
    assert lib.grCompileARMPixelPipelineEx(direct, 4096, buffer(bytes(bad_blend)), 0, C.byref(mask), 1) == 0
    print(f"ARM JIT: {untex_checks} untextured extended pixel pipeline comparisons passed")


def check_scheduler(lib, machine, code, data, stack, stop):
    """Check load/store aliases and live values across scheduled register spills."""
    rng = random.Random(113601)
    checks, different = 0, 0
    for case in range(12):
        program = [instruction("LD", 1, 0, 0, True),
                   instruction("LD", 2, 0, 4, True),
                   instruction("LD", 3, 0, 8, True),
                   instruction("SD", 3, 1, 16, True),
                   instruction("LD", 4, 2, 0, True),
                   instruction("ADD", 5, 4, 3),
                   instruction("SD", 5, 2, 4, True),
                   instruction("LD", 6, 1, 20, True)]
        live = list(range(2, 7))
        for reg in range(7, 25):
            program.append(instruction("LD", reg, 0, 32 + 4 * (reg - 7), True))
            live.append(reg)
        for reg in range(25, 48):
            left, right = live.pop(0), live.pop(-1)
            program.append(instruction(rng.choice(("ADD", "SUB", "XOR", "OR", "MULU")),
                                       reg, left, right))
            live.append(reg)
            if len(live) == 1:
                break
        program += [instruction("SD", live[0], 1, 240, True), instruction("EOP")]
        source = buffer(words([word for ins in program for word in ins]))
        versions = []
        for mask in (0x2FF, 0x3FF):
            output = buffer(bytes(65536))
            size = lib.differential_raster_compile(source, mask, output, 65536, buffer(bytes(16)))
            assert 0 < size <= 65536, (case, mask, size)
            versions.append(bytes(output)[:size])
        different += versions[0] != versions[1]
        for sample in range(24):
            initial = bytearray(words([rng.getrandbits(32) for _ in range(64)]))
            payload_address = data + 4096
            struct.pack_into('<II', initial, 0, payload_address,
                             payload_address + 16 + 4 * (sample % 4))
            payload = words([rng.getrandbits(32) for _ in range(64)])
            expected = bytearray(payload)
            regs = {0: data}
            for opcode, rhs in program:
                op, dst, src = opcode & 255, (opcode >> 10) & 2047, opcode >> 21
                if op == OP['EOP']:
                    break
                value = rhs if opcode & 256 else regs[rhs]
                if op == OP['LD']:
                    address = regs[src] + value
                    storage, offset = ((initial, address - data) if src == 0 else
                                       (expected, address - payload_address))
                    regs[dst] = struct.unpack_from('<I', storage, offset)[0]
                elif op == OP['SD']:
                    struct.pack_into('<I', expected, regs[src] + value - payload_address, regs[dst])
                else:
                    a = regs[src]
                    regs[dst] = {OP['ADD']: lambda: a + value,
                                 OP['SUB']: lambda: a - value,
                                 OP['XOR']: lambda: a ^ value,
                                 OP['OR']: lambda: a | value,
                                 OP['MULU']: lambda: a * value}[op]() & 0xFFFFFFFF
            for version, emitted in enumerate(versions):
                machine.mem_write(code, emitted)
                machine.ctl_remove_cache(code, code + 65536)
                machine.mem_write(data, bytes(initial))
                machine.mem_write(payload_address, payload)
                machine.reg_write(ARM.UC_ARM_REG_R0, data - 64)
                machine.reg_write(ARM.UC_ARM_REG_SP, stack + 65536 - 16)
                machine.reg_write(ARM.UC_ARM_REG_LR, stop)
                machine.emu_start(code, stop, timeout=100000, count=10000)
                assert machine.reg_read(ARM.UC_ARM_REG_PC) == stop
                actual = bytes(machine.mem_read(payload_address, len(expected)))
                assert actual == expected, (case, sample, version,
                    [(i, actual[i:i + 4].hex(), expected[i:i + 4].hex())
                     for i in range(0, len(expected), 4) if actual[i:i + 4] != expected[i:i + 4]])
                checks += 1
    assert different, 'ARM scheduler did not change any generated program'
    print(f'ARM scheduler: {checks} aliasing and register-pressure comparisons passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True,
                        help="Differential library built with GERBERA_ARMV6_JIT=1")
    parser.add_argument("--armv5", action="store_true", help="Check the compatibility backend")
    args = parser.parse_args()
    lib = install(path=args.library)
    lib.differential_arm_codegen.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    lib.differential_arm_codegen.restype = C.c_int
    lib.differential_raster_compile.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_int, C.c_void_p]
    lib.emulator_evalALUOp.argtypes = [C.c_int, C.c_uint32, C.c_uint32]
    lib.HC_setConstantParameter.argtypes = [C.c_void_p, C.c_int, C.c_int]
    for mutable in (False, True):
        source = buffer(words([word for ins in (
            instruction("LD", 1, 0, 24, True), instruction("LD", 2, 1, 24, True),
            instruction("SD", 1, 0, 24 if mutable else 28, True), instruction("EOP")) for word in ins]))
        initial = bytes(source)
        assert lib.HC_setConstantParameter(source, 24, 31) == int(not mutable)
        if mutable:
            assert bytes(source) == initial
        else:
            assert bytes(source)[:8] == words(instruction("MOV", 1, 0, 31, True))
            assert bytes(source)[8:] == initial[8:]
    machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    machine.ctl_set_cpu_model(ARM.UC_CPU_ARM_926 if args.armv5 else ARM.UC_CPU_ARM_1136_R2)
    code, data, stack, stop = 0x10000, 0x20000, 0x30000, 0x40000
    for address in (code, data, stack, stop):
        machine.mem_map(address, 0x10000)
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    make = instruction
    cases = [
        ("halfword", [make("AND", 4, 1, 65535, True)],
         lambda a, b, c: a & 65535, "uxth"),
        ("byte", [make("LSR", 3, 1, 8, True), make("AND", 4, 3, 255, True)],
         lambda a, b, c: a >> 8 & 255, "uxtb"),
        ("unsigned clamp", [make("AND", 3, 1, 65535, True), make("MINU", 4, 3, 255, True)],
         lambda a, b, c: min(a & 65535, 255), "usat"),
        ("signed clamp", [make("MAXS", 3, 1, 0, True), make("MINS", 4, 3, 255, True)],
         lambda a, b, c: min(max(signed(a), 0), 255), "usat"),
        ("unsigned high bit", [make("MINU", 4, 1, 255, True)],
         lambda a, b, c: min(a, 255), None),
        ("shifted clamp", [make("LSR", 3, 1, 8, True), make("MINU", 4, 3, 255, True)],
         lambda a, b, c: min(a >> 8, 255), None),
        ("byte addition", [make("AND", 1, 1, 255, True), make("AND", 2, 2, 255, True),
                           make("ADD", 3, 1, 2), make("MINU", 4, 3, 255, True)],
         lambda a, b, c: min((a & 255) + (b & 255), 255), "uqadd8"),
        ("multiply add", [make("MULU", 3, 1, 2), make("ADD", 4, 3, 6)],
         lambda a, b, c: a * b + c, "mla"),
        ("signed halfword multiply add", [make("ASR", 1, 1, 16, True), make("ASR", 2, 2, 16, True),
                                         make("MULS", 3, 1, 2), make("ADD", 4, 3, 6)],
         lambda a, b, c: (signed(a) >> 16) * (signed(b) >> 16) + c, "smlabb"),
        ("square", [make("MULU", 1, 1, 1), make("MOV", 4, 1)],
         lambda a, b, c: a * a, "mul"),
        ("input redefinition", [make("LSR", 3, 1, 8, True), make("MOV", 1, 0, 17, True),
                                 make("AND", 4, 3, 255, True)],
         lambda a, b, c: a >> 8 & 255, None),
        ("BIC first operand", [make("LSL", 3, 1, 2, True), make("BIC", 4, 3, 2)],
         lambda a, b, c: a << 2 & ~b, None),
        ("both shifted operands", [make("LSL", 3, 1, 2, True), make("ADD", 4, 3, 3)],
         lambda a, b, c: a << 3, None),
        ("overlapping clamps", [make("AND", 1, 1, 65535, True),
                                 make("AND", 2, 2, 65535, True),
                                 make("AND", 6, 6, 65535, True),
                                 make("LSR", 3, 1, 3, True), make("LSR", 5, 2, 2, True),
                                 make("LSR", 7, 6, 1, True), make("MINU", 5, 5, 255, True),
                                 make("MINU", 3, 3, 255, True), make("MINU", 7, 7, 255, True),
                                 make("ADD", 4, 3, 5), make("ADD", 4, 4, 7)],
         lambda a, b, c: min((a & 65535) >> 3, 255) + min((b & 65535) >> 2, 255)
                         + min((c & 65535) >> 1, 255), ("usat", 3)),
        ("overlapping extracts and multiply", [make("LSR", 3, 1, 8, True),
                        make("MULU", 5, 2, 6), make("LSR", 7, 2, 16, True),
                        make("ADD", 8, 5, 6), make("AND", 3, 3, 255, True),
                        make("AND", 7, 7, 255, True), make("ADD", 4, 3, 7),
                        make("ADD", 4, 4, 8)],
         lambda a, b, c: (a >> 8 & 255) + (b >> 16 & 255) + b * c + c, ("uxtb", 2)),
        ("nonadjacent zero test", [make("MOV", 4, 0, 0, True), make("ADD", 3, 1, 1, True),
                                   make("ADD", 5, 2, 1, True), make("CBNEQ", 1, 3, 0, True),
                                   make("MOV", 4, 0, 1, True), make("LABEL", rhs=1, immediate=True)],
         lambda a, b, c: int(a == 0xFFFFFFFF), None),
        ("intervening flags", [make("MOV", 4, 0, 0, True), make("ADD", 3, 1, 1, True),
                                make("MINU", 5, 2, 1, True), make("CBNEQ", 1, 3, 0, True),
                                make("MOV", 4, 0, 1, True), make("LABEL", rhs=1, immediate=True)],
         lambda a, b, c: int(a == 0xFFFFFFFF), None),
        ("nonadjacent signed test", [make("MOV", 4, 0, 1, True), make("ADD", 3, 1, 1, True),
                                     make("ADD", 5, 2, 1, True), make("CBGES", 1, 3, 0, True),
                                     make("MOV", 4, 0, 0, True), make("LABEL", rhs=1, immediate=True)],
         lambda a, b, c: int(signed((a + 1) & 0xFFFFFFFF) >= 0), None),
        ("compiled repeated expression", [make("XOR", 3, 1, 255, True),
                                            make("XOR", 5, 1, 255, True), make("ADD", 4, 3, 5)],
         lambda a, b, c: 2 * (a ^ 255), None),
        ("compiled redefined source", [make("XOR", 3, 1, 255, True), make("ADD", 1, 1, 1, True),
                                         make("XOR", 5, 1, 255, True), make("ADD", 4, 3, 5)],
         lambda a, b, c: (a ^ 255) + (((a + 1) & 0xFFFFFFFF) ^ 255), None),
        ("compiled redefined result", [make("XOR", 3, 1, 255, True), make("MOV", 3, 2),
                                         make("XOR", 5, 1, 255, True), make("ADD", 4, 3, 5)],
         lambda a, b, c: b + (a ^ 255), None),
        ("compiled repeated parameter", [make("LD", 3, 0, 0, True),
                                           make("LD", 5, 0, 0, True), make("ADD", 4, 3, 5)],
         lambda a, b, c: 2 * a, None),
        ("compiled parameter result overwritten", [make("MOV", 1, 2),
                     make("LD", 3, 0, 0, True), make("ADD", 4, 1, 3)],
         lambda a, b, c: a + b, None),
        ("compiled parameter word store", [make("SD", 2, 0, 0, True),
                                             make("LD", 4, 0, 0, True)],
         lambda a, b, c: b, None),
        ("compiled parameter byte store", [make("SB", 2, 0, 65, True),
                                             make("LD", 4, 0, 0, True)],
         lambda a, b, c: (a & 0xFFFF00FF) | ((b & 255) << 8), None),
        ("compiled parameter halfword store", [make("SW", 2, 0, 66, True),
                                                 make("LD", 4, 0, 0, True)],
         lambda a, b, c: (a & 65535) | ((b & 65535) << 16), None),
        ("compiled aliased parameter store", [make("SD", 2, 6, 0, True),
                                                make("LD", 4, 0, 0, True)],
         lambda a, b, c: b, None),
        ("compiled aliased parameter byte store", [make("SB", 2, 6, 1, True),
                                                     make("LD", 4, 0, 0, True)],
         lambda a, b, c: (a & 0xFFFF00FF) | ((b & 255) << 8), None),
        ("compiled alternate entry parameter", [make("CBNEQ", 7, 1, 0, True),
                     make("LD", 3, 0, 4, True), make("ADD", 1, 3, 1),
                     make("LABEL", rhs=7, immediate=True), make("LD", 4, 0, 4, True),
                     make("ADD", 4, 4, 1)],
         lambda a, b, c: b + (a if a else b), None),
    ]
    extracts = [make("LSR", reg, 1, 8 * ((reg - 2) % 3 + 1), True) for reg in range(2, 11)]
    extracts += [make("AND", reg, reg, 255, True) for reg in range(2, 11)]
    extracts += [make("MOV", 11, 0, 0, True)]
    extracts += [make("ADD", 11, 11, reg) for reg in range(2, 11)]
    cases.append(("full deferred queue", [*extracts, make("MOV", 4, 11)],
                  lambda a, b, c: 3 * sum(a >> shift & 255 for shift in (8, 16, 24)), ("uxtb", 8)))
    packed_ops = ("UQADD8", "UQSUB8", "UQADD8_RGB", "UQSUB8_RGB",
                  "PACKED_ALPHA_A", "PACKED_ALPHA_B", "PACKED_ALPHA_RGB_A", "PACKED_ALPHA_RGB_B")
    for op in packed_ops:
        if not args.armv5:
            cases.append(("packed " + op, [make(op, 4, 1, 2)],
                          lambda a, b, c, op=op: packed_color(op, a, b), None))
        required = "uxtab16" if op.startswith("PACKED_ALPHA") else op.split("_")[0].lower()
        cases.append(("packed scalar " + op, scalar_blend(op, 3 if "_RGB" in op else 4),
                      lambda a, b, c, op=op: packed_color(op, a, b), required))
    if not args.armv5:
        cases += [
            ("packed UXTB16", [make("UXTB16", 4, 1, 8, True)],
             lambda a, b, c: (a >> 8 | a << 24) & 0x00FF00FF, "uxtb16"),
            ("packed UXTB_ROR", [make("UXTB_ROR", 4, 1, 24, True)],
             lambda a, b, c: a >> 24, "uxtb"),
            ("packed UXTAB16_ROR8", [make("UXTAB16_ROR8", 4, 1, 2)],
             lambda a, b, c: ((a + (b >> 8 & 255)) & 65535)
                              | (((a >> 16) + (b >> 24)) & 65535) << 16, "uxtab16"),
            ("packed scalar different bias", scalar_blend("PACKED_ALPHA_A", wrong_bias=True),
             lambda a, b, c: sum(min((((a >> shift & 255) * (a >> 24) + (a >> 24)) >> 8)
                                   + (((b >> shift & 255) * (255 - (a >> 24))
                                       + (255 - (a >> 24))) >> 8), 255) << shift
                                for shift in (0, 8, 16, 24)), None),
        ]
    if not args.armv5:
        for rotation in (0, 8, 16, 24):
            cases.append(("rotated-byte range " + str(rotation),
                          [make("UXTB_ROR", 7, 1, rotation, True), make("MINS", 4, 7, 255, True)],
                          lambda a, b, c, rotation=rotation: (a >> rotation) & 255, "uxtb"))
    edge = [0, 1, 127, 128, 254, 255, 256, 32767, 32768, 65535,
            0x7FFFFFFF, 0x80000000, 0xFFFF8000, 0xFFFFFFFF]
    rng = random.Random(1136)
    inputs = [(a, b, 0xFFFFFFFF) for a in edge for b in edge]
    inputs += [tuple(rng.getrandbits(32) for _ in range(3)) for _ in range(128)]
    alpha_inputs = [(alpha << 24 | color, (255 - alpha) << 24 | (color ^ 0xFFFFFF), 0)
                    for alpha in range(256) for color in (0, 0xFFFFFF, 0x017FFE, 0x80FE01)]
    saved = {reg: 0x71360000 + reg for reg in range(ARM.UC_ARM_REG_R4, ARM.UC_ARM_REG_R11 + 1)}
    comparisons = 0
    for name, body, expected, required in cases:
        program = [make("LD", 1, 0, 0, True), make("LD", 2, 0, 4, True),
                   make("LD", 6, 0, 8, True), *body, make("SD", 4, 0, 16, True), make("EOP")]
        source = buffer(b"".join(struct.pack("<II", *item) for item in program))
        output = buffer(bytes(65536))
        compiled = name.startswith(("packed ", "compiled "))
        if compiled:
            length = lib.differential_raster_compile(source, 0x3FF, output, 65536, buffer(bytes(16)))
        else:
            length = lib.differential_arm_codegen(source, output, 65536)
        assert 0 < length <= 65536, (name, length)
        emitted = bytes(output)[:length]
        if not args.armv5:
            assert struct.unpack_from("<I", emitted)[0] & 0x100F == 0, name
        operations = [item.mnemonic for item in disassembler.disasm(emitted, code)]
        if name == "nonadjacent zero test" and not args.armv5:
            assert "cmp" not in operations, operations
        if required and not args.armv5:
            mnemonic, count = required if isinstance(required, tuple) else (required, 1)
            assert operations.count(mnemonic) >= count, (name, required, operations)
        machine.mem_write(code, emitted)
        machine.ctl_remove_cache(code, code + 65536)
        for values in inputs + (alpha_inputs if name.startswith("packed ") else []):
            if name.startswith("compiled aliased parameter"):
                values = (values[0], values[1], data)
            machine.mem_write(data, struct.pack("<5I", *values, 0, 0))
            # HC_compile reserves 64 bytes before the ARM parameter block.
            machine.reg_write(ARM.UC_ARM_REG_R0, data - 64 if compiled else data)
            machine.reg_write(ARM.UC_ARM_REG_SP, stack + 65536 - 16)
            machine.reg_write(ARM.UC_ARM_REG_LR, stop)
            for reg, value in saved.items():
                machine.reg_write(reg, value)
            machine.emu_start(code, stop, timeout=100000, count=2000)
            assert machine.reg_read(ARM.UC_ARM_REG_PC) == stop, name
            actual = struct.unpack("<I", machine.mem_read(data + 16, 4))[0]
            assert actual == expected(*values) & 0xFFFFFFFF, (name, values, actual)
            if name.startswith("packed ") and name[7:] in OP:
                rhs = {"UXTB16": 8, "UXTB_ROR": 24}.get(name[7:], values[1])
                emulated = lib.emulator_evalALUOp(OP[name[7:]], values[0], rhs) & 0xFFFFFFFF
                assert emulated == actual, (name, values, emulated, actual)
            assert all(machine.reg_read(reg) == value for reg, value in saved.items()), name
            assert machine.reg_read(ARM.UC_ARM_REG_SP) == stack + 65536 - 16, name
            comparisons += 1
    print(f"ARM JIT: {comparisons} arithmetic and ABI comparisons passed")
    if not args.armv5:
        check_scheduler(lib, machine, code, data, stack, stop)
        check_spans(lib, machine, code, data, stack, stop)


if __name__ == "__main__":
    main()
