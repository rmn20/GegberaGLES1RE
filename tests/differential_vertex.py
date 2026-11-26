"""Whole-buffer vertex fetch, transform, fog and lighting oracle fixtures."""
import ctypes
import struct

from differential_numeric import host_buffer, independent_vector

VERTEX_OFFSET = 1332
VERTEX_SIZE = 128
STACK_SIZE = VERTEX_OFFSET + 256 * VERTEX_SIZE
INVALID_INDEX = 255
X_ONE = 1 << 21
DATATYPE_BYTE, DATATYPE_SHORT, DATATYPE_FLOAT, DATATYPE_FIXED = 0, 2, 6, 7

# Stage order matches DifferentialVertexStage in the host adapter.
STAGES = (
    ("grFetchVertexNormalINT8", 0x1A0C4, 0x1A144, 0x42CC00, 0x42CD21),
    ("grFetchVertexNormalINT16", 0x1A144, 0x1A1AC, 0x42CD30, 0x42CEBD),
    ("grFetchVertexNormalFIXED", 0x1A1AC, 0x1A234, 0x42CEC0, 0x42D047),
    ("grFetchVertexNormalFLOAT", 0x1A234, 0x1A300, 0x42D050, 0x42D1D7),
    ("grFetchVertexNormalDEFAULT", 0x1A300, 0x1A344, 0x42D1E0, 0x42D2ED),
    ("grFetchVertexColorUINT8", 0x19E18, 0x19E98, 0x42C5F0, 0x42C74E),
    ("grFetchVertexColorFLOAT", 0x19E98, 0x19FB8, 0x42C750, 0x42C927),
    ("grFetchVertexColorFIXED", 0x19FB8, 0x1A070, 0x42C930, 0x42CAD0),
    ("grFetchVertexColorDEFAULT", 0x1A070, 0x1A0C4, 0x42CAD0, 0x42CBF6),
    ("grFetchVertexColorFastUINT8", 0x19B1C, 0x19B98, 0x42BE60, 0x42BFE0),
    ("grFetchVertexColorFastFLOAT", 0x19B98, 0x19CFC, 0x42BFE0, 0x42C254),
    ("grFetchVertexColorFastFIXED", 0x19CFC, 0x19DA8, 0x42C260, 0x42C47C),
    ("grFetchVertexColorFastDEFAULT", 0x19DA8, 0x19E18, 0x42C480, 0x42C5EC),
    ("grApplyFogFAST", 0x1A704, 0x1A738, 0x42DD00, 0x42DDD1),
    ("grApplyFogLINEAR", 0x1A798, 0x1A810, 0x42DEC0, 0x42DFE7),
    ("grApplyFogEXP", 0x1A810, 0x1A8AC, 0x42DFF0, 0x42E119),
    ("grApplyFogEXP2", 0x1A8AC, 0x1A94C, 0x42E120, 0x42E259),
    ("grFetchPointSizeFLOAT", 0x1A94C, 0x1A9D0, 0x42E260, 0x42E394),
    ("grFetchPointSizeFIXED", 0x1A9D0, 0x1AA2C, 0x42E3A0, 0x42E4BF),
    ("grTransformVertex", 0, 0, 0x428FB0, 0x42981D),
    ("grViewportTransformVertex", 0x17960, 0x17A44, 0x429820, 0x429872),
    ("grVertexCacheGet", 0x1CA98, 0x1D16C, 0x432720, 0x432A50),
)


def words(*values):
    return struct.pack('<' + 'I' * len(values), *(v & 0xFFFFFFFF for v in values))


def float_word(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def put(data, offset, *values):
    data[offset:offset + 4 * len(values)] = words(*values)


def targets(suite, entries):
    result = {}
    for name, arm_start, arm_end, x86_start, x86_end in entries:
        start, end = (arm_start, arm_end) if suite.arch == 'arm' else (x86_start, x86_end)
        if start:
            suite.register(name, start, end)
            result[name] = start
    return result


def transform(suite, matrix):
    raw = host_buffer(struct.pack('<16f', *matrix))
    encoded = host_buffer(size=80)
    native = host_buffer(size=200)
    suite.lib.IMatrix4x4_setfv(encoded, raw)
    suite.lib.IVertexTransform_copy(native, encoded)
    b = bytes(native)
    original = b[80:112] + b[112:176] + b[:80] + b[176:192] + b[196:200] + b[192:196]
    return native, original, encoded


MATRICES = (
    (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1),
    (-2, 0, 0, 0, 0, .5, 0, 0, 0, 0, 1.5, 0, .5, -.25, 2, 1),
    (1, .25, .125, .125, -.5, 1, -.25, -.125, .25, -.125, 1, .25, .5, -.25, .125, 1),
    (1.25, 0, 0, 0, 0, 1.5, 0, 0, 0, 0, -1.25, -1, 0, 0, -2.25, 0),
)


def state_bytes(suite, control, array, stack, original_transform):
    state = bytearray(8000)
    shift = 0 if suite.arch == 'arm' else 8
    put(state, 116 + shift, stack, stack)
    for i in range(8):
        offset = 308 + shift + 20 * i
        put(state, offset, control[0])
        state[offset + 4] = control[1]
        state[offset + 5] = control[2]
        put(state, offset + 8, array)
    for offset in (1664, 1864, 2064):
        state[offset + shift:offset + shift + 200] = original_transform
    put(state, 5632 + shift, *control[3:6])
    put(state, 5612 + shift, *control[6:9])
    struct.pack_into('<4H', state, 5624 + shift, *control[9:13])
    put(state, 536 + shift, control[14], control[13], control[15])
    put(state, 284 + shift, 320 << 8, 240 << 8, 0x1FFFFFFF)
    put(state, 296 + shift, 320 << 8, -(240 << 8), 0x1FFFFFFF)
    return state


def vertex_stack(suite, case, fog=False):
    pool = bytearray(b'\xa5' * STACK_SIZE)
    positions = ((.5, -.25, -2, 1), (-1, 1, -4, 1), (2, 0, .25, 1), (0, 0, 0, 1))
    for i, index in enumerate((3, 0, 2, 1)):
        offset = VERTEX_OFFSET + VERTEX_SIZE * i
        put(pool, offset, index)
        position_state = i if fog else 0
        struct.pack_into('<HH', pool, offset + 4, position_state << 14, 0)
        p = positions[(i + case) % 4]
        if position_state == 1:
            pool[offset + 72:offset + 88] = words(*(int(v * 65536) for v in p))
        else:
            pool[offset + 72:offset + 92] = independent_vector(p)
        if position_state == 3:
            put(pool, offset + 88, float_word(p[2]))
        # Shared-exponent clip position, including w=0 viewport fallback.
        put(pool, offset + 8, 127, int(p[0] * (1 << 27)), int(p[1] * (1 << 27)),
            int(p[2] * (1 << 26)), 0 if i == 3 else 1 << 29)
        if i == 3:
            put(pool, offset + 8, 0, 0, 0, 0, 0)
        put(pool, offset + 92, (i - 1) * (1 << 20), 0, (1 if i & 1 else -1) * X_ONE)
        put(pool, offset + 40, X_ONE // 4, X_ONE // 2, X_ONE)
        struct.pack_into('<H', pool, offset + 52, 0x89AB)
    return pool


def array_bytes(suite, kind, case, size=4, count=4):
    rows = []
    edges = (-1.0, 0.0, .5, 1.0, 1.5, -.25, 2.0, .125)
    for i in range(count):
        values = [edges[(case + i + j) % len(edges)] for j in range(4)]
        if kind == 'byte':
            row = struct.pack('<4b', *[int(v * 32) for v in values])
        elif kind == 'short':
            row = struct.pack('<4h', *[int(v * 8192) for v in values])
        elif kind == 'ubyte':
            row = bytes([0, 1, 127, 255][(case + i + j) % 4] for j in range(4))
        elif kind == 'float':
            row = struct.pack('<4f', *values)
        else:
            row = words(*(int(v * 65536) for v in values))
        rows.append(row + b'\x5a' * (20 - len(row)))
    return b''.join(rows)


def check_stages(suite):
    addresses = targets(suite, STAGES)
    adapter = suite.lib.differential_vertex_stage
    adapter.argtypes = [ctypes.c_uint] + [ctypes.c_void_p] * 5
    adapter.restype = None
    kinds = ('byte', 'short', 'fixed', 'float', 'fixed', 'ubyte', 'float', 'fixed',
             'fixed', 'ubyte', 'float', 'fixed', 'fixed', 'fixed', 'fixed', 'fixed',
             'fixed', 'float', 'fixed')
    for operation, entry in enumerate(STAGES):
        name = entry[0]
        if name not in addresses:
            continue
        for case in range(12):
            native_transform, original_transform, _ = transform(suite, MATRICES[case % len(MATRICES)])
            kind = kinds[operation] if operation < len(kinds) else ('byte', 'short', 'fixed', 'float')[case % 4]
            datatype = {'byte': DATATYPE_BYTE, 'short': DATATYPE_SHORT,
                        'float': DATATYPE_FLOAT}.get(kind, DATATYPE_FIXED)
            control = [20, 2 + case % 3, datatype, -X_ONE, X_ONE // 2, X_ONE,
                       -X_ONE // 4, X_ONE // 2, X_ONE * 2, 0, 1, 0xFFFF, 0xABCD,
                       float_word((0, .125, 1, 4)[case % 4]), float_word(3), float_word(.5)]
            data = array_bytes(suite, kind, case, count=40 if operation == 21 else 4)
            pool = vertex_stack(suite, case, fog=13 <= operation <= 16)
            sequence = bytes((2, 0, 3, 1, INVALID_INDEX))
            if operation == 21:
                # Cache misses, hits, hash collisions and eviction of locked slots.
                pool[:168] = b'\0' * 168
                put(pool, 8, 64, 64)
                put(pool, VERTEX_OFFSET, -1)
                sequence = bytes((0, 1, 2, 3, 32, 33, 34, 35, 0, 32, 3, 1, INVALID_INDEX))
            skew = 0
            if operation == 9:  # unsigned byte color does not require word alignment
                control[0] = (20, 4, 5)[case % 3]
                skew = case % 4
                data = b"\x5a" * skew + b"".join(data[i:i + control[0]] for i in range(0, len(data), 20))
            native_pool = host_buffer(bytes(pool))
            native_array = host_buffer(data)
            adapter(operation, host_buffer(words(*control)), ctypes.byref(native_array, skew), native_pool,
                    host_buffer(sequence), native_transform)
            oracle = suite.oracle
            oracle.reset()
            original_pool = oracle.alloc(len(pool), bytes(pool))
            original_array = oracle.alloc(len(data), data)
            state = state_bytes(suite, control, original_array + skew, original_pool, original_transform)
            state_pointer = oracle.alloc(len(state), bytes(state))
            oracle.watch(original_pool, len(pool), 'complete vertex buffer and cache')
            oracle.watch(original_array, len(data), 'input array')
            if operation < 19:
                oracle.call(addresses[name], [state_pointer, oracle.alloc(len(sequence), sequence)])
            else:
                for index in sequence[:-1]:
                    argument = index if operation == 21 else original_pool + VERTEX_OFFSET + index * VERTEX_SIZE
                    oracle.call(addresses[name], [state_pointer, argument])
            actual = oracle.readback(original_pool, len(pool))
            unchanged = oracle.readback(original_array, len(data))
            suite.compare_bytes(name, {'case': case, 'control': control, 'sequence': sequence.hex(),
                                      'matrix': MATRICES[case % len(MATRICES)], 'array': data.hex()},
                                bytes(native_pool)[:len(pool)] + data, actual + unchanged)


def check_texture_transforms(suite):
    entries = (
        ('ITCTransform_copy', 0x2E098, 0x2E1C4, 0x462640, 0x462831),
        ('ITCTransform_mulxv', 0x2E270, 0x2E4F0, 0x462D60, 0x462E68),
        ('ITCTransform_mulfv', 0x2E4F0, 0x2E848, 0x463220, 0x463341),
        ('grTexCoordDivByQ', 0x2E1C4, 0x2E270, 0x462840, 0x462945),
    )
    addresses = targets(suite, entries)
    suite.lib.ITCTransform_copy.argtypes = [ctypes.c_void_p] * 2
    suite.lib.ITCTransform_copy.restype = None
    for name in ('ITCTransform_mulxv', 'ITCTransform_mulfv'):
        getattr(suite.lib, name).argtypes = [ctypes.c_void_p] * 3 + [ctypes.c_int]
        getattr(suite.lib, name).restype = None
    for matrix in MATRICES:
        _, _, encoded = transform(suite, matrix)
        native_transform = host_buffer(size=132)
        suite.lib.ITCTransform_copy(native_transform, encoded)
        b = bytes(native_transform)[:132]
        canonical = b[128:132] + b[:16] + b[80:112] + b[16:80] + b[112:128]
        oracle = suite.oracle
        oracle.reset()
        output = oracle.alloc(132)
        oracle.watch(output, 132, 'texture transform')
        oracle.call(addresses['ITCTransform_copy'], [output, oracle.alloc(80, bytes(encoded))])
        suite.compare_bytes('ITCTransform_copy', matrix, canonical, oracle.readback(output, 132))
        for count in (2, 3, 4):
            for position in ((.5, -.25, 2, 1), (-2, .75, -.5, 0), (0, 0, 0, -1)):
                for name in ('ITCTransform_mulxv', 'ITCTransform_mulfv'):
                    data = (struct.pack('<4f', *position) if name.endswith('mulfv') else
                            words(*(int(v * 65536) for v in position)))
                    result = host_buffer(b'\xa5' * 16)
                    getattr(suite.lib, name)(result, native_transform, host_buffer(data), count)
                    oracle.reset()
                    out = oracle.alloc(16, b'\xa5' * 16)
                    oracle.watch(out, 16, 'texture coordinate and canary')
                    oracle.call(addresses[name], [out, oracle.alloc(132, canonical),
                                                 oracle.alloc(len(data), data), count])
                    suite.compare_bytes(name, {'matrix': matrix, 'position': position, 'size': count},
                                        bytes(result), oracle.readback(out, 16))




def check_texture_arrays(suite):
    entries = (
        ('grTransformTexCoordINT8x2', 0x1A344, 0x1A3D4, 0x42D2F0, 0x42D45D),
        ('grTransformTexCoordINT8x4', 0x1A3E4, 0x1A494, 0x42D460, 0x42D5EC),
        ('grTransformTexCoordINT16x2', 0x1A494, 0x1A524, 0x42D5F0, 0x42D74D),
        ('grTransformTexCoordINT16x4', 0x1A524, 0x1A5D4, 0x42D750, 0x42D8DC),
        ('grTransformTexCoordFIXEDx4', 0x1A5D4, 0x1A644, 0x42D8E0, 0x42DA28),
        ('grTransformTexCoordDEFAULT', 0x1A644, 0x1A694, 0x42DA30, 0x42DB37),
        ('grTransformTexCoordFLOATx4', 0x1A694, 0x1A704, 0x42DB40, 0x42DCFE),
    )
    addresses = targets(suite, entries)
    adapter = suite.lib.differential_vertex_texture
    adapter.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_int] + [ctypes.c_void_p] * 4
    adapter.restype = None
    for operation, entry in enumerate(entries):
        name = entry[0]
        for case in range(12):
            matrix = MATRICES[case % len(MATRICES)]
            _, original_transform, encoded = transform(suite, matrix)
            native_texture = host_buffer(size=132)
            suite.lib.ITCTransform_copy(native_texture, encoded)
            b = bytes(native_texture)[:132]
            original_texture = b[128:132] + b[:16] + b[80:112] + b[16:80] + b[112:128]
            unit = case % 2
            size = 2 if operation in (0, 2) else 2 + case % 3
            if operation in (1, 3):
                size = 3 + case % 2
            data = bytearray()
            for i in range(4):
                values = [(-2, 0, 1, 2)[(case + i + j) % 4] for j in range(4)]
                if operation < 2:
                    row = struct.pack('<4b', *values)
                elif operation < 4:
                    row = struct.pack('<4h', *values)
                elif operation == 6:
                    row = struct.pack('<4f', *values)
                else:
                    row = words(*(v * 65536 for v in values))
                data.extend(row + b'\x5a' * (20 - len(row)))
            pool = vertex_stack(suite, case)
            sequence = bytes((2, 0, 3, 1, INVALID_INDEX))
            native_pool = host_buffer(bytes(pool))
            adapter(operation, unit, size, 20, host_buffer(bytes(data)), native_pool,
                    host_buffer(sequence), native_texture)
            oracle = suite.oracle
            oracle.reset()
            original_pool = oracle.alloc(len(pool), bytes(pool))
            array = oracle.alloc(len(data), bytes(data))
            state = state_bytes(suite, [20, size, DATATYPE_FIXED] + [0] * 13, array,
                                original_pool, original_transform)
            shift = 0 if suite.arch == 'arm' else 8
            offset = 4064 + shift + unit * 132
            state[offset:offset + 132] = original_texture
            put(state, 5676 + shift, -0x8000, 0x10000, 0x20000, -0x4000)
            oracle.watch(original_pool, len(pool), 'complete vertex buffer')
            oracle.watch(array, len(data), 'input array')
            oracle.call(addresses[name], [oracle.alloc(len(state), bytes(state)),
                                         oracle.alloc(len(sequence), sequence), unit])
            actual = oracle.readback(original_pool, len(pool)) + oracle.readback(array, len(data))
            suite.compare_bytes(name, {'case': case, 'unit': unit, 'size': size,
                                      'matrix': matrix, 'array': data.hex()},
                                bytes(native_pool)[:len(pool)] + bytes(data), actual)


# Complete 208-byte light records use a different field order in the host.
LIGHT_LAYOUT = ((0, 192, 4), (4, 0, 16), (20, 180, 12), (32, 168, 12),
                (44, 24, 20), (64, 20, 4), (68, 72, 36), (104, 132, 36),
                (140, 60, 12), (152, 44, 16), (168, 108, 4), (172, 112, 16),
                (188, 128, 4), (192, 16, 4), (196, 196, 12))


def original_lights(host):
    result = bytearray(len(host))
    for base in range(0, len(host), 208):
        for native, original, size in LIGHT_LAYOUT:
            result[base + original:base + original + size] = host[base + native:base + native + size]
    return bytes(result)


def light_records(suite, case, directional, positional):
    result = bytearray(208 * (directional + positional))
    for i in range(directional + positional):
        record = bytearray(208)
        is_positional = i >= directional
        put(record, 0, int(is_positional))
        put(record, 4, 129, (i - 1) * (1 << 27), 0, 1 << 29)
        direction = (0, 0, X_ONE if i % 2 == 0 else -X_ONE)
        put(record, 20, *direction)
        halfway = host_buffer(words(direction[0], direction[1], direction[2] + X_ONE))
        suite.lib.XNormal3_normalize(halfway, halfway)
        record[32:44] = bytes(halfway)[:12]
        mask = (0, 2, 4, 8, 14)[case % 5]
        if not is_positional:
            mask |= 64
        elif case % 4 == 1:
            mask |= 16
        elif case % 4 == 2:
            mask |= 32
        elif case % 4 == 3:
            mask |= 48
        put(record, 64, mask)
        for color in range(3):
            put(record, 68 + color * 12, X_ONE // (4 + color), X_ONE // (5 + color), X_ONE // (6 + color))
            put(record, 104 + color * 12, X_ONE // (7 + color), X_ONE // (8 + color), X_ONE // (9 + color))
        put(record, 140, 0, 0, X_ONE if case & 1 else -X_ONE)
        put(record, 168, (0, X_ONE, 4 * X_ONE)[case % 3])
        record[172:188] = independent_vector((1, .125, .0625))
        put(record, 188, X_ONE // 2, float_word(45 if mask & 16 else 180), 0x12345678, 0x23456789, 0x3456789A)
        result[i * 208:(i + 1) * 208] = record
    return bytes(result)


def check_lighting(suite):
    addresses = targets(suite, (
        ('grLightVertex', 0x17B2C, 0x199EC, 0x42AC80, 0x42BBBD),
        ('grApplyLighting', 0x199EC, 0x19B1C, 0x42BBC0, 0x42BE53),
    ))
    adapter = suite.lib.differential_vertex_lighting
    adapter.argtypes = [ctypes.c_void_p] * 6
    adapter.restype = None
    suite.lib.XNormal3_normalize.argtypes = [ctypes.c_void_p] * 2
    suite.lib.XNormal3_normalize.restype = None
    for case in range(80):
        directional = (0, 1, 2, 1)[case % 4]
        positional = (0, 1, 0, 2)[(case // 4) % 4]
        apply = (case // 8) & 1
        normalize = (case // 2) & 1
        control = [1 if case & 1 else 3, normalize, (case // 4) & 1,
                   (0, X_ONE, 8 * X_ONE)[case % 3], directional, positional, apply]
        name = 'grApplyLighting' if apply else 'grLightVertex'
        native_transform, original_transform, _ = transform(suite, MATRICES[case % 3])
        normal_transform = words(1 << 29, 0, 0, 0, 1 << 29, 0, 0, 0, 1 << 29, 3)
        if case % 5 == 0:
            normal_transform = words(-(1 << 28), 1 << 27, 0, 0, 1 << 29, 0,
                                     0, 0, 1 << 28, 3)
        pool = vertex_stack(suite, case, fog=bool(apply and positional))
        for i in range(4):
            offset = VERTEX_OFFSET + i * VERTEX_SIZE
            axis = [(0, 0, X_ONE), (0, 0, -X_ONE), (X_ONE, 0, 0), (-X_ONE, 0, 0)][i]
            put(pool, offset + 92, *(v * (2 if normalize else 1) for v in axis))
            if positional and (not apply or i == 3):
                struct.pack_into('<H', pool, offset + 4, 3 << 14)
                put(pool, offset + 72, 127, (i - 1) * (1 << 27), 0, -(1 << 29), float_word(-1))
        lights = light_records(suite, case, directional, positional)
        native_lights = host_buffer(lights, size=max(208, len(lights)))
        native_pool = host_buffer(bytes(pool))
        sequence = bytes((2, 0, 3, 1, INVALID_INDEX))
        adapter(host_buffer(words(*control)), native_pool, host_buffer(sequence),
                native_transform, host_buffer(normal_transform), native_lights)
        oracle = suite.oracle
        oracle.reset()
        original_pool = oracle.alloc(len(pool), bytes(pool))
        original_light_data = original_lights(lights)
        light_pointer = oracle.alloc(max(208, len(lights)), original_light_data)
        state = state_bytes(suite, [20, 4, DATATYPE_FLOAT] + [0] * 13, 0,
                            original_pool, original_transform)
        shift = 0 if suite.arch == 'arm' else 8
        state[24 + shift] = control[0]
        state[90 + shift] = control[1]
        state[93 + shift] = control[2]
        state[47 + shift] = bool(positional)
        put(state, 5568 + shift, control[3])
        struct.pack_into('<H', state, 5572 + shift, 0xABCD)
        put(state, 5556 + shift, 0x10000, 0x20000, 0x30000)
        put(state, 5588 + shift, 0x40000, 0x80000, 0xC0000)
        put(state, 5600 + shift, 0x40000, 0x80000, 0xC0000)
        state[4328 + shift:4368 + shift] = normal_transform
        for i in range(directional):
            put(state, 5448 + shift + 4 * i, light_pointer + i * 208)
        for i in range(positional):
            put(state, 5484 + shift + 4 * i, light_pointer + (directional + i) * 208)
        state_pointer = oracle.alloc(len(state), bytes(state))
        oracle.watch(original_pool, len(pool), 'complete vertex buffer')
        if lights:
            oracle.watch(light_pointer, len(lights), 'complete light records')
        if apply:
            oracle.call(addresses[name], [state_pointer, oracle.alloc(len(sequence), sequence)])
        else:
            for index in sequence[:-1]:
                oracle.call(addresses[name], [state_pointer, original_pool + VERTEX_OFFSET + index * VERTEX_SIZE])
        actual = oracle.readback(original_pool, len(pool)) + oracle.readback(light_pointer, len(lights))
        expected = bytes(native_pool)[:len(pool)] + original_lights(bytes(native_lights)[:len(lights)])
        suite.compare_bytes(name, {'case': case, 'control': control, 'normal_transform': normal_transform.hex(),
                                  'lights': lights.hex(), 'matrix': MATRICES[case % 3]}, expected, actual)

def run(suite):
    suite.lib.IMatrix4x4_setfv.argtypes = [ctypes.c_void_p] * 2
    suite.lib.IMatrix4x4_setfv.restype = None
    suite.lib.IVertexTransform_copy.argtypes = [ctypes.c_void_p] * 2
    suite.lib.IVertexTransform_copy.restype = None
    check_stages(suite)
    check_texture_transforms(suite)
    check_texture_arrays(suite)
    check_lighting(suite)
