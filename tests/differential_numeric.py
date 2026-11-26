"""Bit-exact numeric cases for the PE32 reference and recovered C library.

The entrypoints use x86 cdecl; 64-bit arguments occupy low/high stack words
and 64-bit results use EDX:EAX.
"""

import ctypes
import struct


TYPES = {"i": ctypes.c_int32, "u": ctypes.c_uint32,
         "q": ctypes.c_int64, "Q": ctypes.c_uint64, "b": ctypes.c_uint8}
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1

# name, start, end, argument types, return type, input family
FUNCTIONS = (
    ("hgClz32", 0x401180, 0x4011ED, "u", "i", "word"),
    ("hgAbs32", 0x409940, 0x409955, "i", "i", "word"),
    ("hgSign32", 0x409830, 0x409855, "i", "i", "word"),
    ("hgSignMask32", 0x41D1C0, 0x41D1CB, "i", "i", "word"),
    ("hgSignBit32", 0x41D1D0, 0x41D1DB, "i", "i", "word"),
    ("hgMaxZero32", 0x408680, 0x408690, "i", "i", "word"),
    ("hgDiff32", 0x41D320, 0x41D334, "ii", "i", "pair"),
    ("hgMin32", 0x403250, 0x403266, "ii", "i", "pair"),
    ("hgMax32", 0x409910, 0x409926, "ii", "i", "pair"),
    ("hgMinu32", 0x41D2B0, 0x41D2C6, "uu", "u", "pair"),
    ("hgMaxu32", 0x41D2D0, 0x41D2E6, "uu", "u", "pair"),
    ("hgInRange32", 0x401EC0, 0x401F47, "iii", "i", "range"),
    ("hgRor32", 0x41D340, 0x41D390, "ii", "i", "rotate"),
    ("hgBitMask32", 0x435830, 0x435879, "ii", "i", "bit"),
    ("hgAdds32", 0x41DBE0, 0x41DC3A, "ii", "i", "pair"),
    ("hgClz64", 0x41DB80, 0x41DBD6, "q", "i", "word64"),
    ("hgAdd64", 0x41D6A0, 0x41D6B1, "qq", "q", "pair64"),
    ("hgSub64", 0x41D6C0, 0x41D6D1, "qq", "q", "pair64"),
    ("hgNeg64", 0x41DA40, 0x41DA52, "q", "q", "word64"),
    ("hgAbs64", 0x41DA70, 0x41DA90, "q", "q", "word64"),
    ("hgLsl64", 0x41D7E0, 0x41D83C, "qi", "q", "shift64"),
    ("hgLsl64h", 0x41D840, 0x41D8A6, "qi", "i", "shift64"),
    ("hgLsr64", 0x41D920, 0x41D978, "qi", "q", "shift64"),
    ("hgAsr64", 0x41D980, 0x41D9DB, "qi", "q", "shift64"),
    ("hgMul64", 0x41DA90, 0x41DA9F, "ii", "q", "pair"),
    ("hgMulu64", 0x41DAA0, 0x41DAAF, "uu", "Q", "pair"),
    ("hgMul64h", 0x41D470, 0x41D482, "ii", "i", "pair"),
    ("hgMulu64h", 0x41D490, 0x41D4A2, "uu", "u", "pair"),
    ("hgMul64x32", 0x41DAB0, 0x41DB4B, "qi", "q", "mixed"),
    ("hgDiv64_32", 0x41DF00, 0x41DFA5, "qi", "q", "divide"),
    ("F_ADD", 0x46C360, 0x46C505, "uu", "u", "float_pair"),
    ("F_MUL", 0x46C510, 0x46C64D, "uu", "u", "float_multiply"),
    ("F_RCP", 0x46C7C0, 0x46C8AB, "u", "u", "float"),
    ("F_RSQ", 0x46C8B0, 0x46C9A6, "u", "u", "float"),
    ("F_ITOF", 0x46C740, 0x46C7BF, "i", "u", "word"),
    ("IScalar_fixedToScalar", 0x468FB0, 0x469031, "ii", "i", "fixed"),
    ("IScalar_scalarToFixed", 0x469040, 0x4690F4, "ii", "i", "scalar_fixed"),
    ("IScalar_byteToScalar", 0x469100, 0x4691A0, "b", "i", "byte"),
    ("IScalar_setAndNormalize", 0x41EAB0, 0x41EB30, "ui", "i", "normalize"),
    ("IUnitScalar_tos", 0x469700, 0x46978F, "i", "i", "unit"),
    ("XScalar_log2", 0x468BC0, 0x468C73, "i", "i", "log"),
    ("XScalar_exp2", 0x468CC0, 0x468FA6, "i", "i", "word"),
    ("imLsl32s", 0x41E4B0, 0x41E517, "ii", "i", "shift32"),
    ("imLsl32us", 0x468C80, 0x468CBB, "ui", "i", "shift32"),
    ("F_SUB", 0x41E1F0, 0x41E20B, "uu", "u", "float_pair"),
    ("F_SQR", 0x46C650, 0x46C734, "u", "u", "float"),
    ("F_SIN", 0x46CEB0, 0x46CEE3, "u", "u", "float"),
    ("F_COS", 0x46CEF0, 0x46CF0E, "u", "u", "float"),
    ("radiansToUnitScalar", 0x46CD60, 0x46CE06, "u", "i", "float"),
    ("IUnitScalar_sin", 0x46C9B0, 0x46CB7F, "i", "i", "unit"),
    ("IUnitScalar_cos", 0x46CB80, 0x46CD5A, "i", "i", "unit"),
    ("IUnitScalar_lerp", 0x4695B0, 0x4696FA, "iii", "i", "lerp"),
    ("IScalar_iftos", 0x408770, 0x4087B3, "i", "i", "word"),
    ("IScalar_expe", 0x41E680, 0x41E6F5, "i", "i", "scalar"),
    ("IScalar_toFixedHO", 0x4691A0, 0x46920F, "ii", "i", "scalar_fixed31"),
    ("IScalar_toUnitScalar", 0x40A170, 0x40A1D7, "i", "i", "scalar"),
    ("IScalar_log2_iterate", 0x4688A0, 0x468BB2, "u", "u", "word"),
)


def word_edges(width):
    """Exercise carries, sign transitions, and the mantissa/word boundaries."""
    mask = (1 << width) - 1
    values = {0, 1, 2, mask, mask - 1}
    for bit in (7, 8, 15, 16, 21, 23, 24, 29, 30, 31, 32, 33, 62, 63):
        if bit < width:
            for delta in (-1, 0, 1):
                values.add(((1 << bit) + delta) & mask)
    return sorted(values)


def float_edges(scalar=False):
    values = {0}
    if not scalar:
        values.add(0x80000000)
    for exponent in (0, 1, 2, 63, 95, 126, 127, 128, 149, 150, 158, 254, 255):
        if scalar and exponent in (0, 255):
            continue
        for mantissa in (0, 1, 0x3FFFFF, 0x7FFFFF):
            if exponent == 255 and mantissa:
                continue  # NaNs violate F_ISVALID.
            for sign in (0, 0x80000000):
                values.add(sign | (exponent << 23) | mantissa)
    return sorted(values)


def random_float(rng, scalar=False):
    exponent = rng.randrange(1, 255) if scalar else rng.randrange(255)
    return rng.getrandbits(1) << 31 | exponent << 23 | rng.getrandbits(23)


def scalar_cases(family, rng, samples):
    words = word_edges(32)
    wide = word_edges(64)
    if family in ("word", "word64", "unit", "log", "byte"):
        if family == "byte":
            yield from ((v,) for v in range(256))
            return
        width = 64 if family == "word64" else 32
        values = list(wide if width == 64 else words)
        values.extend(rng.getrandbits(width) for _ in range(samples))
        if family == "unit":
            values = [0x20000000, -0x20000000] + [
                (v & 0x3FFFFFFF) - 0x20000000 for v in values]
        if family == "log":
            values = [0x80000000] + [v & 0x7FFFFFFF for v in values]
        yield from ((v,) for v in values)
    elif family in ("pair", "pair64", "mixed", "divide"):
        width = 32 if family == "pair" else 64
        other_width = 64 if family == "pair64" else 32
        mask = (1 << other_width) - 1
        for a in words if width == 32 else wide:
            for b in (0, 1, mask, a & mask, (-a) & mask, 1 << (other_width - 1)):
                if family != "divide" or b:
                    yield a, b
        for _ in range(samples):
            b = rng.getrandbits(other_width)
            yield rng.getrandbits(width), b or (family == "divide")
    elif family in ("rotate", "bit", "shift32", "shift64", "fixed", "scalar_fixed", "scalar_fixed31"):
        top = 63 if family == "shift64" else 31 if family in ("bit", "shift32", "scalar_fixed31") else 32
        values = wide if family == "shift64" else (
            float_edges(True) if family.startswith("scalar_fixed") else words)
        shifts = sorted({s for s in (0, 1, 15, 16, 21, 30, 31, 32, 33, 62, 63)
                         if s <= top})
        for value in values:
            for shift in shifts:
                yield value, shift
        for _ in range(samples):
            value = random_float(rng, True) if family.startswith("scalar_fixed") else (
                rng.getrandbits(64 if family == "shift64" else 32))
            yield value, rng.randrange(top + 1)
    elif family in ("float", "scalar", "float_pair", "float_multiply"):
        for a in float_edges(family == "scalar"):
            if family in ("float", "scalar"):
                yield (a,)
            else:
                for b in (0, 0x3F800000, a, a ^ 0x80000000, 0x7F7FFFFF):
                    yield a, b
                if family == "float_multiply":
                    # Operand two distinguishes signed zero from subnormals;
                    # operand one instead tests just the exponent field.
                    for b in (0x80000000, 1, 0x80000001, 0x007FFFFF,
                              0x807FFFFF, 0x00800000, 0x80800000):
                        yield a, b
        for _ in range(samples):
            a = random_float(rng, family == "scalar")
            yield (a,) if family in ("float", "scalar") else (a, random_float(rng))
    elif family == "lerp":
        for a in (0, 1, 0x10000000, 0x1FFFFFFF, 0x20000000):
            for b in (0, 1, 0x20000000):
                for factor in (0, 1, 0x10000000, 0x1FFFFFFF, 0x20000000):
                    yield a, b, factor
        for _ in range(samples):
            yield tuple(rng.randrange(0x20000001) for _ in range(3))
    elif family == "normalize":
        # Keep the signed input within the internal mantissa domain. The
        # original itself asserts for e=0,m=0x80000001 after normalization.
        for e in (0, 1, 30, 31, 126, 127, 128, 253, 254):
            for m in [0, 0x40000000] + [(v & 0x7FFFFFFF) - 0x40000000 for v in words]:
                yield e, m
        for _ in range(samples):
            yield rng.randrange(255), rng.randrange(-0x40000000, 0x40000001)
    elif family == "range":
        signed = [ctypes.c_int32(v).value for v in words]
        for lo, hi in ((-0x80000000, 0x7FFFFFFF), (-0x80000000, -1),
                       (-1, 1), (0, 0), (0, 0x7FFFFFFF)):
            for a in signed:
                yield a, lo, hi
        for _ in range(samples):
            lo, hi = sorted((rng.randrange(-0x80000000, 0x80000000),
                             rng.randrange(-0x80000000, 0x80000000)))
            yield rng.randrange(-0x80000000, 0x80000000), lo, hi
    else:
        raise ValueError(f"Unknown numeric input family: {family}")


def stack_words(signature, args):
    result = []
    for kind, value in zip(signature, args):
        result.append(value & MASK32)
        if kind in "qQ":
            result.append((value >> 32) & MASK32)
    return result


def host_buffer(data=b"", size=None):
    """Keep pointer arguments aligned for the native 64-bit fields."""
    result = (ctypes.c_uint64 * (((size or len(data)) + 7) // 8))()
    ctypes.memmove(result, data, len(data))
    return result


def compare_output(suite, name, address, arguments, size, alias=None, returns=False,
                   original_arguments=None):
    """Compare an output-first C function with bytes as pointer arguments."""
    native_args = [host_buffer(a) if isinstance(a, bytes) else a for a in arguments]
    native_output = native_args[alias] if alias is not None else host_buffer(b"\xa5" * size)
    result = getattr(suite.lib, name)(native_output, *native_args)
    expected = bytes(native_output)[:size]
    suite.oracle.reset()
    original_args = [suite.oracle.alloc(len(a), a) if isinstance(a, bytes) else a & MASK32
                     for a in (original_arguments or arguments)]
    original_output = original_args[alias] if alias is not None else suite.oracle.alloc(size, b"\xa5" * size)
    actual_result = suite.oracle.call(address, [original_output, *original_args])
    actual = suite.oracle.read(original_output, size)
    if returns:
        expected, actual = (result & MASK32, expected), (actual_result, actual)
    suite.compare(name, {"arguments": [a.hex() if isinstance(a, bytes) else a for a in arguments],
                         "alias": alias}, expected, actual)
    return bytes(native_output)[:size]


def independent_vector(values):
    """Encode finite float components in the independent-exponent layout."""
    words = struct.unpack("<" + "I" * len(values), struct.pack("<" + "f" * len(values), *values))
    exponent = 0
    mantissas = []
    for i, word in enumerate(words):
        e = (word >> 23) & 255
        if not 0 < e < 255 and word & 0x7FFFFFFF:
            raise ValueError("vector generator requires finite normal components or zero")
        exponent |= e << (8 * i)
        m = ((word & 0x7FFFFF) | 0x800000) << 6 if e else 0
        mantissas.append(-m if word & 0x80000000 else m)
    return struct.pack("<I" + "i" * len(values), exponent, *mantissas)


def check_vector_length(suite, address_map=None):
    name = "IVector3_length"
    start, end = (address_map or {}).get(name, (0x46A1C0, 0x46A4BC))
    suite.register(name, start, end)
    native = getattr(suite.lib, name)
    native.argtypes = [ctypes.c_void_p]
    native.restype = ctypes.c_int32
    cases = [(0, 0, 0, 0)]
    for exponent in (1, 2, 126, 127, 128, 253, 254):
        for sign in (-1, 1):
            for m in (0x20000000, 0x20000001, 0x3FFFFFFF):
                cases.extend(((exponent, sign * m, 0, 0),
                              (exponent, sign * m, -m, m)))
    for _ in range(suite.samples):
        mantissas = [suite.rng.randrange(-0x3FFFFFFF, 0x40000000) for _ in range(3)]
        axis = suite.rng.randrange(3)
        mantissas[axis] = suite.rng.choice((-1, 1)) * suite.rng.randrange(0x20000000, 0x40000000)
        cases.append((suite.rng.randrange(1, 255), *mantissas))
    for values in cases:
        packed = struct.pack("<Iiii", *values)
        host = ctypes.create_string_buffer(packed)
        expected = native(host) & MASK32
        suite.oracle.reset()
        pointer = suite.oracle.alloc(len(packed), packed)
        actual = suite.oracle.call(start, [pointer])
        suite.compare(name, {"vector": values}, expected, actual)


def check_matrices(suite, address_map=None):
    """Compare matrix storage and derived transforms without assuming host ABI."""
    entries = {
        "IMatrix4x4_setfv": (0x4616D0, 0x46180D),
        "IMatrix4x4_mul": (0x462140, 0x462207),
        "IMatrix4x4_inverse": (0x465E50, 0x46665E),
        "IMatrix4x4_det2h": (0x465C00, 0x465E4A),
        "IVertexTransform_copy": (0x463350, 0x46368D),
        "IVertexTransform_zmulxv4": (0x464870, 0x464A90),
        "IMatrix4x4_getfv": (0x461810, 0x461981),
        "IMatrix4x4_transpose": (0x461520, 0x4616C8),
        "IMatrix4x4_inversetranspose": (0x466660, 0x4669CE),
        "IMatrix4x4_frustum": (0x462280, 0x4625DB),
        "IMatrix4x4_ortho": (0x465150, 0x465485),
        "IMatrix4x4_rotate": (0x465730, 0x465BFE),
        "IMatrix4x4_scale": (0x465490, 0x465625),
        "IMatrix4x4_translate": (0x465630, 0x46572F),
        "IMatrix4x4_mulfv4": (0x464FD0, 0x46514A),
        "IVertexTransform_mulxv4": (0x4638F0, 0x464388),
        "IVertexTransform_mulxv4s": (0x466EF0, 0x4672C6),
        "IVertexTransform_mulfv4": (0x464390, 0x4647C0),
        "IVertexTransform_mulfv4s": (0x4669D0, 0x466EE5),
        "IVertexTransform_zmulfv4": (0x4647C0, 0x46486D),
    }
    if address_map is not None:
        entries = {name: address_map[name] for name in entries}
    functions = {}
    for name, (start, end) in entries.items():
        suite.register(name, start, end)
        functions[name] = getattr(suite.lib, name)
    pointer = ctypes.c_void_p
    functions["IMatrix4x4_setfv"].argtypes = [pointer, pointer]
    functions["IMatrix4x4_mul"].argtypes = [pointer, pointer, pointer]
    functions["IMatrix4x4_inverse"].argtypes = [pointer, pointer, ctypes.c_int32]
    functions["IMatrix4x4_det2h"].argtypes = [pointer] + [ctypes.c_int32] * 4
    functions["IVertexTransform_copy"].argtypes = [pointer, pointer]
    functions["IVertexTransform_zmulxv4"].argtypes = [pointer, pointer]
    prepare_float = suite.lib.IVertexTransform_prepareFloat
    prepare_float.argtypes = [pointer, pointer, ctypes.c_uint32]
    prepare_float.restype = None
    prepared_float = suite.lib.IVertexTransform_mulfv4Prepared
    prepared_float.argtypes = [pointer, pointer, pointer, pointer]
    prepared_float.restype = None
    for name in ("IMatrix4x4_getfv", "IMatrix4x4_transpose", "IVertexTransform_zmulfv4"):
        functions[name].argtypes = [pointer, pointer]
    functions["IMatrix4x4_inversetranspose"].argtypes = [pointer, pointer, ctypes.c_int32]
    functions["IMatrix4x4_rotate"].argtypes = [pointer, pointer, ctypes.c_int32, pointer]
    for name in ("IMatrix4x4_frustum", "IMatrix4x4_ortho", "IMatrix4x4_scale",
                 "IMatrix4x4_translate", "IMatrix4x4_mulfv4", "IVertexTransform_mulxv4",
                 "IVertexTransform_mulxv4s", "IVertexTransform_mulfv4", "IVertexTransform_mulfv4s"):
        functions[name].argtypes = [pointer, pointer, pointer]
    for name, function in functions.items():
        function.restype = ctypes.c_int32 if name in (
            "IMatrix4x4_det2h", "IMatrix4x4_inversetranspose",
            "IVertexTransform_zmulfv4", "IVertexTransform_zmulxv4") else None

    identity = [1.0 if c == r else 0.0 for c in range(4) for r in range(4)]
    matrices = [identity,
                [1, 2, 0, 0, 3, 7, 0, 0, 0, 0, 1, 0, 5, 11, 0, 1],
                [1, 0, 2, 0, 0, 1, 4, 0, 0, 0, -0.5, 0, 0, 0, 8, 1]]
    matrices.append([1.5, 0, 0, 0, 0, 2, 0, 0, 0.25, -0.125, -1.02, -1,
                     0, 0, -2.02, 0])
    for epsilon in (0.0, 2 ** -4, 2 ** -16, 2 ** -22):
        # Two nearly equal rows exercise cancellation and the singular path.
        matrices.append([1, 1, 0, 0, 1, 1 + epsilon, 0, 0,
                         0.5, 0.5, 1, 0, -1, -1, 0, 1])
    for _ in range(min(suite.samples, 32)):
        matrix = identity.copy()
        # Triangular dyadic matrices stay nonsingular and cover both signs.
        for c in range(4):
            matrix[5 * c] = suite.rng.choice((-2.0, -0.5, 0.5, 1.0, 2.0))
            for r in range(c):
                matrix[4 * c + r] = suite.rng.randrange(-8, 9) / 4.0
        matrices.append(matrix)
        matrices.append([suite.rng.randrange(-32, 33) / 8.0 for _ in range(16)])

    previous = None
    for matrix_index, matrix in enumerate(matrices):
        packed = struct.pack("<16f", *matrix)
        source = host_buffer(packed)
        native_matrix = host_buffer(b"", 80)
        functions["IMatrix4x4_setfv"](native_matrix, source)
        recovered = bytes(native_matrix)
        suite.oracle.reset()
        original_source = suite.oracle.alloc(64, packed)
        original_matrix = suite.oracle.alloc(80)
        suite.oracle.call(entries["IMatrix4x4_setfv"][0], [original_matrix, original_source])
        original = suite.oracle.read(original_matrix, 80)
        suite.compare("IMatrix4x4_setfv", matrix, recovered, original)

        compare_output(suite, "IMatrix4x4_getfv", entries["IMatrix4x4_getfv"][0], [recovered], 64)
        for alias in (None, 0):
            compare_output(suite, "IMatrix4x4_transpose", entries["IMatrix4x4_transpose"][0],
                           [recovered], 80, alias=alias)
        for rescale in (0, 1):
            compare_output(suite, "IMatrix4x4_inversetranspose",
                           entries["IMatrix4x4_inversetranspose"][0], [recovered, rescale], 40,
                           returns=True)

        operations = {
            "IMatrix4x4_frustum": (-1.0, 2.0, -0.75, 1.25, 0.5, 64.0),
            "IMatrix4x4_ortho": (-2.0, 3.0, -1.0, 2.0, -4.0, 16.0),
            "IMatrix4x4_scale": (-0.5, 2.0, 0.25),
            "IMatrix4x4_translate": (1.25, -2.5, 0.5),
        }
        for name, parameters in operations.items():
            packed_parameters = struct.pack("<" + "f" * len(parameters), *parameters)
            for alias in (None, 0):
                compare_output(suite, name, entries[name][0], [recovered, packed_parameters], 80,
                               alias=alias)
        axis = independent_vector(((1, 0, 0), (0, 1, 0), (0, 0, 1),
                                   (1, -2, 3))[matrix_index % 4])
        angle = struct.unpack("<I", struct.pack("<f", (0, 30, -90, 179.5)[matrix_index % 4]))[0]
        for alias in (None, 0):
            compare_output(suite, "IMatrix4x4_rotate", entries["IMatrix4x4_rotate"][0],
                           [recovered, angle, axis], 80, alias=alias)

        # Reuse identical canonical matrix bytes on both sides for each operation.
        for scale in (0, 1):
            native_result = host_buffer(b"", 80)
            functions["IMatrix4x4_inverse"](native_result, native_matrix, scale)
            suite.oracle.reset()
            argument = suite.oracle.alloc(80, recovered)
            output = suite.oracle.alloc(80)
            suite.oracle.call(entries["IMatrix4x4_inverse"][0], [output, argument, scale])
            suite.compare("IMatrix4x4_inverse", (matrix, scale), bytes(native_result),
                          suite.oracle.read(output, 80))

        for indices in ((0, 0, 1, 1), (0, 2, 3, 3), (1, 0, 2, 2)):
            expected = functions["IMatrix4x4_det2h"](native_matrix, *indices) & MASK32
            suite.oracle.reset()
            argument = suite.oracle.alloc(80, recovered)
            actual = suite.oracle.call(entries["IMatrix4x4_det2h"][0], [argument, *indices])
            suite.compare("IMatrix4x4_det2h", (matrix, indices), expected, actual)

        for alias in ("none", "left", "right"):
            left = host_buffer(recovered)
            right = host_buffer(previous or recovered)
            output = left if alias == "left" else right if alias == "right" else host_buffer(b"", 80)
            functions["IMatrix4x4_mul"](output, left, right)
            suite.oracle.reset()
            a = suite.oracle.alloc(80, recovered)
            b = suite.oracle.alloc(80, previous or recovered)
            d = a if alias == "left" else b if alias == "right" else suite.oracle.alloc(80)
            suite.oracle.call(entries["IMatrix4x4_mul"][0], [d, a, b])
            suite.compare("IMatrix4x4_mul", {"left": recovered.hex(),
                          "right": (previous or recovered).hex(), "alias": alias},
                          bytes(output), suite.oracle.read(d, 80))
        previous = recovered

        native_transform = host_buffer(b"", 200)
        functions["IVertexTransform_copy"](native_transform, native_matrix)
        fields = bytes(native_transform)
        # Original: w, unnormalized, v, sh, cl, exp. Host: v,w,unnormalized,sh,exp,cl.
        canonical = fields[80:112] + fields[112:176] + fields[:80] + fields[176:192] + fields[196:200] + fields[192:196]
        suite.oracle.reset()
        argument = suite.oracle.alloc(80, recovered)
        transform = suite.oracle.alloc(200)
        suite.oracle.call(entries["IVertexTransform_copy"][0], [transform, argument])
        suite.compare("IVertexTransform_copy", matrix, canonical, suite.oracle.read(transform, 200))
        for position in ((0x8000, -0x4000, 0x20000, 0x10000),
                         (-0x8000, 0x4000, 0x20000, -0x10000),
                         (0x40000, 0, 0, 0x20000)):
            packed_position = struct.pack("<4i", *position)
            native_position = host_buffer(packed_position)
            expected = functions["IVertexTransform_zmulxv4"](native_transform, native_position) & MASK32
            suite.oracle.reset()
            transform = suite.oracle.alloc(200, canonical)
            original_position = suite.oracle.alloc(16, packed_position)
            actual = suite.oracle.call(entries["IVertexTransform_zmulxv4"][0], [transform, original_position])
            suite.compare("IVertexTransform_zmulxv4", (matrix, position), expected, actual)
            for name in ("IVertexTransform_mulxv4", "IVertexTransform_mulxv4s"):
                compare_output(suite, name, entries[name][0], [fields, packed_position], 20,
                               original_arguments=[canonical, packed_position])
            vector = independent_vector([v / 65536.0 for v in position])
            compare_output(suite, "IMatrix4x4_mulfv4", entries["IMatrix4x4_mulfv4"][0],
                           [recovered, vector], 20)
            for name in ("IVertexTransform_mulfv4", "IVertexTransform_mulfv4s"):
                expected = compare_output(suite, name, entries[name][0], [fields, vector], 20,
                                          original_arguments=[canonical, vector])
                if name == "IVertexTransform_mulfv4":
                    plan, output = host_buffer(b"", 36), host_buffer(b"", 20)
                    prepare_float(plan, native_transform, struct.unpack_from("<I", vector)[0])
                    prepared_float(output, native_transform, host_buffer(vector), plan)
                    suite.compare(name, {"matrix": matrix, "position": position, "prepared": True},
                                  bytes(output)[:20], expected)
            native_vector = host_buffer(vector)
            expected = functions["IVertexTransform_zmulfv4"](native_transform, native_vector) & MASK32
            suite.oracle.reset()
            transform = suite.oracle.alloc(200, canonical)
            original_vector = suite.oracle.alloc(20, vector)
            actual = suite.oracle.call(entries["IVertexTransform_zmulfv4"][0], [transform, original_vector])
            suite.compare("IVertexTransform_zmulfv4", (matrix, position), expected, actual)


def check_vectors(suite, address_map=None):
    entries = {
        "IFVector4_setfv": (0x46BA50, 0x46BBEA, "ppi", False),
        "IFVector4_setxv": (0x46B920, 0x46BA48, "ppi", False),
        "IFVector4_tov3": (0x4678D0, 0x467C34, "pp", True),
        "IFVector4_dp4l": (0x464A90, 0x464D88, "pp", True),
        "IVector4_add": (0x46B670, 0x46B91C, "ppp", False),
        "IVector4_smul": (0x46B440, 0x46B66C, "ppi", False),
        "IVector3_normsub": (0x469CE0, 0x46A1B4, "ppp", True),
        "XNormal3_normalize": (0x468180, 0x46853C, "pp", False),
        "XNormal3_dot": (0x4680B0, 0x468178, "pp", True),
    }
    if address_map is not None:
        entries = {name: (*address_map[name], signature, returns)
                   for name, (_, _, signature, returns) in entries.items()}
    for name, (start, end, signature, returns) in entries.items():
        suite.register(name, start, end)
        function = getattr(suite.lib, name)
        function.argtypes = [ctypes.c_void_p if t == "p" else ctypes.c_int32 for t in signature]
        function.restype = ctypes.c_int32 if returns else None

    def output(name, arguments, size, **kwargs):
        return compare_output(suite, name, entries[name][0], arguments, size, **kwargs)

    def dot(name, a, b):
        expected = getattr(suite.lib, name)(host_buffer(a), host_buffer(b)) & MASK32
        suite.oracle.reset()
        pa, pb = suite.oracle.alloc(len(a), a), suite.oracle.alloc(len(b), b)
        actual = suite.oracle.call(entries[name][0], [pa, pb])
        suite.compare(name, (a.hex(), b.hex()), expected, actual)

    raw = [(0, 0x80000000, 1, 0x7FFFFFFF),
           (0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000),
           (0x7FC00000, 0xFFC00001, 0x007FFFFF, 0x807FFFFF)]
    raw.extend(tuple(suite.rng.getrandbits(32) for _ in range(4))
               for _ in range(min(suite.samples, 64)))
    for values in raw:
        packed = struct.pack("<4I", *values)
        for n in (1, 2, 3, 4):
            output("IFVector4_setfv", [packed, n], 20)
            output("IFVector4_setxv", [packed, n], 20)

    shared = [(0, 0, 0, 0, 0)]
    for exponent in (1, 2, 96, 126, 127, 128, 158, 253, 254):
        shared.extend(((exponent, 0x20000000, 0, -0x20000000, 0x10000000),
                       (exponent, -0x3FFFFFFF, 0x3FFFFFFF, 1, -1)))
    for _ in range(min(suite.samples, 64)):
        m = [suite.rng.randrange(-0x3FFFFFFF, 0x40000000) for _ in range(4)]
        m[0] = suite.rng.choice((-1, 1)) * suite.rng.randrange(0x20000000, 0x40000000)
        shared.append((suite.rng.randrange(1, 255), *m))
    for index, values in enumerate(shared):
        a = struct.pack("<I4i", *values)
        b = struct.pack("<I4i", *shared[(index + 1) % len(shared)])
        for right in (a, b):
            for alias in (None, 0, 1):
                output("IVector4_add", [a, right], 20, alias=alias)
            output("IVector3_normsub", [a[:16], right[:16]], 12, returns=True)
        for scalar in (0, 0x3F800000, 0xBFC00000):
            output("IVector4_smul", [a, scalar], 20, alias=0)
        normal = struct.pack("<3i", *values[1:4])
        for alias in (None, 0):
            output("XNormal3_normalize", [normal], 12, alias=alias)
        dot("XNormal3_dot", normal, b[4:16])

    vectors = [(0, 0, 0, 0), (1, -2, 3, 1), (0.25, -0.5, 2, -1),
               (0, 1, -1, 0), (1, -2, 3, 0.125), (2 ** -20, 2 ** 20, -1, 2),
               (1.75, 1.75, 1.75, 0.75), (-1.75, -1.75, -1.75, 0.75)]
    vectors.extend(tuple(suite.rng.randrange(-32, 33) / 8.0 for _ in range(4))
                   for _ in range(min(suite.samples, 64)))
    for index, values in enumerate(vectors):
        a = independent_vector(values)
        b = independent_vector(vectors[(index + 1) % len(vectors)])
        output("IFVector4_tov3", [a], 16, returns=True)
        dot("IFVector4_dp4l", a, b)


def check_scalars(suite, functions):
    for name, start, end, signature, result, family in functions:
        suite.register(name, start, end)
        native = getattr(suite.lib, name)
        native.argtypes = [TYPES[kind] for kind in signature]
        native.restype = TYPES[result]
        return64 = result in "qQ"
        mask = MASK64 if return64 else MASK32
        for args in scalar_cases(family, suite.rng, suite.samples):
            expected = native(*args) & mask
            suite.check(name, start, stack_words(signature, args), expected,
                        return64=return64)


def check_hc_evaluator(suite):
    """Exercise every HC arithmetic/branch opcode, including variable shifts."""
    arm = suite.arch == "arm"
    targets = (("emulator_evalALUOp", 0x2C978, 0x2CABC, range(23)),
               ("emulator_evalConditional", 0x2C89C, 0x2C978, range(41, 53))) if arm else (
               ("emulator_evalALUOp", 0x45D7C0, 0x45DA42, range(23)),
               ("emulator_evalConditional", 0x45D670, 0x45D769, range(41, 53)))
    matching_reference = suite.arm_reference == arm
    edges = word_edges(32)
    pairs = [(a, b) for a in edges for b in
             (0, 1, 31, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)]
    pairs.extend((suite.rng.getrandbits(32), suite.rng.getrandbits(32))
                 for _ in range(suite.samples))
    for name, start, end, ops in targets:
        suite.register(name, start, end)
        native = getattr(suite.lib, name)
        native.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32]
        native.restype = ctypes.c_uint32
        for op in ops:
            if name == "emulator_evalALUOp" and op in (2, 5):  # ASD / LSD
                shifts = list(range(-31, 32))
                if matching_reference:
                    shifts += [-0x80000000, -513, -256, -255, -129, -128,
                               -127, -64, -33, -32, 32, 33, 64, 127, 128,
                               255, 256, 257, 511, 512, 0x7FFFFFFF]
                inputs = [(a, b & MASK32) for a in
                          (0, 1, 0x80000000, 0x80000001, 0x7FFFFFFF, MASK32)
                          for b in shifts]
            elif name == "emulator_evalALUOp" and op in (3, 6, 7, 18, 19):
                # These opcodes explicitly assert counts below 32 in PE.
                inputs = [(a, b) for a in edges for b in range(32)]
            else:
                inputs = pairs
            for a, b in inputs:
                suite.check(name, start, [op, a, b], native(op, a, b))


def check_hc_substitution(suite):
    """Compare folded instruction words, including immediate/register aliases."""
    class Instr(ctypes.Structure):
        _fields_ = [("instr0", ctypes.c_uint32), ("instr1", ctypes.c_uint32)]

    name = "emulator_substitute"
    arm = suite.arch == "arm"
    start, end = (0x2CABC, 0x2CE48) if arm else (0x45DA50, 0x45DE58)
    suite.register(name, start, end)
    native = getattr(suite.lib, name)
    native.argtypes = [ctypes.POINTER(Instr), ctypes.c_int, ctypes.c_int]
    native.restype = Instr
    cases = []
    for op in list(range(14)) + list(range(17, 23)):
        for src0, src1, immediate, reg in ((3, 4, False, 3), (2, 3, False, 3),
                                         (3, 3, False, 3), (2, 4, False, 3),
                                         (3, 3, True, 3), (2, 3, True, 3),
                                         (2, 4, True, 3)):
            cases.append((op, src0, src1, immediate, reg, 5))
    cases.append((20, 2, 0x803, True, 3, 11))
    for op in (14, 15):
        for src0, immediate, reg in ((3, False, 3), (2, False, 3), (0, True, 3)):
            cases.append((op, src0, 0x80000000 if immediate else 0,
                          immediate, reg, 0x80000000))
    for op in range(23, 39):
        for reg in (2, 3, 4):
            cases.append((op, 2, 3, False, reg, 0x7FFFFFFF))
        cases.append((op, 2, 3, True, 3, 0x80000000))
    for op in range(41, 53):
        for reg in (2, 3, 4):
            cases.append((op, 2, 3, False, reg, 5))
        for value in (0, 5, 0xFFFFFFFF):
            cases.append((op, 2, 3, True, 2, value))

    oracle = suite.oracle
    guard = b"\xA5" * 8
    for op, src0, src1, immediate, reg, value in cases:
        instr = Instr(op | (1 << 10) | (src0 << 21) | (int(immediate) << 8), src1)
        before = bytes(instr)
        expected = native(ctypes.byref(instr), reg, ctypes.c_int(value).value)
        oracle.reset()
        source = oracle.alloc(24, guard + before + guard)
        oracle.watch(source, 24, "source instruction and guards")
        if arm:
            destination = oracle.alloc(24, guard + bytes(8) + guard)
            oracle.watch(destination, 24, "folded instruction and guards")
            oracle.call(start, [destination + 8, source + 8, reg, value])
            actual = oracle.readback(destination, 24)
            output = guard + bytes(expected) + guard
        else:
            actual = struct.pack("<Q", oracle.call(start, [source + 8, reg, value], return64=True))
            output = bytes(expected)
        original_source = oracle.readback(source, 24)
        suite.compare(name, (op, src0, src1, immediate, reg, value),
                      (output, guard + bytes(instr) + guard, before),
                      (actual, original_source, bytes(instr)))


def run(suite):
    check_hc_evaluator(suite)
    check_hc_substitution(suite)
    if suite.arch == "arm":
        from differential_arm_numeric import run as run_arm
        return run_arm(suite)
    check_scalars(suite, FUNCTIONS)
    check_vector_length(suite)
    check_matrices(suite)
    check_vectors(suite)
