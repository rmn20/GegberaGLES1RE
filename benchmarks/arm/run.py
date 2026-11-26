#!/usr/bin/env python3
"""Compare real ARM renderer/JIT instructions with host-emulated Symbian imports."""
from __future__ import annotations

import argparse
import ctypes as C
import csv
from concurrent.futures import ProcessPoolExecutor, as_completed
from enum import IntEnum
import multiprocessing
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import time

from elftools.elf.elffile import ELFFile
import unicorn as U
from unicorn import arm_const as A

HERE = Path(__file__).resolve().parent
BPP = {7: 2, 8: 3, 10: 2, 11: 4, 12: 4, 13: 4}
CPU_MODELS = {'arm926': A.UC_CPU_ARM_926, 'arm1136': A.UC_CPU_ARM_1136}
COUNT_FIELDS = ('instructions', 'pipeline_setup_instructions', 'jit_instructions',
                'os_calls', 'copy_bytes', 'zero_bytes', 'jit_flushes')
GL = IntEnum('GL', {name: int(value, 0) for name, value in re.findall(
    r'^#define GL_([A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$',
    (HERE.parents[1] / 'src/include/GLES/gl.h').read_text(), re.MULTILINE)})
PROJECTIVE_SCENES = ('projective_colors', 'projective_clipped', 'projective_thin',
                     'projective_clamp', 'projective_mipmap', 'projective_bilinear')
SCENES = ('flat', 'blend', 'nearest', 'bilinear', 'overdraw', 'alpha_test', 'fog',
          'multitexture', 'combine', 'polygon_offset', 'perspective', 'small_triangles',
          'alpha_cutout', 'texture_blend', 'texture_rgb565', 'short3_colors',
          'short3_strided', 'float_lighting', 'points', 'lines', 'fog_exp2', 'mixed_overdraw',
          'palette8_alpha', 'stencil', 'float_wide') + PROJECTIVE_SCENES
RECTANGLE_SCENES = set(SCENES) - {'small_triangles', 'short3_colors', 'short3_strided',
                                 'float_lighting', 'float_wide', 'points', 'lines',
                                 'alpha_cutout', 'palette8_alpha'} - set(PROJECTIVE_SCENES)


def counter_library():
    build = HERE / 'build'
    build.mkdir(exist_ok=True)
    source = HERE / 'counter.c'
    library = build / ('counter.dylib' if sys.platform == 'darwin' else 'counter.so')
    if not library.exists() or library.stat().st_mtime < source.stat().st_mtime:
        with tempfile.TemporaryDirectory(prefix='counter-', dir=build) as temporary:
            compiled = Path(temporary) / library.name
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                           ['-O2', '-shared', '-fPIC', str(source), '-o', str(compiled)], check=True)
            os.replace(compiled, library)
    return C.CDLL(str(library))


def words(values):
    return struct.pack('<' + 'I' * len(values), *(x & 0xffffffff for x in values))


class Counter(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in
                ('total', 'jit', 'import_start', 'import_end', 'profile_start',
                 'profile_end', 'profile_jit', 'profile_other')] + [('profile', C.POINTER(C.c_uint64))]
    _fields_ += [('jit_blocks', C.POINTER(C.c_uint64)), ('jit_sizes', C.POINTER(C.c_uint32))]
    _fields_ += [('pipeline_setup', C.c_uint64), ('setup_entries', C.c_uint64 * 8),
                 ('setup_entry_count', C.c_uint32), ('setup_return', C.c_uint32),
                 ('read_register', C.c_void_p), ('link_register', C.c_int)]


class Machine:
    HEAP = 0x20000000
    JIT = 0x30000000
    STUB = 0x7f000000
    RET = STUB + 0xf000
    STACK = 0x70000000

    def __init__(self, path, library, width, height, bpp, *, cpu='arm926', counter='instruction',
                 profile=False, profile_jit=False):
        profile |= profile_jit
        if profile and counter != 'block':
            raise ValueError('Function profiling requires block counting')
        self.uc = U.Uc(U.UC_ARCH_ARM, U.UC_MODE_ARM)
        self.uc.ctl_set_cpu_model(CPU_MODELS[cpu])
        self.symbols = {}
        self.functions = {}
        with path.open('rb') as stream:
            elf = ELFFile(stream)
            segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            low = min(s['p_vaddr'] for s in segments) & ~4095
            high = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
            self.uc.mem_map(low, high - low)
            for seg in segments:
                self.uc.mem_write(seg['p_vaddr'], seg.data())
            for symbol in elf.get_section_by_name('.symtab').iter_symbols():
                if symbol['st_shndx'] != 'SHN_UNDEF':
                    self.symbols[symbol.name] = symbol['st_value']
                    if symbol['st_info']['type'] == 'STT_FUNC' and symbol['st_size']:
                        self.functions.setdefault(symbol['st_value'],
                                                  (symbol.name, symbol['st_size']))
        for addr, size in [(self.HEAP, 64 << 20), (self.JIT, 8 << 20),
                           (self.STACK - (1 << 20), 1 << 20), (self.STUB, 65536)]:
            self.uc.mem_map(addr, size)
        self.heap_next = self.HEAP + 4096
        self.jit_next = self.JIT
        self.allocations = {}
        self.chunks = {}
        self.tls = {}
        self.bitmaps = {}
        self.width, self.height = width, height
        self.mode = 7 if bpp == 16 else 11
        self.last_bitmap = 0
        self.os_calls = self.copy_bytes = self.zero_bytes = self.imb_calls = 0
        self.api_calls = 0
        self.failure = None
        start, end = (self.symbols[n] for n in ('bench_import_start', 'bench_import_end'))
        self.imports = {addr: name for name, addr in self.symbols.items()
                        if start <= addr < end and not name.startswith(('bench_', '$'))}
        self.uc.hook_add(U.UC_HOOK_CODE, self.os_hook, begin=start, end=end - 1)
        self.uc.hook_add(U.UC_HOOK_CODE, self.os_hook, begin=self.STUB, end=self.RET - 1)
        self.stub_next = self.STUB
        self.heap_vtable = self.alloc(32)
        for index, name in [(0, 'heap_alloc'), (1, 'heap_free'), (2, 'heap_realloc'), (4, 'noop')]:
            self.uc.mem_write(self.heap_vtable + 4 * index, words([self.stub(name)]))
        self.heap_object = self.put(words([self.heap_vtable]))
        self.base_vtable = self.put(words([0, self.stub('noop')]))
        self.gc_vtable = self.alloc(320)
        for index, name in [(1, 'noop'), (57, 'bitblt'), (67, 'noop'), (68, 'noop'), (69, 'noop')]:
            self.uc.mem_write(self.gc_vtable + 4 * index, words([self.stub(name)]))
        self.coe = self.alloc(0x98)
        self.uc.mem_write(self.coe + 72, words([0xcafe0001]))
        self.counter = Counter(0, 0, start, end)
        self.profile_buffer = None
        self.jit_blocks = self.jit_sizes = None
        if profile:
            self.counter.profile_start, self.counter.profile_end = low, high
            self.profile_buffer = (C.c_uint64 * ((high - low) // 4))()
            if profile_jit:
                self.jit_blocks = (C.c_uint64 * ((8 << 20) // 4))()
                self.jit_sizes = (C.c_uint32 * len(self.jit_blocks))()
                self.counter.jit_blocks, self.counter.jit_sizes = self.jit_blocks, self.jit_sizes
        self.library = library
        self.uclib = U.Uc.hook_add.__globals__['uclib']
        # Inclusive pipeline validation: signature lookup, plus compilation
        # and executable-cache insertion on a miss. It excludes pixel execution.
        entries = sorted({addr for name, addr in self.symbols.items()
                          if name == 'grEnsureValidPixelPipeline' or
                          name.startswith('grEnsureValidPixelPipeline.')})
        if len(entries) > len(self.counter.setup_entries):
            raise RuntimeError('Too many pipeline setup entry points')
        self.counter.setup_entry_count = len(entries)
        self.counter.setup_entries[:len(entries)] = entries
        self.counter.read_register = C.cast(self.uclib.uc_reg_read, C.c_void_p).value
        self.counter.link_register = A.UC_ARM_REG_LR
        handle = C.c_size_t()
        hook, callback = {'instruction': (U.UC_HOOK_CODE, library.count_instruction),
                          'block': (U.UC_HOOK_BLOCK, library.count_block)}[counter]
        if profile:
            callback = library.profile_block
        status = self.uclib.uc_hook_add(self.uc._uch, C.byref(handle), hook,
                    C.cast(callback, C.c_void_p), C.byref(self.counter),
                    C.c_uint64(1), C.c_uint64(0))
        if status:
            raise RuntimeError(f'Counter hook: {status}')
        self.uc._callbacks[handle.value] = callback

    def alloc(self, size):
        size = max(8, (size + 7) & ~7)
        addr = self.heap_next
        self.heap_next += size + 8
        if self.heap_next > self.HEAP + (64 << 20):
            raise RuntimeError('Harness heap exhausted')
        self.allocations[addr] = size
        return addr

    def put(self, data):
        addr = self.alloc(len(data))
        self.uc.mem_write(addr, bytes(data))
        return addr

    def readword(self, addr):
        return struct.unpack('<I', self.uc.mem_read(addr, 4))[0]

    def stub(self, name):
        addr = self.stub_next
        self.stub_next += 4
        self.imports[addr] = name
        self.uc.mem_write(addr, words([0xe12fff1e]))
        return addr

    def os_hook(self, uc, address, size, data):
        try:
            result = self.os_call(self.imports[address])
            if result is not None:
                uc.reg_write(A.UC_ARM_REG_R0, result & 0xffffffff)
        except BaseException as error:
            self.failure = error
            uc.emu_stop()

    def os_call(self, name):
        self.os_calls += 1
        r0, r1, r2, r3 = [self.uc.reg_read(A.UC_ARM_REG_R0 + i) for i in range(4)]
        if name in ('sym_UserSvr_DllTls',):
            return self.tls.get(r0, 0)
        if name == 'sym_UserSvr_DllSetTls':
            self.tls[r0] = r2
            return 0
        if name == 'sym_UserSvr_DllFreeTls':
            self.tls.pop(r0, None)
            return 0
        if name in ('sym_User_Alloc', 'sym_User_AllocZ', 'sym_User_AllocZL', 'heap_alloc'):
            return self.alloc(r1 if name == 'heap_alloc' else r0)
        if name == 'heap_realloc':
            addr = self.alloc(r2)
            if r1:
                self.uc.mem_write(addr, bytes(self.uc.mem_read(r1, min(self.allocations[r1], r2))))
            return addr
        if name == 'sym_UserHeap_ChunkHeap':
            return self.heap_object
        if name == 'sym_RChunk_CreateLocalCode':
            self.chunks[r0] = (self.jit_next, r1)
            self.jit_next += (r2 + 4095) & ~4095
            if self.jit_next > self.JIT + (8 << 20):
                raise RuntimeError('Harness JIT heap exhausted')
            self.uc.mem_write(r0, words([len(self.chunks)]))
            return 0
        if name in ('sym_RChunk_Base', 'sym_RChunk_Size'):
            return self.chunks[r0][name == 'sym_RChunk_Size']
        if name == 'sym_User_IMB_Range':
            self.imb_calls += 1
            self.uc.ctl_remove_cache(r0, r1)
            return 0
        if name == 'sym_memclr':
            self.zero_bytes += r1
            self.uc.mem_write(r0, b'\0' * r1)
            return r0
        if name in ('sym_memmove', 'sym_wordmove'):
            self.copy_bytes += r2
            self.uc.mem_write(r0, bytes(self.uc.mem_read(r1, r2)))
            return r0
        if name == 'sym_memcompare':
            a, b = bytes(self.uc.mem_read(r0, r1)), bytes(self.uc.mem_read(r2, r3))
            return (a > b) - (a < b)
        if name == 'sym_HAL_Get':
            self.uc.mem_write(r1, words([32 << 20]))
            return 0
        if name == 'sym_CCoeEnv_Static':
            return self.coe
        if name == 'sym_RWsSession_GetDefModeMaxNumColors':
            return self.mode
        if name == 'sym_CWindowGc_Ctor':
            self.uc.mem_write(r0, words([self.gc_vtable]))
            return r0
        if name in ('sym_CWsBitmap_Ctor', 'sym_RWindow_Ctor'):
            self.uc.mem_write(r0, words([self.base_vtable]))
            return r0
        if name in ('sym_CWsBitmap_Create', 'sym_CFbsBitmap_Resize'):
            w, h = struct.unpack('<ii', self.uc.mem_read(r1, 8))
            mode = r2 if name == 'sym_CWsBitmap_Create' else self.bitmaps[r0]['mode']
            stride = (w * BPP[mode] + 3) & ~3
            self.bitmaps[r0] = dict(w=w, h=h, mode=mode, stride=stride,
                                    data=self.alloc(stride * h), locked=0)
            return 0
        if name == 'sym_RWindowBase_Size':
            self.uc.mem_write(r0, words([self.width, self.height]))
            return 0
        if name == 'sym_RDrawableWindow_GetDrawRect':
            self.uc.mem_write(r0, words([0, 0, self.width, self.height]))
            return 0
        if name == 'sym_TRect_IsEmpty':
            return 0
        if name == 'sym_CFbsBitmap_SizeInPixels':
            b = self.bitmaps[r1]
            self.uc.mem_write(r0, words([b['w'], b['h']]))
            return 0
        if name == 'sym_CFbsBitmap_ScanLineLength':
            return (r0 * BPP[r1] + 3) & ~3
        if name in ('sym_CFbsBitmap_DisplayMode', 'sym_CFbsBitmap_DataAddress'):
            return self.bitmaps[r0]['mode' if name.endswith('DisplayMode') else 'data']
        if name in ('sym_CFbsBitmap_LockHeap', 'sym_CFbsBitmap_UnlockHeap'):
            self.bitmaps[r0]['locked'] += 1 if name.endswith('_LockHeap') else -1
            return 0
        if name == 'bitblt':
            self.last_bitmap = r2
            if self.bitmaps[r2]['locked']:
                raise RuntimeError('Locked bitmap at presentation')
            return 0
        if name in ('noop', 'heap_free', 'sym_User_Free', 'sym_RAllocator_Close',
                    'sym_RHandleBase_Close', 'sym_User_MarkCleanupStack',
                    'sym_User_UnMarkCleanupStack', 'sym_CWsBitmap_Reset',
                    'sym_CFbsBitmap_Reset', 'sym_RWindowTreeNode_Close',
                    'sym_RWindow_Invalidate', 'sym_RWindow_BeginRedraw',
                    'sym_RWindow_EndRedraw', 'sym_RWindow_Construct'):
            return 0
        raise RuntimeError(f'Unhandled OS call {name}({r0:#x},{r1:#x},{r2:#x},{r3:#x})')

    def call(self, name, *args):
        self.api_calls += 1
        addr = self.symbols[name] if isinstance(name, str) else name
        sp = self.STACK - 4096
        self.uc.reg_write(A.UC_ARM_REG_CPSR, 0x10)
        for i, value in enumerate(args[:4]):
            self.uc.reg_write(A.UC_ARM_REG_R0 + i, value & 0xffffffff)
        if len(args) > 4:
            self.uc.mem_write(sp, words(args[4:]))
        self.uc.reg_write(A.UC_ARM_REG_SP, sp)
        self.uc.reg_write(A.UC_ARM_REG_LR, self.RET)
        try:
            self.uc.emu_start(addr, self.RET, timeout=30_000_000)
        except U.UcError as error:
            raise RuntimeError(f'{name}: {error}, pc={self.uc.reg_read(A.UC_ARM_REG_PC):#x}') from error
        if self.failure:
            raise self.failure
        if self.uc.reg_read(A.UC_ARM_REG_PC) != self.RET:
            raise RuntimeError(f'{name}: emulation timed out')
        if self.counter.setup_return == self.RET:
            self.counter.setup_return = 0
        elif self.counter.setup_return:
            raise RuntimeError(f'{name}: pipeline setup scope did not return normally')
        return self.uc.reg_read(A.UC_ARM_REG_R0)

    def reset_counts(self):
        self.counter.total = self.counter.jit = self.counter.pipeline_setup = 0
        self.counter.setup_return = 0
        self.os_calls = self.copy_bytes = self.zero_bytes = self.imb_calls = 0
        self.api_calls = 0

    def counts(self):
        return dict(instructions=self.counter.total,
                    pipeline_setup_instructions=self.counter.pipeline_setup,
                    jit_instructions=self.counter.jit,
                    os_calls=self.os_calls, copy_bytes=self.copy_bytes,
                    zero_bytes=self.zero_bytes, jit_flushes=self.imb_calls)

    def profile_enable(self, enabled):
        if self.profile_buffer is not None:
            self.counter.profile = (C.cast(self.profile_buffer, C.POINTER(C.c_uint64))
                                    if enabled else C.POINTER(C.c_uint64)())

    def reset_profile(self):
        if self.profile_buffer is not None:
            C.memset(self.profile_buffer, 0, C.sizeof(self.profile_buffer))
            self.counter.profile_jit = self.counter.profile_other = 0
            if self.jit_blocks is not None:
                C.memset(self.jit_blocks, 0, C.sizeof(self.jit_blocks))

    def profile_rows(self):
        if self.profile_buffer is None:
            return []
        rows = []
        attributed = 0
        for address, (name, size) in self.functions.items():
            start = (address - self.counter.profile_start) // 4
            end = start + (size + 3) // 4
            instructions = sum(self.profile_buffer[start:end])
            if instructions:
                rows.append(dict(function=name, instructions=instructions))
                attributed += instructions
        unknown = sum(self.profile_buffer) - attributed + self.counter.profile_other
        if unknown:
            rows.append(dict(function='<unattributed>', instructions=unknown))
        if self.counter.profile_jit:
            rows.append(dict(function='<generated pixel pipelines>',
                             instructions=self.counter.profile_jit))
        return sorted(rows, key=lambda row: row['instructions'], reverse=True)

    def jit_profile_text(self, limit=20):
        if self.jit_blocks is None:
            return ''
        import capstone
        decoder = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        blocks = sorted(((count, index) for index, count in enumerate(self.jit_blocks) if count),
                        reverse=True)
        lines = ['Warm scene generated-code blocks, ranked by dynamic instruction slots.']
        for count, index in blocks[:limit]:
            address, size = self.JIT + index * 4, self.jit_sizes[index]
            lines.append(f'\n{address:#010x}: {count:,} instructions, {count // (size // 4):,} executions')
            for instruction in decoder.disasm(bytes(self.uc.mem_read(address, size)), address):
                lines.append(f'  {instruction.address:08x}  {instruction.mnemonic:8s} {instruction.op_str}')
        return '\n'.join(lines) + '\n'

    def verify_counter(self):
        # MOV/CMP, one false and one true predicated MOV, then BX LR.
        # Counts are instruction slots, including condition-failed ARM words.
        code = self.put(words([0xe3a00000, 0xe3500000, 0x13a01001, 0x03a01002, 0xe12fff1e]))
        self.reset_counts()
        self.call(code)
        assert self.counter.total == 5 and self.counter.jit == 0
        assert self.uc.reg_read(A.UC_ARM_REG_R1) == 2
        # A counted loop and a generated-code block verify branch boundaries
        # and the separate JIT range without relying on renderer output.
        loop = self.put(words([0xe3a00003, 0xe2500001, 0x1afffffd, 0xe12fff1e]))
        self.reset_counts()
        self.call(loop)
        assert self.counter.total == 8 and self.counter.jit == 0
        entries = list(self.counter.setup_entries)
        entry_count = self.counter.setup_entry_count
        self.counter.setup_entries[0] = loop
        self.counter.setup_entry_count = 1
        caller = self.alloc(12)
        branch = 0xeb000000 | (((loop - (caller + 12)) // 4) & 0xffffff)
        self.uc.mem_write(caller, words([0xe92d4000, branch, 0xe8bd8000]))
        self.reset_counts()
        self.call(caller)
        assert self.counter.total == 11 and self.counter.pipeline_setup == 8
        self.counter.setup_entries[1] = caller
        self.counter.setup_entry_count = 2
        self.reset_counts()
        self.call(caller)
        assert self.counter.total == self.counter.pipeline_setup == 11
        self.counter.setup_entries[:] = entries
        self.counter.setup_entry_count = entry_count
        jit_probe = self.JIT + (8 << 20) - 4096
        self.uc.mem_write(jit_probe, words([0xe3a00000, 0xe12fff1e]))
        self.reset_counts()
        self.call(jit_probe)
        assert self.counter.total == self.counter.jit == 2

    def initialize(self, bpp, window=False, *, stencil=False):
        self.dpy = self.call('eglGetDisplay', 0)
        assert self.call('eglInitialize', self.dpy, 0, 0) == 1
        cfgs, output = self.alloc(1024), self.alloc(4)
        assert self.call('eglGetConfigs', self.dpy, cfgs, 256, output) == 1
        configs = struct.unpack('<' + 'I' * self.readword(output),
                               self.uc.mem_read(cfgs, 4 * self.readword(output)))
        target = (5, 6, 5) if bpp == 16 else (8, 8, 8)
        choices = []
        for cfg in configs:
            attrs = []
            for attr in (0x3024, 0x3023, 0x3022, 0x3021, 0x3025, 0x3026, 0x3031, 0x3033):
                assert self.call('eglGetConfigAttrib', self.dpy, cfg, attr, output) == 1
                attrs.append(self.readword(output))
            if (tuple(attrs[:3]) == target and attrs[4] == 16 and attrs[6] <= 1
                    and attrs[7] & (4 if window else 1) and (not stencil or attrs[5] >= 8)):
                choices.append((attrs[3], attrs[5], cfg, attrs))
        if not choices:
            raise RuntimeError(f'No {target}/Z16' + ('/S8' if stencil else '') + ' config')
        _, _, cfg, self.config = min(choices)
        if window:
            self.surface = self.call('eglCreateWindowSurface', self.dpy, cfg, self.alloc(32), 0)
        else:
            attrs = self.put(words([0x3057, self.width, 0x3056, self.height, 0x3038]))
            self.surface = self.call('eglCreatePbufferSurface', self.dpy, cfg, attrs)
        context = self.call('eglCreateContext', self.dpy, cfg, 0, 0)
        assert self.surface and context, (self.surface, context, self.call('eglGetError'))
        assert self.call('eglMakeCurrent', self.dpy, self.surface, self.surface, context) == 1
        self.call('glViewport', 0, 0, self.width, self.height)
        self.call('glDisable', 0x0bd0)  # dither: same output precision for both
        self.call('glClearColorx', 0, 0, 0, 65536)
        self.call('glClearDepthx', 65536)

    def pixels(self):
        buffer = self.alloc(self.width * self.height * 4)
        self.call('glReadPixels', 0, 0, self.width, self.height, 0x1908, 0x1401, buffer)
        assert self.call('glGetError') == 0
        return bytes(self.uc.mem_read(buffer, self.width * self.height * 4))

    def verify_presentation(self, pixels):
        b = self.bitmaps[self.last_bitmap]
        raw = self.uc.mem_read(b['data'], b['stride'] * b['h'])
        for y in range(self.height):
            for x in range(self.width):
                # glReadPixels starts at the lower left; Symbian bitmaps at top left.
                offset = (self.height-y-1)*b['stride'] + x*BPP[b['mode']]
                if b['mode'] == 7:
                    value = raw[offset] | raw[offset+1] << 8
                    r, g, blue = value >> 11, (value >> 5) & 63, value & 31
                    rgb = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (blue << 3) | (blue >> 2))
                else:
                    rgb = tuple(raw[offset+2-c] for c in range(3))
                pos = (y*self.width+x)*4
                assert max(abs(rgb[c]-pixels[pos+c]) for c in range(3)) <= 1


def fixed(values):
    return words([round(v * 65536) for v in values])


def setup_projective_scene(m, name):
    """Homogeneous meshes with varying W, shared edges and smooth vertex colors."""
    c = m.call
    vertices, uv, colors, indices = [], [], [], []

    def vertex(x, y, z, w, s, t):
        index = len(vertices) // 4
        vertices.extend((x * w, y * w, z * w, w))
        uv.extend((s, t))
        colors.extend((48 + index * 29 % 208, 48 + index * 67 % 208,
                       48 + index * 43 % 208, 255))
        return index

    if name == 'projective_thin':
        for i in range(12):
            x = -.85 + i * .15
            base = vertex(x, -.85, .1, .25, -2, 0)
            vertex(x + .035, .8, .6, 4, 5, 2)
            vertex(x + .065, -.75, -.2, 1, 0, -1)
            indices.extend((base, base + 1, base + 2))
    else:
        columns, rows = 4, 3
        extent = 1.2 if name == 'projective_clipped' else .85
        for y in range(rows + 1):
            for x in range(columns + 1):
                w = (.25, .5, 1, 2, 4)[(x + y * 3) % 5]
                z = ((x + y) % 3 - 1) * (.99 if name == 'projective_clipped' else .5)
                vertex(-extent + x * 2 * extent / columns,
                       -extent + y * 2 * extent / rows, z, w,
                       x * 1.25 - .25, y * 1.5 - .5)
        for y in range(rows):
            for x in range(columns):
                base = y * (columns + 1) + x
                indices.extend((base, base + 1, base + columns + 1,
                                base + columns + 1, base + 1, base + columns + 2))

    for mode in (GL.PROJECTION, GL.MODELVIEW, GL.TEXTURE):
        c('glMatrixMode', mode)
        c('glLoadIdentity')
    c('glMatrixMode', GL.MODELVIEW)
    c('glVertexPointer', 4, GL.FIXED, 0, m.put(fixed(vertices)))
    c('glColorPointer', 4, GL.UNSIGNED_BYTE, 0, m.put(bytes(colors)))
    c('glTexCoordPointer', 2, GL.FIXED, 0, m.put(fixed(uv)))
    for array in (GL.VERTEX_ARRAY, GL.COLOR_ARRAY, GL.TEXTURE_COORD_ARRAY):
        c('glEnableClientState', array)
    c('glEnable', GL.DEPTH_TEST)
    c('glDepthFunc', GL.LEQUAL)
    c('glEnable', GL.TEXTURE_2D)
    c('glTexEnvi', GL.TEXTURE_ENV, GL.TEXTURE_ENV_MODE, GL.MODULATE)
    c('glHint', GL.PERSPECTIVE_CORRECTION_HINT, GL.NICEST)
    c('glShadeModel', GL.SMOOTH)
    texture = m.alloc(4)
    c('glGenTextures', 1, texture)
    c('glBindTexture', GL.TEXTURE_2D, m.readword(texture))
    linear = name == 'projective_bilinear'
    c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER,
      GL.LINEAR if linear else GL.NEAREST)
    c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER,
      GL.NEAREST_MIPMAP_NEAREST if name == 'projective_mipmap' else
      GL.LINEAR if linear else GL.NEAREST)
    c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_WRAP_S,
      GL.CLAMP_TO_EDGE if name == 'projective_clamp' else GL.REPEAT)
    c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_WRAP_T,
      GL.CLAMP_TO_EDGE if name == 'projective_clamp' else GL.REPEAT)
    for level in range(6 if name == 'projective_mipmap' else 1):
        size = 32 >> level
        texels = bytes(component for y in range(size) for x in range(size)
                       for component in ((x * 37 + level * 43) & 255,
                                         (y * 53 + level * 29) & 255,
                                         224 if (x ^ y) & 1 else 32, 255))
        c('glTexImage2D', GL.TEXTURE_2D, level, GL.RGBA, size, size, 0,
          GL.RGBA, GL.UNSIGNED_BYTE, m.put(texels))
    if name == 'projective_clipped':
        c('glEnable', GL.SCISSOR_TEST)
        c('glScissor', m.width // 8, m.height // 8, m.width * 3 // 4, m.height * 3 // 4)
        c('glClipPlanex', GL.CLIP_PLANE0, m.put(fixed((1, .5, 0, .75))))
        c('glEnable', GL.CLIP_PLANE0)
    index_data = m.put(struct.pack('<' + 'H' * len(indices), *indices))
    assert c('glGetError') == 0
    return lambda: c('glDrawElements', GL.TRIANGLES, len(indices), GL.UNSIGNED_SHORT, index_data)


def setup_scene(m, name):
    if name in PROJECTIVE_SCENES:
        return setup_projective_scene(m, name)
    c = m.call
    c('glMatrixMode', 0x1701)
    c('glLoadIdentity')
    c('glMatrixMode', 0x1700)
    c('glLoadIdentity')
    c('glEnableClientState', 0x8074)
    c('glColor4x', 65536, 65536, 65536, 65536)
    # The two triangles cover a 3/4-width by 3/4-height rectangle exactly.
    v = [-.75,-.75,0, .75,-.75,0, -.75,.75,0, .75,.75,0]
    uv = [0,0, 1,0, 0,1, 1,1]
    if name == 'palette8_alpha':
        uv = [-1.25,-.25, 3.75,-.25, -1.25,1.25, 3.75,1.25]
    indices = [0,1,2, 2,1,3]
    if name in ('small_triangles', 'short3_colors', 'short3_strided', 'float_lighting'):
        v, uv, indices = [], [], []
        for y in range(12):
            for x in range(16):
                x0, y0 = -0.9 + x * 1.8/16, -0.9 + y * 1.8/12
                base = len(v)//3
                v += [x0,y0,0, x0+.08,y0,0, x0,y0+.10,0]
                uv += [0,0, 1,0, 0,1]
                indices += [base,base+1,base+2]
    mode = GL.TRIANGLES
    if name == 'points':
        v = [component for y in range(8) for x in range(12)
             for component in (-.85 + x * 1.7 / 11, -.85 + y * 1.7 / 7, 0)]
        indices = list(range(len(v) // 3))
        mode = GL.POINTS
        c('glPointSizex', 4 * 65536)
    elif name == 'lines':
        v = [component for i in range(17) for component in
             (-1.2, -.9 + i * .1125, 0, 1.2, .9 - i * .1125, 0)]
        indices = list(range(len(v) // 3))
        mode = GL.LINES
        c('glLineWidthx', 2 * 65536)
    elif name == 'fog_exp2':
        for i in range(len(v) // 3):
            v[3 * i + 2] = -.1 if i % 2 == 0 else -.9
    if name == 'perspective':
        c('glMatrixMode', 0x1701)
        c('glFrustumx', -65536, 65536, -65536, 65536, 65536, 8*65536)
        c('glMatrixMode', 0x1700)
        c('glHint', 0x0c50, 0x1102)  # GL_NICEST
        v = [-.75,-.75,-1, 3,-3,-4, -.75,.75,-1, 3,3,-4]
    if name == 'float_wide':
        # Consecutive vertices span nine exponent groups under a dense transform.
        v = [component for i in range(192 * 3) for component in
             tuple((-1 if i & (1 << axis) else 1) * weight * 2 ** (-1 - i % 9)
                   for axis, weight in enumerate((1., .75, .5)))]
        indices = list(range(len(v) // 3))
        c('glLoadMatrixx', m.put(fixed([.9, .1, .05, .0625, .15, .85, .1, .03125,
                                       .05, -.1, .8, -.03125, .125, -.125, 0, 1])))
    vertex_type, stride, vertices = GL.FIXED, 0, fixed(v)
    if name in ('short3_colors', 'short3_strided'):
        vertex_type = GL.SHORT
        stride = 8 if name == 'short3_strided' else 6
        vertices = b''.join(struct.pack('<hhh', *(round(x * 4096) for x in v[i:i + 3])) +
                            (b'\xa5\xa5' if stride == 8 else b'')
                            for i in range(0, len(v), 3))
        c('glScalex', 16, 16, 16)
    elif name in ('float_lighting', 'float_wide'):
        vertex_type = GL.FLOAT
        vertices = struct.pack('<' + 'f' * len(v), *v)
    if name == 'float_lighting':
        normals = [component for i in range(len(v) // 3)
                   for component in (-.4 + .1 * (i % 9), .15, 1.)]
        c('glNormalPointer', GL.FLOAT, 0,
          m.put(struct.pack('<' + 'f' * len(normals), *normals)))
        c('glEnableClientState', GL.NORMAL_ARRAY)
        c('glEnable', GL.LIGHTING)
        c('glEnable', GL.LIGHT0)
        c('glEnable', GL.NORMALIZE)
        c('glLightxv', GL.LIGHT0, GL.POSITION, m.put(fixed([0, 0, 1, 0])))
        c('glMaterialxv', GL.FRONT_AND_BACK, GL.AMBIENT_AND_DIFFUSE,
          m.put(fixed([.8, .6, .4, 1])))
    vp = m.put(vertices)
    c('glVertexPointer', 3, vertex_type, stride, vp)
    if name in ('short3_colors', 'short3_strided', 'points', 'lines', 'float_wide'):
        colors = bytes(component for i in range(len(v) // 3)
                       for component in (64 + (i * 31) % 192, 64 + (i * 73) % 192,
                                         64 + (i * 17) % 192, 255))
        c('glColorPointer', 4, GL.UNSIGNED_BYTE, 0, m.put(colors))
        c('glEnableClientState', GL.COLOR_ARRAY)
    index_type = GL.UNSIGNED_BYTE if name in ('points', 'lines') else GL.UNSIGNED_SHORT
    ip = m.put(bytes(indices) if index_type == GL.UNSIGNED_BYTE else
               struct.pack('<' + 'H' * len(indices), *indices))
    textured = name not in ('flat', 'blend', 'points', 'lines', 'stencil', 'float_wide')
    if textured:
        c('glEnable', 0x0de1)
        c('glEnableClientState', 0x8078)
        c('glTexCoordPointer', 2, 0x140c, 0, m.put(fixed(uv)))
        tid = m.alloc(4)
        c('glGenTextures', 1, tid)
        c('glBindTexture', 0x0de1, m.readword(tid))
        filt = 0x2601 if name == 'bilinear' else 0x2600
        c('glTexParameteri', 0x0de1, 0x2801, filt)
        c('glTexParameteri', 0x0de1, 0x2800, filt)
        if name == 'palette8_alpha':
            palette = bytes(component for i in range(256) for component in
                            ((i * 73) & 255, (i * 31) & 255, (i * 17) & 255,
                             (0, 96, 192, 255)[i % 4]))
            tex = palette + bytes((x * 17 + y * 37) & 255 for y in range(32) for x in range(32))
            c('glCompressedTexImage2D', GL.TEXTURE_2D, 0, GL.PALETTE8_RGBA8_OES,
              32, 32, 0, len(tex), m.put(tex))
            c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, GL.REPEAT)
            c('glTexParameteri', GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, GL.CLAMP_TO_EDGE)
        else:
            tex = bytearray()
            for y in range(32):
                for x in range(32):
                    light = ((x // 4) ^ (y // 4)) & 1
                    rgba = (240, 160, 80, 255) if light else (64, 128, 224, 255)
                    if name == 'alpha_cutout':
                        rgba = (*rgba[:3], 255 if light else 0)
                    elif name == 'texture_blend':
                        rgba = (*rgba[:3], 192 if light else 64)
                    if name == 'texture_rgb565':
                        r, g, b, _ = rgba
                        tex += struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
                    else:
                        tex += bytes(rgba)
            texture_format = GL.RGB if name == 'texture_rgb565' else GL.RGBA
            texture_type = GL.UNSIGNED_SHORT_5_6_5 if name == 'texture_rgb565' else GL.UNSIGNED_BYTE
            c('glTexImage2D', GL.TEXTURE_2D, 0, texture_format, 32, 32, 0,
              texture_format, texture_type, m.put(tex))
    if name not in ('flat', 'blend', 'stencil'):
        c('glEnable', 0x0b71)
        c('glDepthFunc', 0x0203)  # LEQUAL, warmed redraw stays visible
    if name in ('blend', 'texture_blend'):
        c('glEnable', 0x0be2)
        c('glBlendFunc', 0x0302, 0x0303)
        c('glColor4x', 65536, 32768, 16384, 32768)
    if name in ('alpha_test', 'alpha_cutout', 'palette8_alpha'):
        c('glEnable', 0x0bc0)
        c('glAlphaFuncx', 0x0204, 32768)
    if name == 'fog':
        c('glEnable', 0x0b60)
        c('glFogx', 0x0b65, 0x2601)
        c('glFogx', 0x0b63, -65536)
        c('glFogx', 0x0b64, 65536)
        c('glFogxv', 0x0b66, m.put(words([16384, 32768, 49152, 65536])))
    if name == 'fog_exp2':
        c('glEnable', GL.FOG)
        c('glFogx', GL.FOG_MODE, GL.EXP2)
        c('glFogx', GL.FOG_DENSITY, 2 * 65536)
        c('glFogxv', GL.FOG_COLOR, m.put(fixed([.25, .5, .75, 1])))
    if name == 'multitexture':
        c('glActiveTexture', 0x84c1)
        c('glEnable', 0x0de1)
        c('glBindTexture', 0x0de1, m.readword(tid))
        c('glClientActiveTexture', 0x84c1)
        c('glEnableClientState', 0x8078)
        c('glTexCoordPointer', 2, 0x140c, 0, m.put(fixed(uv)))
        c('glActiveTexture', 0x84c0)
        c('glClientActiveTexture', 0x84c0)
    if name == 'combine':
        c('glTexEnvi', 0x2300, 0x2200, 0x8570)  # COMBINE
        c('glTexEnvi', 0x2300, 0x8571, 0x0104)  # ADD
        # The first texture stage adds texture RGB to the primary color.
        c('glTexEnvi', 0x2300, 0x8580, 0x1702)  # SOURCE0_RGB = TEXTURE
        c('glTexEnvi', 0x2300, 0x8581, 0x8578)  # SOURCE1_RGB = PREVIOUS
        c('glTexEnvi', 0x2300, 0x8590, 0x0300)  # OPERAND0_RGB = SRC_COLOR
        c('glTexEnvi', 0x2300, 0x8591, 0x0300)
        c('glTexEnvi', 0x2300, 0x8573, 1)
        c('glTexEnvi', 0x2300, 0x8572, 0x1e01)  # COMBINE_ALPHA = REPLACE
        c('glTexEnvi', 0x2300, 0x8588, 0x1702)
        c('glTexEnvi', 0x2300, 0x8598, 0x0302)  # OPERAND0_ALPHA = SRC_ALPHA
        c('glColor4x', 8192, 8192, 8192, 65536)
    if name == 'polygon_offset':
        c('glEnable', 0x8037)
        c('glPolygonOffsetx', 65536, 65536)
    if name == 'stencil':
        inner = m.put(fixed([-.5,-.5,0, .5,-.5,0, -.5,.5,0, .5,.5,0]))
        c('glEnable', GL.STENCIL_TEST)
        c('glClearStencil', 0)
        def draw():
            c('glColor4x', 0, 0, 65536, 65536)
            c('glColorMask', 0, 0, 0, 0)
            c('glStencilFunc', GL.ALWAYS, 1, 255)
            c('glStencilOp', GL.KEEP, GL.KEEP, GL.REPLACE)
            c('glVertexPointer', 3, GL.FIXED, 0, inner)
            c('glDrawElements', mode, len(indices), index_type, ip)
            c('glColorMask', 1, 1, 1, 1)
            c('glVertexPointer', 3, GL.FIXED, 0, vp)
            c('glStencilFunc', GL.EQUAL, 1, 255)
            c('glStencilOp', GL.KEEP, GL.KEEP, GL.INCR)
            c('glDrawElements', mode, len(indices), index_type, ip)
            c('glStencilFunc', GL.EQUAL, 2, 255)
            c('glStencilOp', GL.INVERT, GL.KEEP, GL.KEEP)
            c('glDrawElements', mode, len(indices), index_type, ip)
            c('glColor4x', 65536, 0, 0, 65536)
            c('glStencilFunc', GL.EQUAL, 255, 255)
            c('glStencilOp', GL.KEEP, GL.KEEP, GL.KEEP)
            c('glDrawElements', mode, len(indices), index_type, ip)
    elif name == 'mixed_overdraw':
        stripes = []
        for i in range(0, 24, 2):
            left, right = -.75 + i / 16, -.75 + (i + 1) / 16
            stripes.extend([left, -.75, -.5, right, -.75, -.5, left, .75, -.5,
                            left, .75, -.5, right, -.75, -.5, right, .75, -.5])
        occluders = m.put(fixed(stripes))
        c('glEnable', GL.DITHER)
        def draw():
            c('glDisable', GL.TEXTURE_2D)
            c('glVertexPointer', 3, GL.FIXED, 0, occluders)
            c('glDrawArrays', GL.TRIANGLES, 0, len(stripes) // 3)
            c('glEnable', GL.TEXTURE_2D)
            c('glVertexPointer', 3, GL.FIXED, 0, vp)
            c('glDrawElements', mode, len(indices), index_type, ip)
    else:
        def draw():
            c('glDrawElements', mode, len(indices), index_type, ip)
    assert c('glGetError') == 0
    return draw


def run_case(renderer, library, width, height, bpp, scene, window, *, elf=None,
             cpu='arm1136', counter='block', profile_dir=None, profile_jit=False):
    m = Machine(elf or HERE/'build'/renderer/(renderer+'.elf'), library, width, height, bpp,
                cpu=cpu, counter=counter, profile=profile_dir is not None, profile_jit=profile_jit)
    m.verify_counter()
    draw = None
    def draw_and_finish():
        draw()
        m.call('glFinish')
    rows = []
    def measure(phase, fn):
        m.reset_counts()
        fn()
        result = dict(renderer=renderer, width=width, height=height, bpp=bpp,
                      scene=scene, surface='window' if window else 'pbuffer', phase=phase,
                      cpu=cpu, counter=counter,
                      alpha_bits=m.config[3], depth_bits=m.config[4], stencil_bits=m.config[5],
                      **m.counts())
        rows.append(result)
        return result
    measure('context_setup', lambda: m.initialize(bpp, window, stencil=scene == 'stencil'))
    def setup_resources():
        nonlocal draw
        draw = setup_scene(m, scene)
    measure('resource_setup', setup_resources)
    # Flush deferred clears outside the draw measurement in both renderers.
    def clear():
        mask = GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT
        if scene == 'stencil':
            mask |= GL.STENCIL_BUFFER_BIT
        m.call('glClear', mask)
        m.call('glFinish')
    measure('clear', clear)
    cold = measure('cold_draw', draw_and_finish)
    assert m.call('glGetError') == 0
    first = m.pixels()
    clear()
    m.reset_profile()
    m.profile_enable(True)
    warm = measure('warm_draw', draw_and_finish)
    m.profile_enable(False)
    assert warm['jit_flushes'] == 0, 'Warm draw recompiled code'
    assert m.call('glGetError') == 0
    pixels = m.pixels()
    assert first == pixels, 'Cold/warm image mismatch'
    if profile_dir is not None:
        directory = profile_dir / str(bpp) / scene
        directory.mkdir(parents=True, exist_ok=True)
        functions = m.profile_rows()
        assert sum(row['instructions'] for row in functions) == warm['instructions']
        with (directory / (renderer + '.csv')).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=('function', 'instructions'), lineterminator='\n')
            writer.writeheader()
            writer.writerows(functions)
        if profile_jit:
            (directory / (renderer + '-jit.txt')).write_text(m.jit_profile_text())
    clear()
    m.reset_counts()
    draw_and_finish()
    assert warm == dict(warm, **m.counts()), 'Warm instruction count is not repeatable'
    assert pixels == m.pixels(), 'Repeated warm image mismatch'
    covered = sum(any(pixels[i:i+3]) for i in range(0, len(pixels), 4))
    assert covered > 0, 'Empty rendering'
    if scene in RECTANGLE_SCENES and width % 8 == 0 and height % 8 == 0:
        assert covered == (width*3//4)*(height*3//4), (scene, covered)
    if scene in ('alpha_cutout', 'palette8_alpha'):
        assert 0 < covered < (width * 3 // 4) * (height * 3 // 4), 'Alpha test did not reject fragments'
    if scene == 'stencil':
        for x, y, expected in ((width // 2, height // 2, b'\0\0\xff'),
                               (13 * width // 16, height // 2, b'\xff\0\0'),
                               (0, 0, b'\0\0\0')):
            offset = (y * width + x) * 4
            assert pixels[offset:offset + 3] == expected, 'Stencil operations produced the wrong mask'
    if scene == 'overdraw':
        m.call('glDepthFunc', 0x0201)  # equal-depth fragments now fail LESS
        draw_and_finish()  # Compile the rejection pipeline before measuring it.
        hidden = measure('hidden_draw', draw_and_finish)
        assert hidden['jit_flushes'] == 0
        assert m.call('glGetError') == 0
        assert pixels == m.pixels(), 'Hidden draw changed framebuffer'
    if window:
        measure('swap', lambda: m.call('eglSwapBuffers', m.dpy, m.surface))
        assert m.last_bitmap
        m.verify_presentation(pixels)
    for row in rows:
        row['covered_pixels'] = covered
        if not m.counter.setup_entry_count:
            row['pipeline_setup_instructions'] = None
    print(f'{renderer:12} {bpp:2} {scene:15} cold={cold["instructions"]:10,} '
          f'warm={warm["instructions"]:10,} jit={warm["jit_instructions"]:10,} pixels={covered}', flush=True)
    return rows, pixels


def compare_case(arguments):
    renderers, width, height, bpp, scene, window, cpu, counter, image_dir, profile_dir, profile_jit = arguments
    library = counter_library()
    rows, reference, reference_label = [], None, None
    for renderer, elf in renderers:
        result, pixels = run_case(renderer, library, width, height, bpp, scene, window,
                                  elf=elf, cpu=cpu, counter=counter,
                                  profile_dir=profile_dir, profile_jit=profile_jit)
        if reference is None:
            reference, reference_label = pixels, renderer
        differences = [abs(a - b) for index, (a, b) in enumerate(zip(reference, pixels)) if index % 4 != 3]
        identical = sum(reference[index:index + 3] == pixels[index:index + 3]
                        for index in range(0, len(pixels), 4))
        for row in result:
            row.update(reference=reference_label,
                       rgb_mean_difference=sum(differences) / len(differences),
                       rgb_max_difference=max(differences), identical_rgb_pixels=identical)
        rows.extend(result)
        if image_dir is not None:
            from PIL import Image
            directory = image_dir / str(bpp) / scene
            directory.mkdir(parents=True, exist_ok=True)
            image = Image.frombytes('RGBA', (width, height), pixels)
            image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(directory / (renderer + '.png'))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', action='append', metavar='LABEL=ELF',
                        help='Named ELF; gerbera, pixelflinger and both select their default builds')
    parser.add_argument('--scenes', nargs='+', choices=SCENES, default=list(SCENES))
    parser.add_argument('--bpp', type=int, nargs='+', default=[16,32], choices=[16,32])
    parser.add_argument('--width', type=int, default=128)
    parser.add_argument('--height', type=int, default=96)
    parser.add_argument('--cpu', choices=CPU_MODELS, default='arm1136')
    parser.add_argument('--counter', choices=('block', 'instruction'), default='block')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--window', action='store_true')
    parser.add_argument('--output', type=Path, default=HERE/'results.csv')
    parser.add_argument('--images', type=Path, help='Save the rendered frames in this directory')
    parser.add_argument('--profile', action='store_true', help='Save warm function profiles beside the output CSV')
    parser.add_argument('--profile-jit', action='store_true', help='Include hot generated-code block disassembly')
    parser.add_argument('--require-identical', action='store_true', help='Fail on cross-renderer RGB differences')
    args = parser.parse_args()
    args.profile |= args.profile_jit
    if args.profile and args.counter != 'block':
        parser.error('profiling requires --counter block')
    if not 1 <= args.jobs <= 8 or min(args.width, args.height) < 1:
        parser.error('jobs must be between 1 and 8, and dimensions must be positive')
    renderers = {}
    for item in args.renderer or ['both']:
        if item in ('gerbera', 'pixelflinger', 'both'):
            labels = ('gerbera', 'pixelflinger') if item == 'both' else (item,)
            items = [(label, HERE/'build'/label/(label+'.elf')) for label in labels]
        else:
            label, separator, path = item.partition('=')
            if not separator or not re.fullmatch(r'[A-Za-z0-9_-]+', label):
                parser.error('each renderer needs a filename-safe LABEL=ELF')
            items = [(label, Path(path))]
        for label, elf in items:
            if label in renderers or not elf.is_file():
                parser.error('renderer labels must be distinct and ELF files must exist')
            renderers[label] = elf
    if args.require_identical and len(renderers) < 2:
        parser.error('--require-identical needs at least two renderers')
    profile_dir = args.output.parent / (args.output.stem + '-profiles') if args.profile else None
    jobs = [(list(renderers.items()), args.width, args.height, bpp, scene, args.window,
             args.cpu, args.counter, args.images, profile_dir, args.profile_jit)
            for bpp in args.bpp for scene in args.scenes]
    started = time.monotonic()
    counter_library()
    rows = []
    if args.jobs == 1:
        for job in jobs:
            rows.extend(compare_case(job))
    else:
        with ProcessPoolExecutor(max_workers=args.jobs, mp_context=multiprocessing.get_context('spawn')) as pool:
            for future in as_completed([pool.submit(compare_case, job) for job in jobs]):
                rows.extend(future.result())
    phase_order = {'context_setup': 0, 'resource_setup': 1, 'clear': 2,
                   'cold_draw': 3, 'warm_draw': 4, 'hidden_draw': 5, 'swap': 6}
    rows.sort(key=lambda row: (row['bpp'], args.scenes.index(row['scene']),
                              list(renderers).index(row['renderer']), phase_order[row['phase']]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    print(f'Saved {args.output}; {len(jobs)} scene/format cases in {time.monotonic() - started:.2f}s')
    differences = {(row['bpp'], row['scene'], row['renderer']) for row in rows
                   if row['rgb_max_difference']}
    if differences:
        print(f'RGB differences in {len(differences)} renderer/scene/format comparisons')
        if args.require_identical:
            raise SystemExit('Cross-renderer RGB mismatch; see the per-scene CSV measurements')
    elif len(renderers) > 1:
        print('All compared RGB images match exactly')


if __name__ == '__main__':
    main()
