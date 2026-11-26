/* hcOpcode.c - HC instruction encoding (hcOpcode.h / hcOpcode.c).
 *
 * The first 55 opcode entries are transcribed from the reference DLL;
 * later entries describe internal ARM lowering operations. Each byte encodes:
 *   bits 0-2: argument count
 *   bits 3-4: type (0=ALUOP, 1=MEMOP, 2=BRANCH)
 *   bit 5: commutative
 *   bits 6-7: dst flags
 */
#include "gerbera/hc/hcOpcode.h"
#include "gerbera/grState.h"

const unsigned char HC_opcodeDecls[HCOP_MAX] =
{
    '#',    '#',    '\x03', '\x03', '\x03', '\x03', '\x03', '\x03',
    '#',    '#',    '#',    '#',    '#',    '#',    '\"',   '\"',
    '\xE0', '#',    '\x03', '\x03', '\x03', '\x03', '#',    '+',
    '+',    '+',    '+',    '\v',   '\v',   '+',    '\v',   'k',
    'k',    'K',    'k',    'K',    'K',    'K',    'K',    '\xD0',
    '\x91', '\xB3', '\xB3', '\x93', '\x93', '\x93', '\x93', '\x93',
    '\x93', '\x93', '\x93', '\xB3', '\xB3', '\xD0', '\xD1',
    '#', '\x03', '\x03', '\x03', '\x03', '\x03', /* internal packed-color ops */
    '\x03', '\x03', '\x03', '\x03', '\x03'
};

static const unsigned char indexSize[10] =
{
    0x1B, 2, /* HCOP_LW2S */
    0x1C, 2, /* HCOP_LW2U */
    0x1E, 4, /* HCOP_LD4  */
    0x21, 2, /* HCOP_SW2  */
    0x23, 4  /* HCOP_SD4  */
};

/* --- assert --------------------------------------------------------------- */

void (HC_assertFail)(const char *expr, const char *file, int line, const char *msg)
{
    (void)msg;
    grAssert(expr, file, line);
}

/* --- instruction generation (hcOpcode.c) ---------------------------------- */

static HCInstr_s genOp3Internal(int op, int dst, int src0, int src1)
{
    HCInstr_s result;

    if (op >= 256)
        HC_assertFail("op < (1<<HCOP_OPER_SIZE)", "hcOpcode.c", 202, NULL);
    if (dst < 0 || dst >= 2048)
        HC_assertFail("dst >= 0 && dst < (1<<HCOP_DST_SIZE)", "hcOpcode.c", 203, NULL);
    if (src0 < 0 || src0 >= 2048)
        HC_assertFail("src0 >= 0 && src0 < (1<<HCREG_SIZE)", "hcOpcode.c", 204, NULL);
    if (src1 < 0 || src1 >= 2048)
        HC_assertFail("src1 >= 0 && src1 < (1<<HCREG_SIZE)", "hcOpcode.c", 205, NULL);
    result.instr0 = (unsigned int)op | ((unsigned int)dst << 10) | ((unsigned int)src0 << 21);
    result.instr1 = (unsigned int)src1;
    return result;
}

static HCInstr_s genOp3ImmInternal(int op, int dst, int src0, int imm)
{
    HCInstr_s result;

    if (op >= HCOP_MAX)
        HC_assertFail("op < HCOP_MAX", "hcOpcode.c", 218, NULL);
    if (dst < 0 || dst >= 2048)
        HC_assertFail("dst >= 0 && dst < (1<<HCOP_DST_SIZE)", "hcOpcode.c", 219, NULL);
    if (src0 < 0 || src0 >= 2048)
        HC_assertFail("src0 >= 0 && src0 < (1<<HCREG_SIZE)", "hcOpcode.c", 220, NULL);
    result.instr0 = (unsigned int)op | 0x100u | ((unsigned int)dst << 10) | ((unsigned int)src0 << 21);
    result.instr1 = (unsigned int)imm;
    return result;
}

HCInstr_s HC_genOp0(int op)
{
    HCInstr_s result;

    if (op >= 256)
        HC_assertFail("op < (1<<HCOP_OPER_SIZE)", "hcOpcode.h", 319, NULL);
    result.instr0 = (unsigned int)op;
    result.instr1 = 0;
    return result;
}

HCInstr_s HC_genOp3Raw(int op, int dst, int src0, int src1)
{
    return genOp3Internal(op, dst, src0, src1);
}

HCInstr_s HC_genOp3ImmRaw(int op, int dst, int src0, int imm)
{
    return genOp3ImmInternal(op, dst, src0, imm);
}

HCInstr_s HC_genOp3(int op, int dst, int src0, int src1)
{
    if (HC_getOpcodeArgCountOp(op) != 3)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 3", "hcOpcode.c", 245, NULL);
    return genOp3Internal(op, dst, src0, src1);
}

HCInstr_s HC_genOp3Imm(int op, int dst, int src0, int imm)
{
    if (HC_getOpcodeArgCountOp(op) != 3)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 3", "hcOpcode.c", 251, NULL);
    return genOp3ImmInternal(op, dst, src0, imm);
}

HCInstr_s HC_genOp2(int op, int dst, int src0)
{
    if (HC_getOpcodeArgCountOp(op) != 2)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 2", "hcOpcode.c", 257, NULL);
    return genOp3Internal(op, dst, src0, 0);
}

HCInstr_s HC_genOp2Imm(int op, int dst, int imm)
{
    if (HC_getOpcodeArgCountOp(op) != 2)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 2", "hcOpcode.c", 263, NULL);
    return genOp3ImmInternal(op, dst, 0, imm);
}

HCInstr_s HC_genOp1Imm(int op, int imm)
{
    if (HC_getOpcodeArgCountOp(op) != 1)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 1", "hcOpcode.c", 269, NULL);
    return genOp3ImmInternal(op, 0, 0, imm);
}

HCInstr_s HC_genOp1(int op, int dst)
{
    if (HC_getOpcodeArgCountOp(op) != 1)
        HC_assertFail("HC_getOpcodeArgCountOp(op) == 1", "hcOpcode.c", 275, NULL);
    return genOp3ImmInternal(op, dst, 0, 0);
}

/* --- opcode accessors (hcOpcode.h) ---------------------------------------- */

int HC_getOpcodeOper(HCInstr_s *instr)
{
    return (unsigned char)instr->instr0;
}

int HC_getOpcodeArgCountOp(int op)
{
    return HC_opcodeDecls[op] & 7;
}

int HC_getOpcodeArgCount(HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);

    return HC_getOpcodeArgCountOp(op);
}

int HC_getOpcodeDst(HCInstr_s *instr)
{
    if (HC_getOpcodeArgCount(instr) < 1)
        HC_assertFail("HC_getOpcodeArgCount(instr) >= 1", "hcOpcode.h", 266, NULL);
    return (instr->instr0 >> 10) & 0x7FF;
}

int HC_getOpcodeSrc0Raw(HCInstr_s *instr)
{
    return instr->instr0 >> 21;
}

int HC_getOpcodeSrc0(HCInstr_s *instr)
{
    if (HC_getOpcodeArgCount(instr) < 2)
        HC_assertFail("HC_getOpcodeArgCount(instr) >= 2", "hcOpcode.h", 278, NULL);
    return HC_getOpcodeSrc0Raw(instr);
}

int HC_getOpcodeSrc1Raw(HCInstr_s *instr)
{
    return instr->instr1 & 0x7FF;
}

int HC_getOpcodeSrc1(HCInstr_s *instr)
{
    if (HC_getOpcodeArgCount(instr) < 3)
        HC_assertFail("HC_getOpcodeArgCount(instr) >= 3", "hcOpcode.h", 289, NULL);
    return HC_getOpcodeSrc1Raw(instr);
}

int HC_isStoreOp(int op)
{
    return (unsigned int)(op - 31) <= 7;
}

int HC_getOpcodeTypeOp(int op)
{
    if ((unsigned int)op >= HCOP_MAX)
        HC_assertFail("(unsigned int)op < HCOP_MAX", "hcOpcode.h", 351, NULL);
    return ((int)(unsigned char)HC_opcodeDecls[op] >> 3) & 3;
}

int HC_getOpcodeFlags(HCInstr_s *instr)
{
    return (instr->instr0 >> 8) & 3;
}

int HC_opcodeHasImm(HCInstr_s *instr)
{
    return (HC_getOpcodeFlags(instr) & 1) != 0;
}

int HC_opcodeIsCommutative(HCInstr_s *instr)
{
    return (((int)(unsigned char)HC_opcodeDecls[HC_getOpcodeOper(instr)] >> 5) & 1) != 0;
}

int HC_getOpcodeImm(HCInstr_s *instr)
{
    if (HC_opcodeHasImm(instr) == 0)
        HC_assertFail("HC_opcodeHasImm(instr)", "hcOpcode.h", 406, NULL);
    if (HC_getOpcodeArgCount(instr) >= 4)
        HC_assertFail("HC_getOpcodeArgCount(instr) < 4", "hcOpcode.h", 407, NULL);
    return instr->instr1;
}

int HC_getOpcodeLabel(HCInstr_s *instr)
{
    if (HC_getOpcodeOper(instr) != HCOP_LABEL)
        HC_assertFail("HC_getOpcodeOper(instr) == HCOP_LABEL", "hcOpcode.h", 420, NULL);
    return HC_getOpcodeImm(instr);
}

void HC_setOpcodeSrc1(HCInstr_s *instr, int v)
{
    if (v < 0 || v >= 2048)
        HC_assertFail("v >= 0 && v < HC_MAX_REG_INDEX", "hcOpcode.h", 257, NULL);
    instr->instr1 = (unsigned int)v;
}

int HC_getOpcodeType(HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);

    return HC_getOpcodeTypeOp(op);
}

int HC_isLoadOp(int op)
{
    if (op >= HCOP_LBS)
        return op <= HCOP_LD4;
    return 0;
}

int HC_isCondBranchOp(int op)
{
    return (unsigned int)(op - 41) <= 0xB;
}

int HC_isBranch(HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);

    return HC_isBranchOp(op);
}

void HC_setOpcodeImm(HCInstr_s *instr, int v)
{
    if (HC_getOpcodeArgCount(instr) > 3)
        HC_assertFail("HC_getOpcodeArgCount(instr) <= 3", "hcOpcode.h", 413, NULL);
    if (HC_opcodeHasImm(instr) == 0)
        HC_assertFail("HC_opcodeHasImm(instr)", "hcOpcode.h", 414, NULL);
    instr->instr1 = (unsigned int)v;
}

int HC_opcodeDstFlagsOp(int op)
{
    if (op < 0 || op >= HCOP_MAX)
        HC_assertFail("(int)op >= 0 && (int)op < HCOP_MAX", "hcOpcode.h", 384, NULL);
    return (int)(unsigned char)HC_opcodeDecls[op] >> 6;
}

void HC_setOpcodeDst(HCInstr_s *instr, int v)
{
    if (HC_getOpcodeArgCount(instr) < 1)
        HC_assertFail("HC_getOpcodeArgCount(instr) >= 1", "hcOpcode.c", 124, NULL);
    if (v >= 2048)
        HC_assertFail("v < HC_MAX_REG_INDEX", "hcOpcode.c", 125, NULL);
    instr->instr0 = (instr->instr0 & 0xFFE003FFu) | ((unsigned int)v << 10);
}

void HC_setOpcodeNoImm(HCInstr_s *instr)
{
    instr->instr0 &= ~0x100u;
}

void HC_setOpcodeSrc0(HCInstr_s *instr, int v)
{
    if (HC_getOpcodeArgCount(instr) < 1)
        HC_assertFail("HC_getOpcodeArgCount(instr) >= 1", "hcOpcode.c", 131, NULL);
    if (v >= 2048)
        HC_assertFail("v < HC_MAX_REG_INDEX", "hcOpcode.c", 132, NULL);
    instr->instr0 = (instr->instr0 & 0x001FFFFFu) | ((unsigned int)v << 21);
}

int HC_isBranchOp(int op)
{
    return (unsigned int)(op - 40) <= 0xC;
}

int HC_getMemOpIndexSizeOp(int op)
{
    int i;

    for (i = 0; i < 10; i += 2)
    {
        if (indexSize[i] == (unsigned char)op)
            return (int)indexSize[i + 1];
    }
    return 1;
}

int HC_getMemOpIndexSize(HCInstr_s *instr)
{
    int op;

    if (HC_getOpcodeType(instr) != HCOP_TYPE_MEMOP)
        HC_assertFail("HC_getOpcodeType(instr) == HCOP_TYPE_MEMOP", "hcOpcode.c", 297, NULL);
    op = HC_getOpcodeOper(instr);
    return HC_getMemOpIndexSizeOp(op);
}

int HC_getDef(HCInstr_s *instr)
{
    int op;

    op = HC_getOpcodeOper(instr);
    if (HC_opcodeDstFlagsOp(op) != HCOP_DST_NORMAL)
        return -1;
    else
        return HC_getOpcodeDst(instr);
}

int HC_getRegUses(int *dst, HCInstr_s *instr)
{
    int t;
    int argCount;
    int dstCount;
    int op;

    op = HC_getOpcodeOper(instr);
    argCount = HC_getOpcodeArgCountOp(op);
    if (argCount < 2)
        return 0;
    dstCount = 2;
    if (HC_opcodeDstFlagsOp(op) == HCOP_DST_AS_SRC)
    {
        int d = HC_getOpcodeDst(instr);
        *dst++ = d;
        dstCount = 3;
    }
    dst[0] = HC_getOpcodeSrc0Raw(instr);
    dst[1] = HC_getOpcodeSrc1Raw(instr);
    t = HC_opcodeHasImm(instr) != 0;
    if (argCount == 2)
        ++t;
    if (t != (argCount == 2) + HC_opcodeHasImm(instr))
        HC_assertFail("t == (HC_opcodeHasImm(instr) + (argCount == 2))", "hcOpcode.c", 333, NULL);
    return dstCount - (3 - argCount + HC_opcodeHasImm(instr));
}

int HC_getUses(HCInstr_s *instr, int *uses)
{
    int result;
    int c;
    int b;
    int cnt;

    switch (HC_getRegUses(uses, instr))
    {
    case 0:
        result = 0;
        break;
    case 1:
        result = 1;
        break;
    case 2:
        if (uses[0] == uses[1])
            result = 1;
        else
            result = 2;
        break;
    case 3:
        cnt = 1;
        b = uses[1];
        c = uses[2];
        if (b != uses[0])
            cnt = 2;
        if (c != uses[0] && c != b)
        {
            uses[cnt] = c;
            ++cnt;
        }
        result = cnt;
        break;
    default:
        HC_assertFail("HC_FALSE", "hcOpcode.c", 195, NULL);
        result = 0;
        break;
    }
    return result;
}

void HC_setUse(HCInstr_s *instr, int which, int newVal)
{
    int op;
    int w;

    w = which;
    op = HC_getOpcodeOper(instr);
    if (which >= HC_getOpcodeArgCountOp(op))
        HC_assertFail("which < argCount", "hcOpcode.c", 345, NULL);
    if (HC_opcodeDstFlagsOp(op) != HCOP_DST_AS_SRC)
        w = which + 1;
    if (w != 0)
    {
        if (w == 1)
        {
            HC_setOpcodeSrc0(instr, newVal);
        }
        else
        {
            if (w != 2)
                HC_assertFail("w == 2", "hcOpcode.c", 359, NULL);
            HC_setOpcodeSrc1(instr, newVal);
        }
    }
    else
    {
        HC_setOpcodeDst(instr, newVal);
    }
}

int HC_bsf(unsigned int x)
{
    int bits;

    bits = 0;
    if (x == 0)
        HC_assertFail("x != 0", "hcDefs.c", 64, NULL);
    if ((x << 16) == 0)
    {
        x >>= 16;
        bits = 16;
    }
    if ((x << 24) == 0)
    {
        x >>= 8;
        bits += 8;
    }
    if ((x << 28) == 0)
    {
        x >>= 4;
        bits += 4;
    }
    if ((x << 30) == 0)
    {
        x >>= 2;
        bits += 2;
    }
    return ((x & 1u) == 0) + bits;
}

int HC_isAbsBranchOp(int op)
{
    return op == HCOP_BRA;
}

int HC_isAbsBranch(HCInstr_s *instr)
{
    return HC_isAbsBranchOp(HC_getOpcodeOper(instr));
}

int HC_min(int a, int b)
{
    return (a > b) ? b : a;
}

