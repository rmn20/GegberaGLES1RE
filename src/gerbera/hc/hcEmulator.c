/* hcEmulator.c - HC IR constant-evaluation & operand-substitution helpers.
 *
 * Ported from docs/libGLES_CM.dll.c:52028-52305 with static tables verified
 * directly from docs/libGLES_CM.dll (`nonComm` at VA 0x47F724, `ops` at
 * VA 0x47F72E) and cross-checked against ARM sub_2C89C / sub_2C978 / sub_2CABC
 * in docs/libgles_cm.dll.epoc.c:36465-36598.
 */
#include "gerbera/hc/hcEmulator.h"
#include "hg/hgDefs.h"

#include <stddef.h>

/* VA 0x47F724: scaled load/store -> unscaled byte-offset load/store pairs
 * ((unscaledOp << 8) | scaledOp):
 *   LW2S(27)->LWS(25), LW2U(28)->LWU(26), LD4(30)->LD(29),
 *   SW2(33)->SW(32),   SD4(35)->SD(34). */
static const unsigned short nonComm[5] =
{
    (unsigned short)((HCOP_LWS << 8) | HCOP_LW2S),
    (unsigned short)((HCOP_LWU << 8) | HCOP_LW2U),
    (unsigned short)((HCOP_LD  << 8) | HCOP_LD4),
    (unsigned short)((HCOP_SW  << 8) | HCOP_SW2),
    (unsigned short)((HCOP_SD  << 8) | HCOP_SD4)
};

/* VA 0x47F72E: conditional branch operand-swap table ((reversedOp << 8) | op):
 *   CBA->CBA, CBEQ->CBEQ, CBGES->CBLES, CBGEU->CBLEU, CBGTS->CBLTS,
 *   CBGTU->CBLTU, CBLES->CBGES, CBLEU->CBGEU, CBLTS->CBGTS, CBLTU->CBGTU,
 *   CBNA->CBNA, CBNEQ->CBNEQ. */
static const unsigned short ops[12] =
{
    (unsigned short)((HCOP_CBA   << 8) | HCOP_CBA),
    (unsigned short)((HCOP_CBEQ  << 8) | HCOP_CBEQ),
    (unsigned short)((HCOP_CBLES << 8) | HCOP_CBGES),
    (unsigned short)((HCOP_CBLEU << 8) | HCOP_CBGEU),
    (unsigned short)((HCOP_CBLTS << 8) | HCOP_CBGTS),
    (unsigned short)((HCOP_CBLTU << 8) | HCOP_CBGTU),
    (unsigned short)((HCOP_CBGES << 8) | HCOP_CBLES),
    (unsigned short)((HCOP_CBGEU << 8) | HCOP_CBLEU),
    (unsigned short)((HCOP_CBGTS << 8) | HCOP_CBLTS),
    (unsigned short)((HCOP_CBGTU << 8) | HCOP_CBLTU),
    (unsigned short)((HCOP_CBNA  << 8) | HCOP_CBNA),
    (unsigned short)((HCOP_CBNEQ << 8) | HCOP_CBNEQ)
};

static unsigned int emu_rol32(unsigned int x, unsigned int s)
{
    s &= 31u;
    if (s == 0)
        return x;
    return (x << s) | (x >> (32u - s));
}

static unsigned int emu_ror32(unsigned int x, unsigned int s)
{
    s &= 31u;
    if (s == 0)
        return x;
    return (x >> s) | (x << (32u - s));
}

static unsigned int asr(int s0, int s)
{
    return (unsigned int)(s0 >> s);
}

static unsigned int asd(int s0, int s)
{
    unsigned int count = s < 0 ? 0u - (unsigned int)s : (unsigned int)s;
#if GERBERA_ARM_REFERENCE
    /* ARM register shifts use the low byte and saturate at 32; the PE
     * evaluator masks the count to five bits. Avoid C's oversized shifts. */
    count &= 255u;
    if (count >= 32u)
        return s < 0 ? (unsigned int)(s0 >> 31) : 0u;
#else
    count &= 31u;
#endif
    return s < 0 ? (unsigned int)(s0 >> count) : (unsigned int)s0 << count;
}

static unsigned int lsd(unsigned int s0, int s)
{
    unsigned int count = s < 0 ? 0u - (unsigned int)s : (unsigned int)s;
#if GERBERA_ARM_REFERENCE
    count &= 255u;
    if (count >= 32u)
        return 0u;
#else
    count &= 31u;
#endif
    return s < 0 ? s0 >> count : s0 << count;
}

int emulator_evalConditional(HCOP_e op, unsigned int s0, unsigned int s1)
{
    switch (op)
    {
    case HCOP_CBA:   return (s1 & s0) != 0;
    case HCOP_CBEQ:  return s0 == s1;
    case HCOP_CBGES: return (int)s0 >= (int)s1;
    case HCOP_CBGEU: return s0 >= s1;
    case HCOP_CBGTS: return (int)s0 > (int)s1;
    case HCOP_CBGTU: return s0 > s1;
    case HCOP_CBLES: return (int)s0 <= (int)s1;
    case HCOP_CBLEU: return s0 <= s1;
    case HCOP_CBLTS: return (int)s0 < (int)s1;
    case HCOP_CBLTU: return s0 < s1;
    case HCOP_CBNA:  return (s1 & s0) == 0;
    case HCOP_CBNEQ: return s0 != s1;
    default:
        HC_assertFail("HC_FALSE", "hcEmulator.c", 89, NULL);
        return 0;
    }
}

int emulator_evalALUOp(HCOP_e op, unsigned int s0, unsigned int s1)
{
    int res = 0;

    switch (op)
    {
    case HCOP_ADD:
        res = (int)(s1 + s0);
        break;
    case HCOP_AND:
        res = (int)(s1 & s0);
        break;
    case HCOP_ASD:
        res = (int)asd((int)s0, (int)s1);
        break;
    case HCOP_ASR:
        if (s1 >= 32u)
            HC_assertFail("s1 < 32", "hcEmulator.c", 125, NULL);
        res = (int)asr((int)s0, (int)s1);
        break;
    case HCOP_BIC:
        res = (int)(s0 & ~s1);
        break;
    case HCOP_LSD:
        res = (int)lsd(s0, (int)s1);
        break;
    case HCOP_LSL:
        if (s1 >= 32u)
            HC_assertFail("s1 < 32", "hcEmulator.c", 128, NULL);
        res = (int)(s0 << s1);
        break;
    case HCOP_LSR:
        if (s1 >= 32u)
            HC_assertFail("s1 < 32", "hcEmulator.c", 129, NULL);
        res = (int)(s0 >> s1);
        break;
    case HCOP_UQADD8:
    case HCOP_UQSUB8:
    case HCOP_PACKED_ALPHA_A:
    case HCOP_PACKED_ALPHA_B:
    case HCOP_UQADD8_RGB:
    case HCOP_UQSUB8_RGB:
    case HCOP_PACKED_ALPHA_RGB_A:
    case HCOP_PACKED_ALPHA_RGB_B:
    {
        unsigned int shift, result = 0;
        unsigned int alpha = (op == HCOP_PACKED_ALPHA_A || op == HCOP_PACKED_ALPHA_RGB_A ? s0 : s1) >> 24;
        unsigned int bits = op >= HCOP_UQADD8_RGB ? 24u : 32u;
        for (shift = 0; shift < bits; shift += 8)
        {
            unsigned int a = (s0 >> shift) & 255u, b = (s1 >> shift) & 255u;
            unsigned int v;
            if (op == HCOP_UQADD8 || op == HCOP_UQADD8_RGB) v = a + b < 255u ? a + b : 255u;
            else if (op == HCOP_UQSUB8 || op == HCOP_UQSUB8_RGB) v = a > b ? a - b : 0;
            else v = ((a * (alpha + 1)) >> 8) + ((b * (256 - alpha)) >> 8);
            result |= v << shift;
        }
        res = (int)result;
        break;
    }
    case HCOP_UXTB_ROR:
        res = (int)(emu_ror32(s0, s1) & 255u);
        break;
    case HCOP_UXTB16:
        res = (int)(emu_ror32(s0, s1) & 0x00FF00FFu);
        break;
    case HCOP_UXTAB16_ROR8:
    {
        unsigned int pair = (s1 >> 8) & 0x00FF00FFu;
        res = (int)(((s0 + pair) & 0xFFFFu) | ((s0 & 0xFFFF0000u) + (pair & 0xFFFF0000u)));
        break;
    }
    case HCOP_MAXS:
        res = ((int)s0 <= (int)s1) ? (int)s1 : (int)s0;
        break;
    case HCOP_MAXU:
        res = (s0 <= s1) ? (int)s1 : (int)s0;
        break;
    case HCOP_MINS:
        res = ((int)s0 >= (int)s1) ? (int)s1 : (int)s0;
        break;
    case HCOP_MINU:
        res = (s0 >= s1) ? (int)s1 : (int)s0;
        break;
    case HCOP_MULS:
    case HCOP_MULU:
        res = (int)(s1 * s0);
        break;
    case HCOP_MOV:
        res = (int)s0;
        break;
    case HCOP_MOVN:
        res = (int)(~s0);
        break;
    case HCOP_NOP:
        return res;
    case HCOP_OR:
        res = (int)(s1 | s0);
        break;
    case HCOP_ROL:
        if (s1 >= 32u)
            HC_assertFail("s1 < 32", "hcEmulator.c", 140, NULL);
        res = (int)emu_rol32(s0, s1);
        break;
    case HCOP_ROR:
        if (s1 >= 32u)
            HC_assertFail("s1 < 32", "hcEmulator.c", 141, NULL);
        res = (int)emu_ror32(s0, s1);
        break;
    case HCOP_SUB:
        res = (int)(s0 - s1);
        break;
    case HCOP_SUBR:
        res = (int)(s1 - s0);
        break;
    case HCOP_XOR:
        res = (int)(s1 ^ s0);
        break;
    default:
        HG_ASSERT_AT("hcEmulator.c", 146, 0);
        break;
    }
    return res;
}

HCInstr_s emulator_substitute(HCInstr_s *instr, int cReg, int cVal)
{
    HCOP_e op = HC_getOpcodeOper(instr);
    int dst = HC_getOpcodeDst(instr);
    int s0 = HC_getOpcodeSrc0(instr);
    int hasImm = HC_opcodeHasImm(instr);
    int argCount = HC_getOpcodeArgCountOp(op);

    if (HC_getOpcodeType(instr) == HCOP_TYPE_ALUOP)
    {
        if (argCount == 3)
        {
            if (hasImm != 0 && s0 == cReg)
            {
                unsigned int imm = (unsigned int)HC_getOpcodeImm(instr);
                int val = emulator_evalALUOp(op, (unsigned int)cVal, imm);
                return HC_genOp2Imm(HCOP_MOV, dst, val);
            }
            /* The original also compares the low 11 bits of an immediate
             * against cReg here (ARM sub_2CABC, PE 0x45DA50).
             * The remaining three-operand cases do not test hasImm. */
            if (s0 == cReg && cReg == HC_getOpcodeSrc1(instr))
            {
                int val = emulator_evalALUOp(op, (unsigned int)cVal, (unsigned int)cVal);
                return HC_genOp2Imm(HCOP_MOV, dst, val);
            }
            if (cReg == HC_getOpcodeSrc1(instr))
                return HC_genOp3Imm(op, dst, s0, cVal);
            if (s0 == cReg)
            {
                int s1 = HC_getOpcodeSrc1(instr);
                if (HC_opcodeIsCommutative(instr) != 0)
                    return HC_genOp3Imm(op, dst, s1, cVal);
                if (op == HCOP_SUB)
                    return HC_genOp3Imm(HCOP_SUBR, dst, s1, cVal);
                if (op == HCOP_SUBR)
                    return HC_genOp3Imm(HCOP_SUB, dst, s1, cVal);
            }
        }
        else if (argCount == 2)
        {
            if (hasImm != 0)
            {
                unsigned int imm = (unsigned int)HC_getOpcodeImm(instr);
                int val = emulator_evalALUOp(op, imm, 0xFFFFFFFFu);
                return HC_genOp2Imm(HCOP_MOV, dst, val);
            }
            if (s0 == cReg)
            {
                int val = emulator_evalALUOp(op, (unsigned int)cVal, 0xFFFFFFFFu);
                return HC_genOp2Imm(HCOP_MOV, dst, val);
            }
        }
        return *instr;
    }

    if (HC_isLoadOp(op) != 0 || HC_isStoreOp(op) != 0)
    {
        if (hasImm == 0)
        {
            int s1 = HC_getOpcodeSrc1(instr);
            int idxSize = HC_getMemOpIndexSize(instr);
            int isNonComm = 0;
            int i;

            for (i = 0; i < 5; ++i)
            {
                int scaledOp = nonComm[i] & 0xFF;
                if ((int)op == scaledOp)
                {
                    if (s1 == cReg)
                    {
                        return HC_genOp3Imm((HCOP_e)(nonComm[i] >> 8),
                                            dst, s0, (int)((unsigned int)idxSize
                                                          * (unsigned int)cVal));
                    }
                    isNonComm = 1;
                }
            }
            if (isNonComm == 0)
            {
                if (s0 == cReg)
                    return HC_genOp3Imm(op, dst, s1, cVal);
                if (cReg == HC_getOpcodeSrc1(instr))
                    return HC_genOp3Imm(op, dst, s0, cVal);
            }
        }
        return *instr;
    }

    if (HC_isCondBranchOp(op) == 0)
        return *instr;

    if (hasImm == 0)
    {
        int s1 = HC_getOpcodeSrc1(instr);
        int i;

        for (i = 0; i < 12; ++i)
        {
            /* Both references use the first table entry whenever s0 == cReg,
             * regardless of the opcode (x86 0x45DD95, ARM 0x2CABC).
             * This is likely a missing opcode comparison in the original. */
            int tblOp = ops[i] & 0xFF;
            (void)tblOp;
            if (s0 == cReg)
                return HC_genOp3Imm((HCOP_e)(ops[i] >> 8), dst, s1, cVal);
            if (s1 == cReg)
                return HC_genOp3Imm(op, dst, s0, cVal);
        }
        return *instr;
    }

    if (s0 != cReg)
        return *instr;
    if (emulator_evalConditional(op, (unsigned int)cVal, (unsigned int)HC_getOpcodeImm(instr)) == 0)
        return HC_genOp0(HCOP_NOP);
    return HC_genOp1(HCOP_BRA, HC_getOpcodeDst(instr));
}
