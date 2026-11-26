"""Deterministic raster comparisons against the original 32-bit routines."""

import ctypes as C
import struct
from unicorn import arm_const as ARM


TARGETS = {
    "grComputeGradientReference": (0x423340, 0x423372),
    "grSetupGradient": (0x4249C0, 0x424BC5),
    "grComputeDeltas": (0x424BD0, 0x424D92),
    "grScanConvertTriangle": (0x422900, 0x423333),
    "grSetupTextureUnit": (0x423380, 0x423B96),
    "grPackColor": (0x439280, 0x43943B),
    "grUnpackColor": (0x439750, 0x439A69),
    "grrDownsample": (0x442500, 0x4429B9),
    "grrGetTextureFormatDescriptor": (0x43EAD0, 0x43ECB6),
    "grDrawClippedLine": (0x425A70, 0x42607B),
    "grrDrawClippedTriangle": (0x427D70, 0x427DAC),
    "grTriangleTessellator": (0x427870, 0x427D30),
    "grDrawClippedTriangle": (0x425620, 0x425A6E),
    "grSetupTriangleGradients": (0x424DA0, 0x42561B),
    "grInterpolateVertexGradients": (0x423BA0, 0x4247AF),
    "grComputeTriangleArea": (0x424890, 0x42496F),
    "grRoundTriangleArea": (0x424970, 0x4249B8),
    "grEvaluatePixelPipeline": (0x43BFC0, 0x43D1BF),
    "grCompressSignature": (0x43D260, 0x43D8A7),
    "grExecuteProgram": (0x43A8F0, 0x43BFBB),
}

ARM_TARGETS = {
    "grrGetTextureFormatDescriptor": (0x1FDF4, 0x1FEA8),
    "grEvaluatePixelPipeline": (0x1FEA8, 0x2076C),
    "grCompressSignature": (0x20774, 0x2086C),
    "grExecuteProgram": (0x1F87C, 0x1FDEC),
    "compileInternal": (0x2C298, 0x2C6B8),
    "HCArena_init": (0x2CE48, 0x2CE60),
    "HCARMBackend_create": (0x25484, 0x2548C),
}

LINE_WIDTH = 64
LINE_PIXELS = LINE_WIDTH * LINE_WIDTH
LINE_OUTPUT_WORDS = 3 + 2 * LINE_PIXELS
STATE_CAPABILITIES = 0x3D
VERTEX_BUFFER_OFFSET = 1332
VERTEX_BYTES = 128


def words(values):
    return struct.pack("<" + "I" * len(values), *(v & 0xFFFFFFFF for v in values))


def unpack(data):
    return struct.unpack("<" + "I" * (len(data) // 4), data)


def buffer(data):
    result = (C.c_uint32 * max(1, (len(data) + 3) // 4))()
    C.memmove(result, data, len(data))
    return result


def native_adapter(lib, name, inputs, output_words):
    result = (C.c_uint32 * output_words)()
    getattr(lib, name)(buffer(words(inputs)), result)
    return tuple(result)


def run(suite):
    run_gradient_boundary_cases(suite)
    run_depth_offset_cases(suite)
    if suite.arch == "arm":
        return run_arm(suite)
    lib, oracle, rng = suite.lib, suite.oracle, suite.rng
    for name, (start, end) in TARGETS.items():
        suite.register(name, start, end)
    for name in ("gradient", "scan", "texture", "line"):
        function = getattr(lib, "differential_raster_" + name)
        function.argtypes = [C.c_void_p, C.c_void_p]
        function.restype = None
    lib.grComputeGradientReference.argtypes = [C.c_int32] * 4
    lib.grComputeGradientReference.restype = C.c_uint32
    lib.grrGetTextureFormatDescriptor.argtypes = [C.c_void_p, C.c_int]
    lib.grrGetTextureFormatDescriptor.restype = None
    for name in ("grPackColor", "grUnpackColor"):
        getattr(lib, name).argtypes = [C.c_uint32, C.c_void_p]
        getattr(lib, name).restype = C.c_uint32
    lib.grrDownsample.argtypes = [C.c_void_p] + [C.c_int] * 4 + [C.c_void_p] * 2
    lib.grrDownsample.restype = None

    def call(name, args):
        return oracle.call(TARGETS[name][0], args)

    for i in range(suite.samples):
        values = [rng.randint(-65536, 65536) for _ in range(4)]
        oracle.reset()
        expected = call("grComputeGradientReference", values)
        actual = lib.grComputeGradientReference(*values)
        suite.compare("grComputeGradientReference", values, actual, expected)

        # Include zero derivatives and both sides of the 18-bit shift split.
        context = [rng.randint(-65536, 65536) for _ in range(2)]
        context += [rng.randint(-1048576, 1048576) for _ in range(4)]
        context += [i % 32]
        gradient = i % 10
        g01 = 0 if i % 5 == 0 else rng.randint(-1048576, 1048576)
        g02 = 0 if i % 7 == 0 else rng.randint(-1048576, 1048576)
        reference = rng.randint(-2147483648, 2147483647)
        for mode, name in enumerate(("grSetupGradient", "grComputeDeltas")):
            inputs = [mode, *context, gradient, g01, g02, reference]
            oracle.reset()
            pipe = oracle.alloc(1024, b"\x5A" * 1024)
            info = oracle.alloc(32, words([pipe, *context]))
            args = [info, gradient, g01, g02]
            if mode == 0:
                args.append(reference)
            call(name, args)
            expected = unpack(oracle.read(pipe + 192, 120))
            actual = native_adapter(lib, "differential_raster_gradient", inputs, 30)
            suite.compare(name, inputs, actual, expected)

    fixed_triangles = [
        [(0, -32), (128, -32), (64, 32)],  # clipped flat top
        [(0, 0), (128, 128), (0, 128)],  # flat bottom
        [(160, 192), (480, 320), (288, 448)],
        [(-128, 0), (0, 128), (-128, 256)],  # outside X
        [(0, -128), (128, -96), (0, -64)],  # outside Y
        [(0, 0), (16, 16), (32, 32)],  # zero-area spans
    ]
    for i in range(suite.samples):
        if i < len(fixed_triangles):
            points = fixed_triangles[i]
        else:
            points = sorted([(rng.randint(-128, 640), rng.randint(-128, 640))
                             for _ in range(3)], key=lambda point: point[1])
            if points[0][1] == points[2][1]:
                points[2] = (points[2][0], points[2][1] + 16)
        scissor = [0, 32, 0, 32] if i % 3 else [4, 24, 5, 25]
        coordinates = [v * 256 for point in points for v in point]
        inputs = [*scissor, *coordinates, 8]
        oracle.reset()
        state = oracle.alloc(0x1800)
        pipe = oracle.alloc(1024)
        stack = oracle.alloc(23860)
        p = oracle.alloc(24, words(coordinates))
        oracle.write(state + 0x7C, words([stack]))
        oracle.write(state + 0x88, words([pipe]))
        oracle.write(state + 0x104, words(scissor))
        count = call("grScanConvertTriangle", [state, p, p + 8, p + 16, 8])
        num_scanlines, spans, y_start = unpack(oracle.read(pipe + 180, 12))
        expected = (count, y_start, num_scanlines)
        if count:
            expected += unpack(oracle.read(spans, 4 * count))
        actual = native_adapter(lib, "differential_raster_scan", inputs, 2051)
        suite.compare("grScanConvertTriangle", inputs, actual[:3 + actual[0]], expected)

    descriptors = []
    for fmt in range(18):
        oracle.reset()
        desc = oracle.alloc(16)
        call("grrGetTextureFormatDescriptor", [desc, fmt])
        expected = oracle.read(desc, 16)
        native_desc = buffer(bytes(16))
        lib.grrGetTextureFormatDescriptor(native_desc, fmt)
        suite.compare("grrGetTextureFormatDescriptor", fmt, bytes(native_desc), expected)
        descriptors.append(expected)
    for i in range(suite.samples):
        descriptor = descriptors[i % len(descriptors)]
        color = [0, 0xFFFFFFFF, 0x01020304, 0x80FF007F][i] if i < 4 else rng.getrandbits(32)
        for name in ("grPackColor", "grUnpackColor"):
            oracle.reset()
            desc = oracle.alloc(16, descriptor)
            expected = call(name, [color, desc])
            actual = getattr(lib, name)(color, buffer(descriptor))
            suite.compare(name, (i % len(descriptors), color), actual, expected)

    # Nonstandard channel widths cover unpack's one-, two-, three-, and
    # greater-than-eight-bit replication paths without a format dependency.
    for sizes in ((1, 2, 3, 0), (9, 10, 0, 13), (8, 8, 8, 8), (0, 0, 0, 0)):
        offsets, position = [], 0
        for size in sizes:
            offsets.append(position if size else 0)
            position += size
        descriptor = bytes([*offsets, *sizes, 15, 4, 0, 0, 0, 0, 0, 0])
        for color in (0, 0xFFFFFFFF, 0x76543210, 0x89ABCDEF):
            oracle.reset()
            desc = oracle.alloc(16, descriptor)
            expected = call("grUnpackColor", [color, desc])
            actual = lib.grUnpackColor(color, buffer(descriptor))
            suite.compare("grUnpackColor", (sizes, color), actual, expected)

    formats = (0, 1, 2, 3, 4, 15, 16, 17)
    dimensions = ((1, 2), (2, 1), (2, 2), (1, 8), (8, 1), (4, 8), (16, 4))
    for i in range(suite.samples):
        fmt = formats[i % len(formats)]
        descriptor = descriptors[fmt]
        width, height = dimensions[(i // len(formats)) % len(dimensions)]
        dst_width, dst_height = max(1, width // 2), max(1, height // 2)
        size = width * height * descriptor[9]
        src = bytes(rng.getrandbits(8) for _ in range(size))
        dst_size = dst_width * dst_height * descriptor[9]
        oracle.reset()
        desc = oracle.alloc(16, descriptor)
        src_ptr = oracle.alloc(size, src)
        dst_ptr = oracle.alloc(dst_size + 16, b"\xA5" * (dst_size + 16))
        call("grrDownsample", [desc, width, height, dst_width, dst_height, src_ptr, dst_ptr])
        expected = oracle.read(dst_ptr, dst_size + 16)
        dst = buffer(b"\xA5" * (dst_size + 16))
        lib.grrDownsample(buffer(descriptor), width, height, dst_width, dst_height, buffer(src), dst)
        suite.compare("grrDownsample", (fmt, width, height, src.hex()),
                      bytes(dst)[:dst_size + 16], expected)

    run_texture_unit_cases(suite)

    # Stop at the pixel-pipeline boundary. Compare the rasterizer's exact
    # coverage and interpolated red input, independently of pixel shading.
    captured = None

    def capture_line(original):
        nonlocal captured
        (state,) = original.arguments(1)
        (pipe,) = unpack(original.read(state + 0x88, 4))
        count, spans, y_start = unpack(original.read(pipe + 180, 12))
        reference, = unpack(original.read(pipe + 200, 4))
        x_adder, = unpack(original.read(pipe + 240, 4))
        y_adder, = unpack(original.read(pipe + 280, 4))
        captured[0] += 1
        for row, span in enumerate(unpack(original.read(spans, 4 * count))):
            start = span & 0xFFFF
            y = y_start + row
            for x in range(start, start + (span >> 16)):
                if x >= LINE_WIDTH or y >= LINE_WIDTH:
                    raise ValueError("line rasterizer emitted a pixel outside its scissor")
                index = LINE_WIDTH * y + x
                captured[1 + index] += 1
                captured[1 + LINE_PIXELS + index] = (reference + x_adder * x + y_adder * row) & 0xFFFFFFFF
        original.return_from_call()

    def line_result(values):
        return (values[0], *values[-2:], tuple((i, values[1 + i], values[1 + LINE_PIXELS + i])
                                             for i in range(LINE_PIXELS) if values[1 + i]))

    hook = oracle.add_hook(0x421D60, capture_line)
    try:
        for i in range(min(suite.samples, 64)):
            delta = ((20, 0), (0, 20), (20, 10), (10, 20), (-20, 10),
                     (-10, -20), (20, -10), (0, 0), (-20, 0), (0, -20),
                     (10, -20), (-20, -10), (-10, 20), (20, 20),
                     (-20, -20), (1, 1))[i % 16]
            inputs = [24 * 4096, 24 * 4096, (24 + delta[0]) * 4096,
                      (24 + delta[1]) * 4096, (1 + i % 4) * 65536,
                      (i // 16) % 2, (i // 8) % 2, (i // 4) % 2,
                      0x20000000, 0x30000000]
            oracle.reset()
            state = oracle.alloc(0x2000)
            pipe = oracle.alloc(1024)
            stack = oracle.alloc(23860)
            dummy = oracle.alloc(4)
            triangle = oracle.alloc(12, words([0x010100, 1, 0]))
            oracle.write(state + 0x7C, words([stack]))
            oracle.write(state + 0x88, words([pipe]))
            oracle.write(state + 0x94, words([inputs[4]]))
            oracle.write(state + 0x104, words([0, 64, 0, 64]))
            oracle.write(state + 0x1644, words([4, 1]))
            oracle.write(state + 0x164C, b"\x02")
            for cap, enabled in ((18, inputs[5]), (17, inputs[6]), (8, inputs[7]), (9, 1), (0, 1)):
                oracle.write(state + STATE_CAPABILITIES + cap, bytes([enabled]))
            for offset in (24, 32, 40):
                oracle.write(state + 0x1CD8 + offset, words([dummy]))
            for vertex in range(2):
                record = bytearray(VERTEX_BYTES)
                struct.pack_into("<H", record, 4, 0x2C)
                struct.pack_into("<I", record, 8, 127)
                struct.pack_into("<I", record, 24, inputs[8 + vertex])
                struct.pack_into("<ii", record, 28, *inputs[2 * vertex:2 * vertex + 2])
                struct.pack_into("<H", record, 40, 0xF000 if vertex else 0x1000)
                oracle.write(stack + VERTEX_BUFFER_OFFSET + VERTEX_BYTES * vertex, record)
            captured = [0] * LINE_OUTPUT_WORDS
            call("grDrawClippedLine", [state, triangle, 0x010100])
            captured[-2] = oracle.read(state + STATE_CAPABILITIES, 1)[0]
            captured[-1] = oracle.read(state + STATE_CAPABILITIES + 17, 1)[0]
            actual = native_adapter(lib, "differential_raster_line", inputs, LINE_OUTPUT_WORDS)
            suite.compare("grDrawClippedLine", inputs, line_result(actual), line_result(captured))
    finally:
        oracle.remove_hook(hook)

    run_signature_cases(suite, descriptors)
    run_triangle_gradient_cases(suite)
    from differential_jit_x86 import run as run_x86_jit
    run_x86_jit(suite, descriptors)


def run_triangle_gradient_cases(suite):
    lib, oracle, rng = suite.lib, suite.oracle, suite.rng
    adapter = lib.differential_raster_triangle_gradients
    adapter.argtypes = [C.c_void_p] * 3
    adapter.restype = None
    masks = (0, 1, 2, 4, 8, 16, 32, 63, 64, 128, 256, 512, 1023)
    permutations = (0x020100, 0x010200, 0x020001, 0x000201, 0x010002, 0x000102)
    for i in range(suite.samples):
        control = (masks[i % len(masks)], (i // len(masks)) % 2,
                   i % 3, (i // 3) % 2, permutations[i % len(permutations)])
        records = bytearray(3 * VERTEX_BYTES)
        for vertex, (x, y) in enumerate(((8, 8), (48, 12), (16, 48))):
            base = vertex * VERTEX_BYTES
            struct.pack_into("<iii", records, base + 28,
                             x * 4096 + rng.randrange(4096), y * 4096 + rng.randrange(4096),
                             rng.randrange(0x1000000))
            struct.pack_into("<8H", records, base + 40, *[rng.randrange(65536) for _ in range(8)])
            struct.pack_into("<4i", records, base + 56,
                             *[rng.randrange(-2097152, 2097152) for _ in range(4)])
        records = bytes(records)
        oracle.reset()
        state = oracle.alloc(0x2000)
        pipe = oracle.alloc(1024, b"\xA5" * 1024)
        stack = oracle.alloc(23860)
        vertices = stack + VERTEX_BUFFER_OFFSET
        oracle.write(vertices, records)
        oracle.write(state + 0x7C, words([stack]))
        oracle.write(state + 0x88, words([pipe]))
        oracle.write(state + STATE_CAPABILITIES + 9, bytes([control[1]]))
        oracle.write(state + 0x1644, words([control[0]]))
        for unit in range(2):
            texture = oracle.alloc(208)
            pixel = oracle.alloc(1)
            oracle.write(texture, struct.pack("<Ihh", pixel, 3, 2))
            oracle.write(texture + 180, struct.pack("<h", 1))
            oracle.write(state + 0x1DC + 32 * unit, words([texture]))
        area64 = oracle.call(TARGETS["grComputeTriangleArea"][0], [vertices, control[4]], return64=True)
        area = oracle.call(TARGETS["grRoundTriangleArea"][0], [area64 & 0xFFFFFFFF, area64 >> 32])
        triangle = oracle.alloc(12, words([control[4], area, control[3]]))
        oracle.watch(pipe + 192, 120, "triangle gradient arrays")
        oracle.call(TARGETS["grSetupTriangleGradients"][0], [state, triangle, control[2], control[0]])
        expected = (area, *unpack(oracle.readback(pipe + 192, 120)))
        actual = (C.c_uint32 * 31)()
        adapter(buffer(words(control)), buffer(records), actual)
        suite.compare("grSetupTriangleGradients", (control, records.hex()), tuple(actual), expected)


# Scalar state fields read by the original signature evaluator. Pointer-bearing
# fields are constructed independently in the C adapter and emulated memory.
SIGNATURE_FIELDS = (
    ("colorMask", 0x1F, 1), ("useDithering", 0x36, 1), ("logicOp", 0x0F, 1),
    ("srcBlendFactor", 0x34, 1), ("dstBlendFactor", 0x35, 1),
    ("depthMask", 0x0A, 1), ("depthFunc", 0x2F, 1),
    ("depthOffsetFactor", 0x98, 4), ("depthOffsetUnits", 0x9C, 4),
    ("viewportScaleZ", 0x138, 4), ("fogColor", 0x22C, 4),
    ("alphaFunc", 0x2E, 1), ("stencilFunc", 0x30, 1),
    ("stencilFail", 0x31, 1), ("stencilZFail", 0x32, 1), ("stencilZPass", 0x33, 1),
    ("stencilTestMask", 0x2C, 1), ("stencilWriteMask", 0x2D, 1),
    ("screenRectGradients", 0x259, 1), ("bufferMask", None, 0),
    ("capabilities", None, 0), ("textureMask", None, 0), ("packedFogColor", 0x230, 4),
)


class SignatureInput(C.Structure):
    _fields_ = [(name, C.c_uint32) for name, _, _ in SIGNATURE_FIELDS]


def run_signature_cases(suite, descriptors, targets=TARGETS, state_bias=0):
    lib, oracle, rng = suite.lib, suite.oracle, suite.rng
    adapter = lib.differential_raster_signature
    adapter.argtypes = [C.c_void_p] * 4
    adapter.restype = None
    lib.grCompressSignature.argtypes = [C.c_void_p, C.c_void_p]
    lib.grCompressSignature.restype = None
    lib.grExecuteProgram.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
    lib.grExecuteProgram.restype = C.c_int
    signature_bytes, compressed_bytes, instruction_bytes = 144, 40, 8
    program_capacity = 4096
    for i in range(suite.samples):
        values = SignatureInput(
            rng.randrange(16), rng.randrange(2), rng.randrange(16),
            rng.randrange(15), rng.randrange(15), rng.randrange(2), rng.randrange(8),
            rng.choice((-65536, 0, 65536)), rng.choice((-65536, 0, 65536)),
            rng.randrange(2), rng.choice((0, 0xFFFFFFFF, 0x40404040, rng.getrandbits(32))),
            rng.randrange(8), rng.randrange(8), *[rng.randrange(6) for _ in range(3)],
            rng.choice((0, 15, 127, 255)), rng.choice((0, 15, 127, 255)),
            rng.randrange(2), rng.randrange(8), rng.getrandbits(16), i % 4, 0xDEADBEEF)
        descriptor = bytearray(descriptors[(0, 1, 2, 3, 4, 15, 16, 17)[i % 8]])
        descriptor[10] = rng.choice((0, 2, 4))
        descriptor[11] = rng.choice((0, 1, 2, 4))
        units = bytearray()
        for unit in range(2):
            units.extend((i % 18, rng.randrange(2) * 16, rng.randrange(8), rng.randrange(2),
                          rng.randrange(3), rng.randrange(3), (i // 3 + unit) % 6,
                          rng.randrange(8), rng.randrange(6),
                          *[rng.randrange(4) for _ in range(9)],
                          *[rng.choice((2, 3)) for _ in range(3)],
                          rng.choice((1, 2, 4)), rng.choice((1, 2, 4))))
        descriptor, units = bytes(descriptor), bytes(units)
        oracle.reset()
        allocation = oracle.alloc(0x2000)
        state = allocation + state_bias
        signature = oracle.alloc(signature_bytes)
        compressed = oracle.alloc(compressed_bytes)
        oracle.watch(signature, signature_bytes, "pixel signature")
        oracle.watch(compressed, compressed_bytes, "compressed signature")
        oracle.watch(state + 0x230, 4, "packed fog color")
        dummy = oracle.alloc(4)
        for name, offset, size in SIGNATURE_FIELDS:
            if offset is not None:
                oracle.write(state + offset, int(getattr(values, name)).to_bytes(4, "little")[:size])
        oracle.write(state + STATE_CAPABILITIES,
                     bytes((values.capabilities >> bit) & 1 for bit in range(16)))
        oracle.write(state + 0x1CD8, descriptor)
        for channel in range(3):
            oracle.write(state + 0x1CF0 + 8 * channel,
                         words([dummy if values.bufferMask & (1 << channel) else 0]))
        for unit in range(2):
            data = units[23 * unit:23 * (unit + 1)]
            texture = oracle.alloc(208)
            oracle.write(texture + 8, struct.pack("<H", data[0]))
            oracle.write(texture + 183, data[1:2])
            oracle.write(texture + 204, bytes((data[4], data[5], data[2], data[3])))
            oracle.write(state + 0x1DC + 32 * unit, words([texture]))
            oracle.write(state + 0x1E4 + 32 * unit, data[6:])
            oracle.write(state + 0x38 + unit, bytes([(values.textureMask >> unit) & 1]))
        oracle.call(targets["grEvaluatePixelPipeline"][0], [allocation, signature])
        oracle.call(targets["grCompressSignature"][0], [compressed, signature])
        expected = oracle.readback(signature, signature_bytes) + oracle.readback(compressed, compressed_bytes)
        expected += oracle.readback(state + 0x230, 4)
        actual = buffer(bytes(len(expected)))
        adapter(C.byref(values), buffer(descriptor), buffer(units), actual)
        description = (i, tuple(getattr(values, name) for name, _, _ in SIGNATURE_FIELDS),
                       descriptor.hex(), units.hex())
        suite.compare("grEvaluatePixelPipeline", description, bytes(actual), expected)

        # Exercise packing with legal values across every packed bit, including
        # flags the evaluator currently leaves at zero.
        widths = (5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 0, 0, 0,
                  1, 4, 1, 1, 3, 1, 3, 1, 1, 1, 4, 1, 4, 4, 1, 1,
                  1, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                  1, 0, 0, 0, 0, 0, 0, 0)
        texture_widths = (5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 0, 0,
                          1, 1, 3, 1, 1, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
                          2, 2, 2, 2, 3, 3, 2, 2, 1, 1, 0, 0)
        packed_input = bytes(rng.getrandbits(width) for width in widths + 2 * texture_widths)
        oracle.write(signature, packed_input)
        oracle.call(targets["grCompressSignature"][0], [compressed, signature])
        packed_output = buffer(bytes(compressed_bytes))
        lib.grCompressSignature(packed_output, buffer(packed_input))
        suite.compare("grCompressSignature", packed_input.hex(), bytes(packed_output),
                      oracle.readback(compressed, compressed_bytes))

        if i < min(suite.samples, 16):
            # Compare the generated instruction stream before compilation;
            # both generators consume the same original evaluated signature.
            oracle.write(signature, expected[:signature_bytes])
            program = oracle.alloc(program_capacity * instruction_bytes)
            oracle.watch(program, program_capacity * instruction_bytes, "generated HC instructions")
            count = oracle.call(targets["grExecuteProgram"][0], [program, signature, program_capacity],
                                instruction_budget=2000000)
            native_program = buffer(bytes(program_capacity * instruction_bytes))
            native_count = lib.grExecuteProgram(native_program, buffer(expected[:signature_bytes]), program_capacity)
            suite.compare("grExecuteProgram", description,
                          (native_count, bytes(native_program)),
                          (count, oracle.readback(program, program_capacity * instruction_bytes)))


def run_gradient_boundary_cases(suite):
    lib, oracle = suite.lib, suite.oracle
    targets = ((0x15D0C, 0x15E54), (0x15E54, 0x15F24)) if suite.arch == "arm" else (
        TARGETS["grSetupGradient"], TARGETS["grComputeDeltas"])
    for mode, name in enumerate(("grSetupGradient", "grComputeDeltas")):
        suite.register(name, *targets[mode])
        for shift in (0, 17, 18, 31):
            for g01, g02 in ((-2147483648, 1), (1, -2147483648),
                              (-2147483648, -2147483648), (2147483647, -2147483648)):
                for origin in (0, 4096):
                    context = (origin, origin, 16384, 0, 0, 16384, shift)
                    inputs = (mode, *context, 6, g01, g02, -2147483648)
                    oracle.reset()
                    pipe = oracle.alloc(1024, b"\x5A" * 1024)
                    info = oracle.alloc(32, words((pipe, *context)))
                    oracle.watch(pipe + 192, 120, "boundary gradients")
                    arguments = [info, 6, g01, g02]
                    if mode == 0:
                        arguments.append(-2147483648)
                    oracle.call(targets[mode][0], arguments)
                    actual = native_adapter(lib, "differential_raster_gradient", inputs, 30)
                    suite.compare(name, inputs, actual, unpack(oracle.readback(pipe + 192, 120)))


def run_depth_offset_cases(suite):
    oracle = suite.oracle
    target = (0x1618C, 0x16208) if suite.arch == "arm" else (0x4247B0, 0x424890)
    suite.register("grComputeDepthOffset", *target)
    bias = -8 if suite.arch == "arm" else 0
    hook = None
    if suite.arch == "arm":
        # ARM inlines this helper in triangle setup. Enter with its state
        # stack slot populated, and stop immediately after the reference write.
        hook = oracle.add_hook(target[1], lambda machine: machine.return_from_call())
    try:
        for units, factor, reference, xadd, yadd in (
                (-1, 0, 0, 0, 0), (2147483647, 0, 1, 0, 0),
                (-2147483648, 0, -2147483648, 0, 0), (-65536, 0, 0, 0, 0),
                (2147483647, 2147483647, 2147483647, 65536, 0),
                (0, 2147483647, 1, -2147483648, 65536),
                (-2147483648, -2147483648, -1, 2147483647, -65536),
                (65536, -65536, 2147483647, -65536, 65536)):
            for depth_bytes in (0, 1, 2, 4):
                oracle.reset()
                state = oracle.alloc(0x2000)
                pipe = oracle.alloc(1024)
                oracle.write(state + 0x88 + bias, words((pipe,)))
                oracle.write(state + 0x98 + bias, words((factor, units)))
                oracle.write(state + 0x1CE2 + bias, bytes((depth_bytes,)))
                oracle.write(pipe + 192, words((reference,)))
                oracle.write(pipe + 232, words((xadd,)))
                oracle.write(pipe + 272, words((yadd,)))
                oracle.watch(pipe + 192, 4, "depth offset reference")
                arguments = [0] * 25 + [state] if suite.arch == "arm" else [state]
                oracle.call(target[0], arguments)
                inputs = (units, factor, depth_bytes, reference, xadd, yadd)
                actual = native_adapter(suite.lib, "differential_raster_depth_offset", inputs, 1)
                suite.compare("grComputeDepthOffset", inputs, actual, unpack(oracle.readback(pipe + 192, 4)))
    finally:
        if hook is not None:
            oracle.remove_hook(hook)


def run_texture_unit_cases(suite):
    lib, oracle, rng = suite.lib, suite.oracle, suite.rng
    target = (0x155F8, 0x15964) if suite.arch == "arm" else TARGETS["grSetupTextureUnit"]
    state_bias = -8 if suite.arch == "arm" else 0
    suite.register("grSetupTextureUnit", *target)
    for i in range(max(2, suite.samples)):
        unit = i % 2
        log_s, log_t = rng.randrange(11), rng.randrange(11)
        min_filter, mag_filter = (i // 2) % 8, (i // 16) % 2
        levels = max(log_s, log_t) + 1
        mask = (1 + (i // 32) % 3) << (6 + 2 * unit)
        adders = [rng.choice((-1, 0, 1)) * (1 << rng.randrange(27)) for _ in range(4)]
        inputs = [unit, log_s, log_t, min_filter, mag_filter, (i // 3) % 3, i % 3,
                  0x20 if min_filter != mag_filter else 0, levels,
                  rng.randint(-2097152, 2097152), rng.randint(-4, 10),
                  rng.randint(-2097152, 2097152), rng.randint(-4, 10),
                  *adders, rng.choice((0, 4096)), rng.choice((0, 4096)), mask]
        if i < 2:
            # INT_MIN exercises gradient-magnitude overflow.
            log_s = log_t = 1
            min_filter = mag_filter = 0
            levels = 2
            adders = [-2147483648, 0, 0, 0]
            mask = 3 << (6 + 2 * unit)
            inputs = [unit, 1, 1, 0, 0, 1, 1, 0, 2,
                      0, 0, 0, 0, *adders, 4096, 0, mask]
        oracle.reset()
        state = oracle.alloc(0x1800)
        pipe = oracle.alloc(1024, b"\x5A" * 1024)
        pixels = oracle.alloc(44)
        palette = oracle.alloc(4)
        texture_data = bytearray(208)
        for level in range(11):
            struct.pack_into("<Ihh", texture_data, 16 * level,
                             pixels + 4 * level, max(0, log_s - level), max(0, log_t - level))
        struct.pack_into("<IhBB", texture_data, 176, palette, levels, 0, inputs[7])
        texture_data[204:208] = bytes((inputs[5], inputs[6], min_filter, mag_filter))
        texture = oracle.alloc(208, bytes(texture_data))
        coords = oracle.alloc(16, words(inputs[9:13]))
        oracle.write(state + 0x88 + state_bias, words([pipe]))
        oracle.write(state + 0x1DC + state_bias + 32 * unit, words([texture]))
        oracle.write(state + 0x1644 + state_bias, words([mask]))
        gradient = 6 + 2 * unit
        oracle.write(pipe + 232 + 4 * gradient, words([adders[0], adders[2]]))
        oracle.write(pipe + 272 + 4 * gradient, words([adders[1], adders[3]]))
        oracle.watch(pipe + 64 * unit, 36, "texture unit")
        oracle.watch(pipe + 192, 120, "texture gradients")
        oracle.call(target[0], [state, unit, coords, inputs[17], inputs[18]])
        record = unpack(oracle.readback(pipe + 64 * unit, 36))
        expected = ((record[1] - pixels) // 4, *record[3:9], int(record[2] == palette))
        expected += unpack(oracle.readback(pipe + 192, 120))
        actual = native_adapter(lib, "differential_raster_texture", inputs, 38)
        suite.compare("grSetupTextureUnit", inputs, actual, expected)


def run_arm(suite):
    lib, oracle = suite.lib, suite.oracle
    for name, (start, end) in ARM_TARGETS.items():
        suite.register(name, start, end)
    lib.grrGetTextureFormatDescriptor.argtypes = [C.c_void_p, C.c_int]
    lib.grrGetTextureFormatDescriptor.restype = None
    descriptors = []
    for fmt in range(18):
        oracle.reset()
        descriptor = oracle.alloc(16)
        oracle.watch(descriptor, 16, "texture format descriptor")
        oracle.call(ARM_TARGETS["grrGetTextureFormatDescriptor"][0], [descriptor, fmt])
        expected = oracle.readback(descriptor, 16)
        actual = buffer(bytes(16))
        lib.grrGetTextureFormatDescriptor(actual, fmt)
        suite.compare("grrGetTextureFormatDescriptor", fmt, bytes(actual), expected)
        descriptors.append(expected)
    run_texture_unit_cases(suite)
    run_signature_cases(suite, descriptors, ARM_TARGETS, -8)
    run_compiled_span_cases(suite, descriptors)
    run_pixel_math_cases(suite, descriptors)
    run_pixel_sampling_cases(suite, descriptors)
    run_pixel_blend_cases(suite, descriptors)


def compare_compiled_pipeline(suite, program, native_program, description, instruction_budget=20000000):
    # Enter after the original setjmp wrapper: all allocation, optimization,
    # register allocation and ARM emission execute inside the bounded arena.
    oracle, lib = suite.oracle, suite.lib
    arena_bytes, output_capacity, parameters = 1024 * 1024, 65536, 78
    hc_opt_all = 0x3FF
    arena = oracle.alloc(arena_bytes)
    manager = oracle.alloc(128)
    output = oracle.alloc(4)
    mask = oracle.alloc(16)
    mutable = oracle.alloc(12, bytes.fromhex("fefffeff00fcffffffff0000"))
    oracle.watch(mask, 16, "compiled parameter mask")
    oracle.call(ARM_TARGETS["HCArena_init"][0], [manager, arena, arena_bytes])
    backend = oracle.call(ARM_TARGETS["HCARMBackend_create"][0], [])
    length = oracle.call(ARM_TARGETS["compileInternal"][0],
                         [manager, output, 0, program, backend, hc_opt_all, mask, mutable, parameters],
                         instruction_budget=2 * (100000000 if description == ("span", 15) else instruction_budget),
                         trace_blocks=False, predicate_range=ARM_TARGETS["compileInternal"])
    code, = unpack(oracle.read(output, 4))
    if length > output_capacity or not arena <= code <= arena + arena_bytes - length:
        raise ValueError("original compiler output exceeds its bounded arena")
    native_output = buffer(bytes(output_capacity))
    native_mask = buffer(bytes(16))
    lib.differential_raster_compile.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_int, C.c_void_p]
    lib.differential_raster_compile.restype = C.c_int
    native_length = lib.differential_raster_compile(buffer(native_program), hc_opt_all,
                                                   native_output, output_capacity, native_mask)
    if native_length < 0 or native_length > output_capacity:
        raise ValueError("recovered compiler output exceeds its bounded arena")
    original_code = oracle.readback(code, length)
    recovered_code = bytes(native_output)[:native_length]
    suite.compare("compileInternal", description,
                  bytes(native_mask), oracle.readback(mask, 16))
    suite.compare_code("compileInternal", description, recovered_code, original_code)
    return original_code, recovered_code


def run_compiled_span_cases(suite, descriptors):
    oracle, lib = suite.oracle, suite.lib
    for mode in range(min(suite.samples, 16)):
        signature = bytearray(144)
        signature[:16] = descriptors[2]
        signature[10:12] = bytes((2, 1))
        for index, value in ((16, 1), (17, 15), (42, 1), (47, 1)):
            signature[index] = value
        if mode == 1:
            signature[18], signature[21], signature[22] = 1, 1, 3
        elif mode == 2:
            signature[27:30] = bytes((1, 4, 5))
        elif mode == 3:
            signature[31:38] = bytes((1, 1, 1, 3, 2, 3, 1))
        elif mode == 4:
            signature[19], signature[20] = 1, 4
        elif 5 <= mode < 12:
            source, destination = ((1, 0), (0, 1), (2, 3), (6, 7), (8, 9), (10, 1), (1, 10))[mode - 5]
            signature[27:30] = bytes((1, source, destination))
        elif mode >= 12:
            texture = bytearray(44)
            texture[:16] = descriptors[2]
            texture[16], texture[17] = 1, 1
            texture[19] = texture[20] = (mode - 12) // 2
            texture[21] = texture[22] = (mode - 12) % 2
            texture[27] = texture[31] = texture[35] = 2
            texture[36] = texture[37] = texture[40] = texture[41] = 1
            signature[56:100] = texture
        signature = bytes(signature)
        oracle.reset()
        sig = oracle.alloc(144, signature)
        program = oracle.alloc(32768)
        oracle.watch(program, 32768, "generated HC instructions")
        count = oracle.call(ARM_TARGETS["grExecuteProgram"][0], [program, sig, 4096],
                            instruction_budget=2000000)
        native_program = buffer(bytes(32768))
        native_count = lib.grExecuteProgram(native_program, buffer(signature), 4096)
        suite.compare("grExecuteProgram", ("span", mode),
                      (native_count, bytes(native_program)), (count, oracle.readback(program, 32768)))
        code = compare_compiled_pipeline(suite, program, bytes(native_program), ("span", mode))
        compare_pixel_span(suite, signature, code, mode)


def compare_pixel_span(suite, signature, code, mode, inputs=None, gradients=None, compiler_description=None,
                       sampling=None, initial=None):
    oracle, lib = suite.oracle, suite.lib
    references = [0x10000000, 0, 80 << 16, 40 << 16, 180 << 16, 64 << 16, 0, 0, 0, 0]
    x_adders = [0x01000000, 0, 7 << 16, 9 << 16, -(11 << 16), 31 << 16, 0, 0, 0, 0]
    y_adders = [0x02000000, 0, 4 << 16, 3 << 16, 2 << 16, 5 << 16, 0, 0, 0, 0]
    gradients = list(gradients) if gradients is not None else references + x_adders + y_adders
    texture_record = signature[56:100]
    if inputs is None and texture_record[16]:
        shift = 31 if texture_record[21] == 0 else 20
        gradients[6], gradients[7] = -(1 << (shift - 1)), 1 << (shift - 2)
        gradients[16], gradients[17] = 3 << (shift - 1), 0
        gradients[26], gradients[27] = 0, 3 << (shift - 2)
    initial = initial or (words([0x8F3A716C + i for i in range(8)]) + b"\xA5" * 8,
               struct.pack("<8H", *([0x2000, 0xFFFF] * 4)) + b"\xA5" * 8,
               bytes((1, 2, 3, 4, 4, 3, 2, 1)) + b"\xA5" * 8)
    buffers = [oracle.alloc(len(data), data) for data in initial]
    for address, data, label in zip(buffers, initial, ("color pixels", "depth pixels", "stencil pixels")):
        oracle.watch(address, len(data), label)
    stack = oracle.alloc(16384)
    pipe = stack + 64  # HCARMBackend scratch space before pixel parameters.
    spans = oracle.alloc(8, words([4 << 16, 1 | (2 << 16)]))
    if signature[45]:
        kernel = (C.c_uint32 * 4).in_dll(getattr(lib, "library", lib), "g_orderedDitherKernel")
        oracle.write(pipe + 128, words([oracle.alloc(16, bytes(kernel))]))
    for unit in range(2):
        texture_record = signature[56 + 44 * unit:100 + 44 * unit]
        if not texture_record[16]:
            continue
        sample = inputs[4 * unit:4 * unit + 4] if inputs is not None else [0xFF204060, 0xFF80A0C0, 0xFF10D070, 0xFFF03090]
        texels = oracle.alloc(16, words(sample))
        shift_s = 31 if texture_record[21] == 0 else 20
        shift_t = 31 if texture_record[22] == 0 else 20
        env_color = inputs[8 + unit] if inputs is not None else 0
        sample_parameters = sampling[6 * unit:6 * unit + 6] if sampling is not None else [1, 1, shift_s, shift_t, 1, texture_record[19]]
        s_mask, t_mask, shift_s, shift_t, width_log, linear = sample_parameters
        oracle.write(pipe + 64 * unit, words([env_color, texels, 0, linear,
                                             s_mask, t_mask, shift_s, shift_t, width_log]))
    emitted_results = []
    saved_registers = (ARM.UC_ARM_REG_R4, ARM.UC_ARM_REG_R5, ARM.UC_ARM_REG_R6, ARM.UC_ARM_REG_R7,
                       ARM.UC_ARM_REG_R8, ARM.UC_ARM_REG_R9, ARM.UC_ARM_REG_R10, ARM.UC_ARM_REG_R11)
    initial_registers = {register: 0x12345670 + index * 0x11111111
                         for index, register in enumerate(saved_registers)}
    expected_abi = (*initial_registers.values(), oracle.STACK + oracle.STACK_SIZE - 4096)
    for emitted in code:
        for address, data in zip(buffers, initial):
            oracle.write(address, data)
        fog_color = inputs[10] if inputs is not None else 0
        if signature[41]:
            fog_color &= 255
        oracle.write(pipe + 132, words([16, 8, 4, fog_color, 64, 3, 3, 255, 255]))
        oracle.write(pipe + 168, words([*buffers, 2, spans, 0]))
        oracle.write(pipe + 192, words(gradients))
        entry = oracle.install_code(emitted)
        oracle.call(entry, [stack], initial_registers=initial_registers)
        pixels = tuple(oracle.readback(address, len(data)) for address, data in zip(buffers, initial))
        registers = tuple(oracle.uc.reg_read(register) for register in (*saved_registers, ARM.UC_ARM_REG_SP))
        emitted_results.append((pixels, registers))
    suite.compare("compiledPixelSpan ABI", mode,
                  (emitted_results[0][1], emitted_results[1][1]), (expected_abi, expected_abi))
    suite.compare("compiledPixelSpan", (mode, "emitted code"), emitted_results[1], emitted_results[0])
    if emitted_results[1] == emitted_results[0]:
        suite.confirm_code_semantics("compileInternal", compiler_description or ("span", mode))
    actual = [buffer(data) for data in initial]
    lib.differential_raster_span.argtypes = [C.c_void_p] * 5
    lib.differential_raster_span.restype = None
    if sampling is not None:
        lib.differential_raster_span_sample.argtypes = [C.c_void_p] * 7
        lib.differential_raster_span_sample.restype = None
        lib.differential_raster_span_sample(buffer(signature), buffer(words(gradients)),
                                            *actual, buffer(words(inputs)), buffer(words(sampling)))
    elif inputs is None:
        lib.differential_raster_span(buffer(signature), buffer(words(gradients)), *actual)
    else:
        lib.differential_raster_span_inputs.argtypes = [C.c_void_p] * 6
        lib.differential_raster_span_inputs.restype = None
        lib.differential_raster_span_inputs(buffer(signature), buffer(words(gradients)),
                                            *actual, buffer(words(inputs)))
    suite.compare("compiledPixelSpan", (mode, "hosted pixels"), tuple(bytes(data) for data in actual),
                  emitted_results[0][0])
    for address in buffers:
        oracle.unwatch(address)


def pixel_texture_record(descriptors, fmt=2, mode=0, function=0, scale=0, operands=0):
    """Build the bytecode's texture state, retaining ordinary env modes for C."""
    record = bytearray(44)
    record[:16] = descriptors[fmt]
    record[16] = record[17] = 1
    record[27] = record[31] = record[35] = 2
    record[40] = record[41] = 1
    color, alpha = fmt != 0, fmt not in (1, 3, 15)
    if mode == 5:
        record[36], record[37] = function, function if function < 6 else 0
        record[38], record[39] = scale if isinstance(scale, tuple) else (scale, scale)
        for arg, source in enumerate((0, 3, 1)):
            record[24 + 4 * arg] = record[25 + 4 * arg] = source
        if operands:
            for arg in range(3):
                record[24 + 4 * arg] = (arg + operands) % 4
                record[25 + 4 * arg] = (arg + operands + 1) % 4
                record[26 + 4 * arg] = (arg + operands) % 4
                record[27 + 4 * arg] = 2 + (arg + operands) % 2
    elif mode == 0:
        record[24] = record[25] = 3
        record[36], record[37] = int(not color), int(not alpha)
    elif mode == 1:
        record[25], record[37] = 3, 1
        record[36] = 4 if fmt in (2, 16, 17) else 1
        record[28], record[34] = 3, 2
    elif mode == 2:
        record[24], record[28], record[36] = (1, 3, 4) if color else (3, 0, 1)
        record[25], record[37] = 3, int(not alpha)
    elif mode == 3:
        record[24], record[25] = 0 if color else 3, 0 if alpha else 3
        record[36] = record[37] = 1
    elif mode == 4:
        record[24] = record[25] = 3
        record[36], record[37] = 2 if color else 1, int(not alpha)
    return bytes(record)


def run_pixel_math_cases(suite, descriptors):
    """Reuse each original compiled program over boundary and channel corpora."""
    oracle, lib = suite.oracle, suite.lib
    configurations = []
    # All ordinary environments, including formats with missing color/alpha.
    for mode in range(5):
        for fmt in (2, 0, 1, 3, 4, 15, 16, 17):
            configurations.append((f"env-{mode}-format-{fmt}", [(fmt, mode, 0, 0, 0)], 0, False))
    for function in range(8):
        for scale in range(3):
            configurations.append((f"combine-{function}-scale-{1 << scale}", [(2, 5, function, scale, 0)], 0, False))
    for scale in ((0, 2), (2, 0)):
        configurations.append((f"dot3-rgba-scales-{scale}", [(2, 5, 7, scale, 0)], 0, False))
    for operands in range(1, 5):
        configurations.append((f"operands-{operands}", [(2, 5, 4, 0, operands)], 0, False))
    for fog in (0xFFFFFFFF, 0x00D08020):
        for general in (False, True):
            textures = [(2, 3, 0, 0, 0)] if general else []
            configurations.append((f"fog-{fog:08x}-general-{general}", textures, fog, True))
    for function in (0, 3, 4, 7):
        configurations.append((f"two-units-{function}", [(2, 0, 0, 0, 0), (2, 5, function, 1, 2)], 0, False))
    # Include arithmetic boundary cases even in short --samples 16 runs.
    priority = ("env-0-format-2", "fog-ffffffff-general-False", "combine-6-scale-1",
                "env-1-format-2", "env-2-format-2", "combine-4-scale-1",
                "combine-3-scale-1", "env-4-format-2", "two-units-0",
                "dot3-rgba-scales-(0, 2)", "fog-00d08020-general-True",
                "combine-0-scale-1", "env-1-format-0", "env-1-format-3",
                "env-1-format-4", "two-units-7")
    configurations.sort(key=lambda config: priority.index(config[0])
                        if config[0] in priority else len(priority))
    boundary_pairs = ((1, 128), (128, 1), (54, 54), (0, 128), (0, 255), (255, 0),
                      (127, 128), (128, 127), (254, 255), (255, 254), (255, 255),
                      (0, 0), (127, 127), (128, 128))
    for name, textures, fog_color, fog_enabled in configurations[:suite.samples]:
        signature = bytearray(144)
        signature[:16] = descriptors[2]
        for index, value in ((16, 1), (17, 15), (40, fog_enabled),
                             (41, fog_color == 0xFFFFFFFF), (42, 1), (47, 1)):
            signature[index] = value
        formats, modes = [2, 2], [0, 0]
        for unit, config in enumerate(textures):
            signature[56 + 44 * unit:100 + 44 * unit] = pixel_texture_record(descriptors, *config)
            formats[unit], modes[unit] = config[:2]
        signature = bytes(signature)
        oracle.reset()
        sig = oracle.alloc(144, signature)
        program = oracle.alloc(32768)
        oracle.watch(program, 32768, "generated HC instructions")
        count = oracle.call(ARM_TARGETS["grExecuteProgram"][0], [program, sig, 4096],
                            instruction_budget=2000000)
        native_program = buffer(bytes(32768))
        native_count = lib.grExecuteProgram(native_program, buffer(signature), 4096)
        description = ("pixel math", name)
        suite.compare("grExecuteProgram", description, (native_count, bytes(native_program)),
                      (count, oracle.readback(program, 32768)))
        code = compare_compiled_pipeline(suite, program, bytes(native_program), description)
        for case in range(len(boundary_pairs) + 4):
            if case < len(boundary_pairs):
                source, sample = boundary_pairs[case]
                primary = [source] * 4
                samples = [sample * 0x01010101, (255 - sample) * 0x01010101]
                env = [0xFFFFFFFF, 0x80808080]
                fog_weight = sample
            else:
                primary = [suite.rng.randrange(256) for _ in range(4)]
                samples = [suite.rng.getrandbits(32) for _ in range(2)]
                env = [suite.rng.getrandbits(32) for _ in range(2)]
                fog_weight = suite.rng.randrange(256)
            inputs = [samples[0]] * 4 + [samples[1]] * 4 + env + [fog_color] + formats + modes
            gradients = [0, fog_weight << 16, *[value << 16 for value in primary], 0, 0, 0, 0] + [0] * 20
            compare_pixel_span(suite, signature, code, (name, case), inputs, gradients, description)


def compile_pixel_case(suite, signature, description, instruction_budget=20000000):
    oracle, lib = suite.oracle, suite.lib
    oracle.reset()
    sig, program = oracle.alloc(144, signature), oracle.alloc(32768)
    oracle.watch(program, 32768, "generated HC instructions")
    count = oracle.call(ARM_TARGETS["grExecuteProgram"][0], [program, sig, 4096],
                        instruction_budget=2000000)
    native_program = buffer(bytes(32768))
    native_count = lib.grExecuteProgram(native_program, buffer(signature), 4096)
    suite.compare("grExecuteProgram", description, (native_count, bytes(native_program)),
                  (count, oracle.readback(program, 32768)))
    return compare_compiled_pipeline(suite, program, bytes(native_program), description,
                                     instruction_budget=instruction_budget)


def run_pixel_sampling_cases(suite, descriptors):
    # Signature filters are (mag, min); the runtime selection is per primitive.
    configurations = [(wrap, wrap, linear, linear, 2, False)
                      for wrap in range(3) for linear in range(2)]
    configurations += [(0, 1, 0, 1, 2, False), (1, 2, 1, 0, 2, False),
                       (0, 0, 0, 1, 2, True), (2, 1, 1, 0, 2, True)]
    configurations += [(0, 0, 1, 1, fmt, False) for fmt in (0, 4, 16, 17)]
    if suite.samples < 256:
        # These retain every sampler branch; larger runs cross formats and filters.
        configurations = [(1, 1, 1, 1, 2, False), (0, 1, 0, 1, 2, False),
                          (2, 1, 1, 0, 2, True)]
    coordinates = ((-257, 128), (-128, 384), (0, 0), (1, 255),
                   (127, 129), (128, 128), (255, 257), (511, 768))
    for configuration in configurations[:suite.samples]:
        wrap_s, wrap_t, mag, minimum, fmt, two_units = configuration
        signature = bytearray(144)
        signature[:16] = descriptors[2]
        for index, value in ((16, 1), (17, 15), (42, 1), (47, 1)):
            signature[index] = value
        for unit in range(1 + two_units):
            texture = bytearray(pixel_texture_record(descriptors, fmt=fmt, mode=3 if unit == 0 else 0))
            texture[17] = 0  # Axes can have different sizes and shifts.
            texture[19:23] = bytes((mag if unit == 0 else 0, minimum if unit == 0 else 0,
                                    wrap_s, wrap_t))
            signature[56 + 44 * unit:100 + 44 * unit] = texture
        signature = bytes(signature)
        description = ("sampling", configuration)
        codes = compile_pixel_case(suite, signature, description, instruction_budget=100000000)
        for linear in sorted({mag, minimum}):
            # These are the dimensions and shifts of the selected mip level.
            for log_s, log_t in ((1, 1), (0, 2), (2, 0), (0, 0)):
                mask_s, mask_t = (1 << log_s) - 1, (1 << log_t) - 1
                shift_s = (32, 21, 31)[wrap_s] - log_s if log_s else 0
                shift_t = (32, 21, 31)[wrap_t] - log_t if log_t else 0
                sampling = ([mask_s, mask_t, shift_s, shift_t, log_s, linear]
                            + [mask_s, mask_t, shift_s, shift_t, log_s, 0])
                for case, (s, t) in enumerate(coordinates):
                    # Match grSetupTextureUnit: an axis of size one is constant zero.
                    raw_s = s * (1 << shift_s) // 256 if mask_s else 0
                    raw_t = t * (1 << shift_t) // 256 if mask_t else 0
                    gradients = [0, 0, 32 << 16, 64 << 16, 128 << 16, 255 << 16,
                                 raw_s, raw_t, raw_t if mask_s else 0, raw_s if mask_t else 0] + [0] * 20
                    inputs = [0x01204060, 0x7F80A0C0, 0x8010D070, 0xFFF03090,
                              0xFF8001FE, 0x0100FF7F, 0x7FFE8001, 0x80017FFF,
                              0, 0, 0, fmt, fmt, 3, 0]
                    compare_pixel_span(suite, signature, codes,
                                       (description, linear, log_s, log_t, case), inputs,
                                       gradients, description, sampling=sampling)
        if wrap_s == wrap_t == 1:
            # grSetupTextureUnit permits shift zero for large clamp coordinates.
            # The original ADD wraps INT_MAX before clamping the neighboring tap.
            for raw_s, raw_t in ((0x7FFFFFFF, 0), (0, 0x7FFFFFFF),
                                 (0x7FFFFFFF, 0x7FFFFFFF), (-0x80000000, 0)):
                gradients = [0, 0, 32 << 16, 64 << 16, 128 << 16, 255 << 16,
                             raw_s, raw_t, 0, 0] + [0] * 20
                compare_pixel_span(suite, signature, codes, (description, "shift zero", raw_s, raw_t),
                                   inputs, gradients, description, sampling=[1, 1, 0, 0, 1, mag] * 2)


def run_pixel_blend_cases(suite, descriptors):
    # Exercise missing/quantized destination alpha and packed framebuffer colors.
    configurations = [(fmt, 4, 5) for fmt in (1, 15, 16, 17)]
    configurations += [(15, 6, 7), (15, 10, 1), (16, 4, 1), (17, 7, 6)]
    if suite.samples < 256:
        # Cover both fast and general blending without destination alpha.
        configurations = configurations[:2]
    colors = ((0, 1, 255, 0), (1, 128, 254, 1), (255, 128, 0, 127),
              (54, 54, 54, 128), (32, 128, 224, 254), (255, 255, 255, 255))
    initial = (words([0xFF00FF00, 0x00FF00FF, 0xFFFFFFFF, 0,
                      0x7F80FF01, 0x807F01FF, 0x01010101, 0xFEFEFEFE]) + b"\xA5" * 8,
               bytes(24), bytes(16))
    for fmt, source, destination in configurations[:suite.samples]:
        signature = bytearray(144)
        signature[:16] = descriptors[fmt]
        for index, value in ((16, 1), (17, 15), (27, 1), (28, source),
                             (29, destination), (42, 1), (47, 1)):
            signature[index] = value
        signature = bytes(signature)
        description = ("blend surface", fmt, source, destination)
        codes = compile_pixel_case(suite, signature, description)
        for case, rgba in enumerate(colors):
            gradients = [0, 0, *[value << 16 for value in rgba], 0, 0, 0, 0] + [0] * 20
            compare_pixel_span(suite, signature, codes, (description, case),
                               [0] * 11 + [2, 2, 0, 0], gradients, description, initial=initial)
