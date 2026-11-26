/* hcARMBackend.c - 32-bit ARM (A32) JIT backend (hcARMBackend.c of original).
 *
 * ARM machine-code generator, pre-regalloc fixer,
 * immediate recognizer (rotated imm8 + multi-chunk MOV/ORR & MVN/BIC
 * synthesis), barrel-shifter operand fusion, CPSR flag-setting CMP/TST
 * elision, and backendDecl vtable. ARMv6 targets additionally use multiply-add,
 * media instructions and the AAPCS callee-save mask.
 * The ARMv5 implementation is recovered from docs/libgles_cm.dll.epoc.c
 * (sub_23B4C..sub_25484) and image bytes in
 * docs/libgles_cm.dll.epoc (VA 0x36EB8..0x36F9B, file = VA - 0x7F64).
 */
#include "gerbera/hc/hcBackend.h"
#include "gerbera/hc/hcBackendInfo.h"
#include "gerbera/hc/hcMemory.h"
#include "gerbera/hc/hcOpcode.h"

#include <stddef.h>
#include <string.h>

/* --- static tables (verified directly from docs/libgles_cm.dll.epoc) ------ */

/* VA 0x36EB8: ARM condition codes (LT=11, CC=3, GT=12, HI=8) for
 * HCOP_MAXS, HCOP_MAXU, HCOP_MINS, HCOP_MINU. */
static const unsigned char armMinMaxCond[4] = { 11, 3, 12, 8 };

/* VA 0x36EBC: per-opcode metadata word (first 55 entries):
 *   bits 0..4   (w & 0x1F)        : ARM data-processing opcode
 *   bits 5..7   ((w >> 5) & 7)    : immediate class for canCreateImmediate
 *   bits 8..9   ((w >> 8) & 3)    : CPSR flag class (0=none, 1=NZ, 2=NZCV)
 *   bit  10     ((w >> 10) & 1)   : barrel-shifter / 0xFFFF-mask ALU op
 *   bits 11..14 ((w >> 11) & 0xF) : codegen switch dispatch class (1..12) */
static const unsigned short armOpcodeInfo[HCOP_MAX] =
{
    0x1624u, 0x1520u, 0x195Fu, 0x195Fu, 0x152Eu, 0x195Fu, 0x195Fu, 0x195Fu,
    0x283Fu, 0x283Fu, 0x283Fu, 0x283Fu, 0x207Fu, 0x207Fu, 0x090Du, 0x090Fu,
    0x48DFu, 0x152Cu, 0x195Fu, 0x195Fu, 0x1622u, 0x1623u, 0x1521u, 0x409Fu,
    0x40BFu, 0x409Fu, 0x409Fu, 0x00FFu, 0x00FFu, 0x40BFu, 0x40FFu, 0x40BFu,
    0x409Fu, 0x00FFu, 0x40BFu, 0x40FFu, 0x40FFu, 0x40FFu, 0x40FFu, 0x50DFu,
    0x30DFu, 0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu,
    0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu, 0x383Fu, 0x58DFu, 0x60DFu,
    0x687Fu, 0x687Fu, 0x68DFu, 0x687Fu, 0x68DFu
};

/* VA 0x36F2A: maps HC hard register 0..13 -> ARM register r0..r12, lr(r14). */
static const unsigned char armRegRemap[14] =
{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14
};

/* VA 0x36F38: S-bit auto-enable table indexed by ARM DP opcode (TST..CMN=1). */
static const unsigned char armAutoSetFlags[16] =
{
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 2, 0, 2
};

/* VA 0x36F48: preRegallocFixer scaled-memop lowering for op 27..35
 * (bits 6..7 = index shift 0/1/2, bits 0..5 = unscaled HCOP). */
static const unsigned char armMemScaleFix[9] =
{
    0x59u, 0x5Au, 0x1Du, 0x9Du, 0x1Fu, 0x20u, 0x60u, 0x22u, 0xA2u
};

/* VA 0x36F51: load/store descriptor for op 23..35 (HCOP_LBS..HCOP_SD4). */
static const unsigned char armMemDesc[13] =
{
    0x03u, 0x01u, 0x07u, 0x05u, 0x17u, 0x15u, 0x09u,
    0x29u, 0x00u, 0x04u, 0x14u, 0x08u, 0x28u
};

/* VA 0x36F5E: ARM condition codes for HCOP_CBA..HCOP_CBNEQ (op 41..52). */
static const unsigned char armBranchCond[12] =
{
    1, 0, 10, 2, 12, 8, 13, 9, 11, 3, 0, 1
};

/* --- internal state structs ----------------------------------------------- */

typedef struct ArmEmitCtx_s
{
    unsigned int *code;
    int count;
    unsigned int regMask;
} ArmEmitCtx_s;

/* Scratch register state for ARMv5 MUL destination constraints. */
#if !GERBERA_ARMV6_JIT
typedef struct ArmScratchState_s
{
    unsigned int freeSpillSlots;          /* +0: 1-bit mask (slot 0 -> [r0+60]) */
    unsigned int freeHardRegs;            /* +4: per-instr free hard reg mask   */
    unsigned char regStatus[16];          /* +8: status per ARM register 0..14  */
} ArmScratchState_s;

#endif

typedef struct ArmLabelNode_s
{
    struct ArmLabelNode_s *next;
    int label;
    int offset;
} ArmLabelNode_s;

#if !GERBERA_ARMV6_JIT
/* sub_23B4C */
static void armScratchInit(ArmScratchState_s *st)
{
    int i;

    memset(st, 0, sizeof(*st));
    st->regStatus[0] = 0xBFu;
    for (i = 1; i < 14; ++i)
        st->regStatus[i] = 0x3Fu;
    st->freeHardRegs = 0;
    st->freeSpillSlots = 1u;
}

/* sub_23B84 */
static int armAllocSpillSlot(ArmScratchState_s *st)
{
    unsigned int m = st->freeSpillSlots;
    int slot;

    for (slot = 0; slot < 1; ++slot)
    {
        if ((m & (1u << slot)) != 0)
        {
            st->freeSpillSlots = m & ~(1u << slot);
            return slot;
        }
    }
    return -1;
}

#endif


/* sub_23BB8: encode immediate shift field (bits 5..11) for shiftType 1..4. */
static unsigned int armEncodeImmShift(int shiftType, int amount)
{
    if (shiftType != 0)
    {
        unsigned int imm5 = ((unsigned int)amount & 0x1Fu) << 7;
        unsigned int st = 0;
        switch (shiftType)
        {
        case 2: st = 1u; break; /* LSR */
        case 3: st = 2u; break; /* ASR */
        case 4: st = 3u; break; /* ROR */
        default: break;         /* LSL */
        }
        return imm5 + 32u * st;
    }
    return 0;
}

/* --- ARM instruction emitters --------------------------------------------- */

/* sub_23BF8: emit single-data-transfer (LDR/STR/LDRB/STRB/LDRH/STRH/LDRSB/LDRSH).
 * Verified against machine code at VA 0x23BF8..0x23D78 (including the
 * +0x02000000 I-bit at VA 0x23D14 for register-indexed word/byte transfers). */
static unsigned int armEmitMemOp(ArmEmitCtx_s *ctx, int cond, int opKind,
                                 int regsPacked, int offsetOrShift, int flags)
{
    unsigned int sizeField = ((unsigned int)flags >> 1) & 3u;
    int signedFlag = (flags & 8) != 0;
    unsigned int rd = ((unsigned int)(unsigned short)regsPacked) >> 12;
    unsigned int rn = (((unsigned int)regsPacked) >> 8) & 0xFu;
    unsigned int rm = (((unsigned int)regsPacked) >> 4) & 0xFu;
    unsigned int word = ((unsigned int)cond << 28) | (rn << 16) | (rd << 12);
    int isExtraLoadStore = 0;
    unsigned int result;

    ctx->regMask |= (1u << rd) | (1u << rn);
    if (opKind == 19)
        word += 0x00100000u; /* L = 1 (load) */

    if (signedFlag || sizeField == 1u)
    {
        /* ARMv4 halfword / signed-byte transfer: 0x00000090 | S<<6 | H<<5 */
        word += 0x90u;
        if (sizeField == 1u)
            word += 0x20u;   /* H = 1 (halfword) */
        if (signedFlag)
            word += 0x40u;   /* S = 1 (sign-extend) */
        isExtraLoadStore = 1;
    }
    else
    {
        /* Word / unsigned-byte transfer: 0x04000000 | (B<<22) */
        word += 0x04000000u;
        if (sizeField == 0u)
            word += 0x00400000u; /* B = 1 (byte) */
    }
    word += 0x01000000u; /* P = 1 (pre-indexed) */

    if ((flags & 1) == 1)
    {
        /* Register offset */
        ctx->regMask |= (1u << rm);
        result = word + rm;
        if ((flags & 0x40) != 0)
            result += 0x00800000u; /* U = 1 */
        if (isExtraLoadStore == 0)
        {
            unsigned int shiftKind = ((unsigned int)flags >> 7) & 7u;
            result = (result + 0x02000000u) + armEncodeImmShift((int)shiftKind, offsetOrShift);
        }
    }
    else
    {
        /* Immediate offset */
        int off = offsetOrShift;
        if ((flags & 0x40) == 0)
            off = -off;
        if (off < 0)
            off = -off;
        else
            word += 0x00800000u; /* U = 1 */

        if (isExtraLoadStore != 0)
            result = word + 0x00400000u + (unsigned int)(off & 0xF) + 16u * (unsigned int)(off & 0xF0);
        else
            result = word + (unsigned int)off;
    }

    ctx->code[ctx->count++] = result;
    return result;
}

/* sub_23D7C */
static unsigned char *HCARMBackend_targetRegisterMapping(void)
{
    return (unsigned char *)armRegRemap;
}

#if !GERBERA_ARMV6_JIT
/* sub_23D84 (3rd arg `excludeMask` in r2 verified from VA 0x23DA8 & 0x251E4). */
static int armAllocScratchReg(ArmScratchState_s *st, ArmEmitCtx_s *ctx,
                              unsigned int excludeMask)
{
    const unsigned char *remap = HCARMBackend_targetRegisterMapping();
    int i;

    for (i = 1; i < 14; ++i)
    {
        int r = remap[i];
        unsigned int bit = 1u << r;
        if ((excludeMask & bit) == 0
            && (st->regStatus[r] & 0x80u) == 0
            && (st->freeHardRegs & bit) != 0)
        {
            st->freeHardRegs &= ~bit;
            st->regStatus[r] = 0xFFu;
            return r;
        }
    }
    for (i = 1; i < 14; ++i)
    {
        int r = remap[i];
        unsigned int bit = 1u << r;
        if ((excludeMask & bit) == 0 && (st->regStatus[r] & 0x80u) == 0)
        {
            int slot = armAllocSpillSlot(st);
            st->regStatus[r] = (unsigned char)((st->regStatus[r] & 0x40u) | (unsigned int)slot | 0x80u);
            /* Spill `r` to [r0 + 60 + 4*slot] (STR r, [r0, #60]). */
            armEmitMemOp(ctx, 14, 21, (unsigned short)(r << 12), 4 * slot + 60, 68);
            return r;
        }
    }
    return -1;
}

/* sub_23E8C */
static void armFreeScratchReg(ArmScratchState_s *st, ArmEmitCtx_s *ctx, int r)
{
    unsigned char status = st->regStatus[r];

    if ((status & 0x40u) != 0)
    {
        st->regStatus[r] = status & 0xBFu;
    }
    else
    {
        int slot = status & 0x3F;
        /* Reload `r` from [r0 + 60 + 4*slot] (LDR r, [r0, #60]). */
        armEmitMemOp(ctx, 14, 19, (unsigned short)(r << 12), 4 * slot + 60, 68);
        st->freeSpillSlots |= (1u << slot);
    }
    st->regStatus[r] &= 0x7Fu;
}

#endif

/* sub_23F04: emit branch `B<cond>` placeholder with labelId in bits 0..23. */
static void armEmitBranch(ArmEmitCtx_s *ctx, int cond, int labelId)
{
    unsigned int word = ((unsigned int)cond << 28) | 0x0A000000u | ((unsigned int)labelId & 0x00FFFFFFu);
    ctx->code[ctx->count++] = word;
}

/* sub_23F2C: emit ARM data-processing instruction.
 * Verified against machine code at VA 0x23F2C..0x2408C (including `+ rm` at
 * VA 0x2406C after sub_23BB8 in the immediate-shifted-register path). */
static void armEmitDataProc(ArmEmitCtx_s *ctx, int cond, int armOp,
                            int regsPacked, unsigned int immOrAmt, int modeFlags)
{
    unsigned int word = ((unsigned int)cond << 28) + ((unsigned int)armOp << 21);
    int opndMode = modeFlags & 3;
    int shiftType = ((unsigned int)modeFlags >> 2) & 7u;
    unsigned int rd = (((unsigned int)regsPacked) >> 12) & 0xFu;
    unsigned int rn = (((unsigned int)regsPacked) >> 8) & 0xFu;
    unsigned int rm = (((unsigned int)regsPacked) >> 4) & 0xFu;
    unsigned int rs = ((unsigned int)regsPacked) & 0xFu;
    unsigned int shifterOperand;

    if (armAutoSetFlags[armOp] == 1u || (modeFlags & 0x20) != 0)
        word += 0x00100000u; /* S = 1 */

    word += (rn << 16) + (rd << 12);
    ctx->regMask |= (1u << rd) | (1u << rn);

    if (opndMode != 0)
    {
        ctx->regMask |= (1u << rm);
        if (opndMode == 1)
        {
            /* Register-shifted register: Rm, <shift> Rs */
            unsigned int st = 0;
            ctx->regMask |= (1u << rs);
            switch (shiftType)
            {
            case 2: st = 1u; break;
            case 3: st = 2u; break;
            case 4: st = 3u; break;
            default: break;
            }
            shifterOperand = rm + (rs << 8) + 16u + 32u * st;
        }
        else
        {
            /* Immediate-shifted (or unshifted when shiftType==0) register Rm */
            shifterOperand = armEncodeImmShift(shiftType, (int)immOrAmt) + rm;
        }
    }
    else
    {
        /* 8-bit rotated immediate (I = 1 -> 0x02000000) */
        unsigned int val = immOrAmt;
        unsigned int rot = 0;
        if (immOrAmt != 0 && (immOrAmt & 3u) == 0)
        {
            do
            {
                val >>= 2;
                rot += 2u;
            }
            while ((val & 3u) == 0);
            if (rot != 0)
                rot = 32u - rot;
        }
        shifterOperand = (val + ((rot >> 1) << 8)) | 0x02000000u;
    }

    ctx->code[ctx->count++] = word + shifterOperand;
}

/* sub_24090: emit block data transfer (STMDB / LDMIA for prologue/epilogue). */
static void armEmitBlockMem(ArmEmitCtx_s *ctx, int cond, int opKind, int baseReg,
                            unsigned int regList, int postIndex, int writeBack, int upBit)
{
    unsigned int word = ((unsigned int)cond << 28) + 0x08000000u + ((unsigned int)baseReg << 16);

    if (postIndex == 0)
        word += 0x01000000u; /* P = 1 */
    if (writeBack == 1)
        word += 0x00200000u; /* W = 1 */
    if (upBit == 1)
        word += 0x00800000u; /* U = 1 */
    if (opKind == 20)
        word += 0x00100000u; /* L = 1 (LDM) */

    ctx->regMask |= regList | (1u << baseReg);
    ctx->code[ctx->count++] = word + regList;
}

/* --- simple vtable methods ------------------------------------------------ */

/* sub_24100 */
static int HCARMBackend_internalStackSpace(void)
{
    return 60;
}

/* sub_24108 */
static int HCARMBackend_numHardRegs(void)
{
    return 14;
}

/* sub_24110 */
static int HCARMBackend_isNative(void)
{
    return 1;
}

/* sub_24118 */
static void HCARMBackend_emulate(void (*program)(void *), void *stackPtr)
{
    program(stackPtr);
}

/* sub_25480: bare `bx lr` in slot 9 (+36). */
static void HCARMBackend_disasm(void *program, int length, void (*output)(char *))
{
    (void)program;
    (void)length;
    (void)output;
}

/* --- immediate recognition & preRegallocFixer ----------------------------- */

/* sub_24124: count how many even-aligned 8-bit chunks are needed to cover `val`. */
static int armCountImmChunks(unsigned int val)
{
    int chunks;
    int bit = 0;

    if (val == 0)
        return 1;
    chunks = 0;
    do
    {
        int nonZero = (val & (3u << bit)) != 0;
        if (nonZero)
        {
            bit += 6;
            ++chunks;
        }
        bit += 2;
    }
    while (bit < 32);

    return chunks;
}

/* sub_24158: map HCOP shift/rotate opcode -> internal shiftType 1..4. */
static int armShiftTypeForOp(int op)
{
    switch (op)
    {
    case HCOP_ASR: return 3;
    case HCOP_LSL: return 1;
    case HCOP_ROR: return 4;
    default:       return 2; /* HCOP_LSR */
    }
}

/* sub_245C0 */
static int HCARMBackend_canCreateImmediate(HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);
    int imm = (int)instr->instr1;
    unsigned int immCls = ((unsigned int)armOpcodeInfo[op] >> 5) & 7u;

#if GERBERA_ARMV6_JIT
    if (op == HCOP_AND && imm == 0xFFFF)
        return 1; /* UXTH */
#endif

    switch (immCls)
    {
    case 0u:
        if (armCountImmChunks((unsigned int)imm) > 1
            && armCountImmChunks(~(unsigned int)imm) > 1)
        {
            return 0;
        }
        return 1;
    case 1u:
        return armCountImmChunks((unsigned int)imm) <= 1;
    case 2u:
    case 6u:
        return 1;
    case 4u:
        return (imm < 256 && imm > -256);
    case 5u:
        return (imm < 4096 && imm > -4096);
    default:
        return 0;
    }
}

#if GERBERA_ARMV6_JIT
/* Widen even and odd byte pairs into two independent 16-bit lanes. Each
 * product is <= 255*256, so it can't carry across lanes. Extract each term's
 * high product byte *before* adding, to keep framebuffer blend rounding. */
static int armLowerPackedAlpha(HCInstr_s *out, HCInstr_s *instr, int *nextReg)
{
    int a = HC_getOpcodeSrc0Raw(instr), b = HC_getOpcodeSrc1Raw(instr);
    int op = HC_getOpcodeOper(instr);
    int rgb = op == HCOP_PACKED_ALPHA_RGB_A || op == HCOP_PACKED_ALPHA_RGB_B;
    int alphaSrc = (op == HCOP_PACKED_ALPHA_A || op == HCOP_PACKED_ALPHA_RGB_A) ? a : b;
    int alpha = (*nextReg)++, direct = (*nextReg)++, inverse = (*nextReg)++;
    int pair[2], i, n = 0;
    out[n++] = HC_genOp3Imm(HCOP_LSR, alpha, alphaSrc, 24);
    out[n++] = HC_genOp3Imm(HCOP_ADD, direct, alpha, 1);
    out[n++] = HC_genOp3Imm(HCOP_SUBR, inverse, alpha, 256);
    for (i = 0; i < 2; ++i)
    {
        int left = (*nextReg)++, right = (*nextReg)++;
        /* On RGB, use the single odd byte (G); neither source's high
         * byte may become part of the stored result. */
        int widen = (rgb && i == 1) ? HCOP_UXTB_ROR : HCOP_UXTB16;
        out[n++] = HC_genOp3Imm(widen, left, a, i * 8);
        out[n++] = HC_genOp3Imm(widen, right, b, i * 8);
        out[n++] = HC_genOp3(HCOP_MULU, left, left, direct);
        out[n++] = HC_genOp3(HCOP_MULU, right, right, inverse);
        out[n++] = HC_genOp3Imm(HCOP_UXTB16, left, left, 8);
        out[n++] = HC_genOp3(HCOP_UXTAB16_ROR8, left, left, right);
        pair[i] = left;
    }
    /* Each truncated term is <= its multiplier minus one. Their sum
     * is therefore <= (direct - 1) + (inverse - 1) == 255. */
    out[n++] = HC_genOp3Imm(HCOP_LSL, pair[1], pair[1], 8);
    out[n++] = HC_genOp3(HCOP_OR, HC_getOpcodeDst(instr), pair[0], pair[1]);
    return n;
}

/* Keeping a shared depth address live is profitable in simple color tails;
 * destination reads and multiple texture loads need the register for shading. */
static int armSimpleDepthTail(HCInstr_s *tail)
{
    int i, loadBase = -1;
    for (i = 0; i < 128; ++i)
    {
        HCInstr_s *ins = &tail[i];
        int op = HC_getOpcodeOper(ins);
        if (op == HCOP_EOP || op == HCOP_LABEL || HC_isBranchOp(op)) return 0;
        if (HC_isLoadOp(op))
        {
            if (loadBase >= 0) return 0;
            loadBase = HC_getOpcodeSrc0Raw(ins);
        }
        if (HC_isStoreOp(op)) return loadBase != HC_getOpcodeSrc0Raw(ins);
    }
    return 0;
}

#endif

static int HCARMBackend_preRegallocFixer(void *mgrVoid, HCInstr_s **dest,
                                         HCInstr_s *src, void *compHandle)
{
    HCArena_s *mgr = (HCArena_s *)mgrVoid;
    int maxR = 0;
    int scanIdx = 0;
    int inIdx = 0;
    int nextReg;
    int mark;
    HCInstr_s *out;
    int outIdx = 0;
    int prevOp;
#if GERBERA_ARMV6_JIT
    int depthStore = -1, depthAddress = -1;
#endif

    (void)compHandle;

    do
    {
        int uses[3];
        int nUses = HC_getUses(&src[scanIdx], uses);
        int k;
        int def;

        for (k = 0; k < nUses; ++k)
        {
            if (uses[k] > maxR)
                maxR = uses[k];
        }
        def = HC_getDef(&src[scanIdx]);
        if (def > maxR)
            maxR = def;
        prevOp = HC_getOpcodeOper(&src[scanIdx++]);
    }
    while (prevOp != HCOP_EOP);

    nextReg = maxR + 1;
    mark = HCArena_markBottom(mgr);
    out = (HCInstr_s *)HCArena_current(mgr);
    *dest = out;

    do
    {
        HCInstr_s *instr = &src[inIdx];
        int op = HC_getOpcodeOper(instr);

#if GERBERA_ARMV6_JIT
        if (op == HCOP_PACKED_ALPHA_A || op == HCOP_PACKED_ALPHA_B ||
            op == HCOP_PACKED_ALPHA_RGB_A || op == HCOP_PACKED_ALPHA_RGB_B)
        {
            HCArena_growCurrent(mgr, 17u * sizeof(HCInstr_s));
            outIdx += armLowerPackedAlpha(out + outIdx, instr, &nextReg);
            goto next_instr;
        }
        if (op == HCOP_UQADD8_RGB || op == HCOP_UQSUB8_RGB)
        {
            int temp = nextReg++;
            HCArena_growCurrent(mgr, 2u * sizeof(HCInstr_s));
            out[outIdx++] = HC_genOp3(op == HCOP_UQADD8_RGB ? HCOP_UQADD8 : HCOP_UQSUB8,
                                      temp, HC_getOpcodeSrc0Raw(instr), HC_getOpcodeSrc1Raw(instr));
            out[outIdx++] = HC_genOp3Imm(HCOP_BIC, HC_getOpcodeDst(instr), temp, (int)0xFF000000u);
            goto next_instr;
        }
#endif
        HCArena_growCurrent(mgr, 8u);

#if GERBERA_ARMV6_JIT
        if (inIdx == depthStore)
        {
            out[outIdx++] = HC_genOp3Imm(HCOP_SW, HC_getOpcodeDst(instr), depthAddress, 0);
            goto next_instr;
        }
        /* The depth load and conditional store address the same halfword.
         * Keep the full address across the comparison so a spilled base
         * need not be loaded again on the passing path. */
        if ((op == HCOP_LW2U || op == HCOP_LW2S) && !HC_opcodeHasImm(instr) &&
            inIdx + 2 < scanIdx && HC_isCondBranchOp(HC_getOpcodeOper(&src[inIdx + 1])))
        {
            HCInstr_s *store = &src[inIdx + 2];
            int base = HC_getOpcodeSrc0Raw(instr), index = HC_getOpcodeSrc1Raw(instr);
            int dst = HC_getOpcodeDst(instr);
            if (HC_getOpcodeOper(store) == HCOP_SW2 && !HC_opcodeHasImm(store) &&
                dst != base && dst != index && base == HC_getOpcodeSrc0Raw(store) &&
                index == HC_getOpcodeSrc1Raw(store) && armSimpleDepthTail(store + 1))
            {
                int depthIndex = nextReg++;
                depthAddress = nextReg++;
                depthStore = inIdx + 2;
                out[outIdx++] = HC_genOp3Imm(HCOP_LSL, depthIndex, index, 1);
                HCArena_growCurrent(mgr, 2u * sizeof(HCInstr_s));
                out[outIdx++] = HC_genOp3(HCOP_ADD, depthAddress, base, depthIndex);
                out[outIdx++] = HC_genOp3Imm(op == HCOP_LW2U ? HCOP_LWU : HCOP_LWS,
                                            dst, depthAddress, 0);
                goto next_instr;
            }
        }
#endif

        /* Scaled loads/stores (LW2S, LW2U, LD4, SW2, SD4 -> op 27..35):
         * scale immediate offsets by 2 or 4, and expand halfword scaled-index
         * (shift == 1) into LSL tmp, s1, #1 + unscaled LWS/LWU/SW. */
        if ((unsigned int)(op - HCOP_LW2S) < 9u)
        {
            unsigned int fix = armMemScaleFix[op - HCOP_LW2S];
            unsigned int shift = fix >> 6;
            if (shift != 0)
            {
                int baseOp = (int)(fix & 0x3Fu);
                int s0 = (int)(instr->instr0 >> 21);
                int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
                if ((instr->instr0 & 0x100u) != 0)
                {
                    out[outIdx++] = HC_genOp3Imm(baseOp, dst, s0,
                                                 (int)(instr->instr1 << shift));
                    goto next_instr;
                }
                if (shift == 1u)
                {
                    int tmp = nextReg++;
                    out[outIdx++] = HC_genOp3Imm(HCOP_LSL, tmp,
                                                 (int)(instr->instr1 & 0x7FFu), 1);
                    HCArena_growCurrent(mgr, 8u);
                    out[outIdx++] = HC_genOp3(baseOp, dst, s0, tmp);
                    goto next_instr;
                }
            }
        }

        if (op == HCOP_ROR)
        {
            if ((instr->instr0 & 0x100u) != 0 && instr->instr1 == 0)
            {
                int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
                int s0 = (int)(instr->instr0 >> 21);
                out[outIdx++] = HC_genOp2(HCOP_MOV, dst, s0);
                goto next_instr;
            }
        }
        else if (op == HCOP_ROL)
        {
            /* ARM has ROR but no ROL: lower ROL(s0, k) into LSR + LSL + OR. */
            int s0 = (int)(instr->instr0 >> 21);
            int hasImm = (instr->instr0 & 0x100u) != 0;
            int dst = (int)((instr->instr0 >> 10) & 0x7FFu);

            if (hasImm != 0)
            {
                int tmp = nextReg++;
                int amt = (int)instr->instr1;
                HCArena_growCurrent(mgr, 16u);
                if (amt != 0)
                {
                    out[outIdx++] = HC_genOp3Imm(HCOP_LSR, tmp, s0, 32 - amt);
                    out[outIdx++] = HC_genOp3Imm(HCOP_LSL, dst, s0, amt);
                    out[outIdx++] = HC_genOp3(HCOP_OR, dst, dst, tmp);
                }
                else
                {
                    out[outIdx++] = HC_genOp2(HCOP_MOV, dst, s0);
                }
            }
            else
            {
                int s1 = (int)(instr->instr1 & 0x7FFu);
                int tmpShifted = nextReg;
                int tmpInvAmt = nextReg + 1;
                nextReg += 2;
                HCArena_growCurrent(mgr, 24u);
                out[outIdx++] = HC_genOp3Imm(HCOP_SUBR, tmpInvAmt, s1, 32);
                out[outIdx++] = HC_genOp3Imm(HCOP_AND, tmpInvAmt, tmpInvAmt, 31);
                out[outIdx++] = HC_genOp3(HCOP_LSR, tmpShifted, s0, tmpInvAmt);
                out[outIdx++] = HC_genOp3(HCOP_LSL, dst, s0, s1);
                out[outIdx++] = HC_genOp3(HCOP_OR, dst, dst, tmpShifted);
            }
            goto next_instr;
        }

        /* Synthesize common 12-bit / 16-bit masks (0xFFF / 0xFFFF) via
         * MOVN tmp, #0 (= 0xFFFFFFFF) + LSR tmp, tmp, #20 / #16. */
        if ((instr->instr0 & 0x100u) != 0
            && ((armOpcodeInfo[op] >> 10) & 1u) == 1u
            && (instr->instr1 == 0xFFFu || (instr->instr1 == 0xFFFFu
                && (!GERBERA_ARMV6_JIT || op != HCOP_AND))))
        {
            int tmp = nextReg++;
            int lsrAmt = (instr->instr1 == 0xFFFu) ? 20 : 16;
            int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
            int s0 = (int)(instr->instr0 >> 21);

            HCArena_growCurrent(mgr, 16u);
            out[outIdx++] = HC_genOp2Imm(HCOP_MOVN, tmp, 0);
            out[outIdx++] = HC_genOp3Imm(HCOP_LSR, tmp, tmp, lsrAmt);
            out[outIdx++] = HC_genOp3(op, dst, s0, tmp);
        }
        else
        {
            out[outIdx++] = *instr;
        }

    next_instr:
        prevOp = HC_getOpcodeOper(&src[inIdx++]);
    }
    while (prevOp != HCOP_EOP);

    return mark;
}

/* --- codegen helpers ------------------------------------------------------ */

/* sub_24674 */
static void armGenLoadStore(ArmEmitCtx_s *ctx, int cond, HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);
    unsigned int desc = armMemDesc[op - HCOP_LBS];
    unsigned int rdField = ((instr->instr0 >> 10) & 0x7FFu) << 12;
    unsigned int rn = instr->instr0 >> 21;
    int opKind = ((desc & 1u) != 0) ? 19 : 21;
    int flagsBase = (int)(2u * ((desc >> 2) & 3u) + 8u * ((desc >> 1) & 1u));
    int offsetOrShift = -1;
    int regsPacked = (int)(rdField + (rn << 8));
    int flags;

    if ((instr->instr0 & 0x100u) != 0)
    {
        int imm = (int)instr->instr1;
        offsetOrShift = (imm < 0) ? -imm : imm;
        flags = flagsBase + ((imm >= 0) ? 64 : 0);
    }
    else
    {
        int isScaledWord = (desc & 0x20u) != 0;
        flags = flagsBase + (isScaledWord ? 128 : 0) + 65;
        regsPacked += 16 * (int)(instr->instr1 & 0x7FFu);
        if (isScaledWord)
            offsetOrShift = 2;
    }

    armEmitMemOp(ctx, cond, opKind, regsPacked, offsetOrShift, flags);
}

#if GERBERA_ARMV6_JIT
/* Bounded proof of an unsigned upper bound, following unique reaching
 * definitions only. UINT_MAX also represents an unknown bit pattern. Do not
 * infer a byte bound for an interpolant merely because it supplies a color. */
static unsigned int armUnsignedMax(HCInstr_s *instrs, int pc, void *info,
                                   int reg, int depth)
{
    void *ud;
    HCInstr_s *def;
    int defPC, op, src, imm;
    unsigned int a, b;

    if (depth == 8)
        return 0xFFFFFFFFu;
    ud = HC_getUDChain(info, pc, reg);
    if (ud == NULL || HCChain_getLength(ud) != 1)
        return 0xFFFFFFFFu;
    defPC = HCChain_getValue(ud, 0);
    if (defPC < 0 || defPC >= pc)
        return 0xFFFFFFFFu;
    def = &instrs[defPC];
    op = HC_getOpcodeOper(def);
    src = HC_getOpcodeSrc0Raw(def);
    imm = HC_opcodeHasImm(def);
    b = def->instr1;

    switch (op)
    {
    case HCOP_LBU:
    case HCOP_UXTB_ROR: return 255u;
    case HCOP_LWU:
    case HCOP_LW2U: return 65535u;
    case HCOP_MOV:
        return imm ? b : armUnsignedMax(instrs, defPC, info, src, depth + 1);
    case HCOP_AND:
        if (imm) return b;
        a = armUnsignedMax(instrs, defPC, info, src, depth + 1);
        b = armUnsignedMax(instrs, defPC, info, (int)(b & 0x7ffu), depth + 1);
        return a < b ? a : b;
    case HCOP_LSR:
    case HCOP_LSL:
        if (!imm || b >= 32u) break;
        a = armUnsignedMax(instrs, defPC, info, src, depth + 1);
        if (op == HCOP_LSR) return a >> b;
        if (a <= (0xFFFFFFFFu >> b)) return a << b;
        break;
    case HCOP_MINU:
        if (imm) return b;
        break;
    case HCOP_MINS:
        if (imm && b <= 0x7FFFFFFFu &&
            armUnsignedMax(instrs, defPC, info, src, depth + 1) <= 0x7FFFFFFFu)
            return b;
        break;
    case HCOP_SUBR:
        if (imm && armUnsignedMax(instrs, defPC, info, src, depth + 1) <= b)
            return b;
        break;
    case HCOP_ADD:
    case HCOP_MULS:
    case HCOP_MULU:
        a = armUnsignedMax(instrs, defPC, info, src, depth + 1);
        if (!imm)
            b = armUnsignedMax(instrs, defPC, info, (int)(b & 0x7ffu), depth + 1);
        if (op == HCOP_ADD)
        {
            if (a <= 0xFFFFFFFFu - b) return a + b;
        }
        else
        {
            /* 32x32->64 is UMULL on ARM; avoid a software divide in the JIT. */
            unsigned long long product = (unsigned long long)a * b;
            if (product <= 0xFFFFFFFFu) return (unsigned int)product;
        }
        break;
    default:
        break;
    }
    return 0xFFFFFFFFu;
}

/* SMULxy/SMLAxy sign extend their halfword operands. */
static int armIsProvably16Bit(HCInstr_s *instrs, int pc, void *info, int reg)
{
    void *ud = HC_getUDChain(info, pc, reg);
    if (ud != NULL && HCChain_getLength(ud) == 1)
    {
        int defPC = HCChain_getValue(ud, 0);
        if (defPC >= 0 && defPC < pc)
        {
            HCInstr_s *def = &instrs[defPC];
            int op = HC_getOpcodeOper(def);
            if (op == HCOP_LBS || op == HCOP_LWS || op == HCOP_LW2S)
                return 1;
            if (HC_opcodeHasImm(def) &&
                ((op == HCOP_ASR && def->instr1 >= 16u && def->instr1 < 32u) ||
                 (op == HCOP_MOV && def->instr1 >= 0xFFFF8000u)))
                return 1;
        }
    }
    return armUnsignedMax(instrs, pc, info, reg, 0) <= 32767u;
}

/* Return the ARM USAT width for #(2^n - 1), or zero for other immediates. */
static int armSatWidth(unsigned int limit)
{
    int width = 0;
    if (limit <= 0x7FFFFFFFu && (limit & (limit + 1u)) == 0)
        for (; limit != 0; limit >>= 1) ++width;
    return width;
}

static unsigned int armUSatWord(int rd, int width, int rm, unsigned int asr)
{
    return 0xE6E00010u | ((unsigned int)width << 16) | ((unsigned int)rd << 12)
        | (asr != 0 ? (asr << 7) | 0x40u : 0) | (unsigned int)rm;
}

static unsigned int armUQAdd8Word(int rd, int rn, int rm)
{
    return 0xE6600F90u | ((unsigned int)rn << 16) | ((unsigned int)rd << 12) | (unsigned int)rm;
}

static unsigned int armUXTBWord(int rd, int rm, unsigned int shift, int pair)
{
    return (pair ? 0xE6CF0070u : 0xE6EF0070u) |
        ((unsigned int)rd << 12) | ((shift / 8u) << 10) | (unsigned int)rm;
}

/* Check that sinking the producer to its single consumer preserves its input.
 * Exclude control-flow edges, and do not rely on the DU chain alone: at a join
 * the use could also have a different reaching definition. */
static int armSingleUse(HCInstr_s *instrs, int pc, void *info, int reg, int input, int input2)
{
    void *du = HC_getDUChain(info, pc);
    void *ud;
    int i, usePC;
    if (du == NULL || HCChain_getLength(du) != 1)
        return -1;
    usePC = HCChain_getValue(du, 0);
    if (usePC <= pc)
        return -1;
    ud = HC_getUDChain(info, usePC, reg);
    if (ud == NULL || HCChain_getLength(ud) != 1 || HCChain_getValue(ud, 0) != pc)
        return -1;
    for (i = pc + 1; i < usePC; ++i)
    {
        HCInstr_s *mid = &instrs[i];
        int def = HC_getDef(mid);
        if ((def >= 0 && (def == input || def == input2)) ||
            HC_getOpcodeOper(mid) == HCOP_LABEL ||
            HC_getOpcodeType(mid) == HCOP_TYPE_BRANCH)
            return -1;
    }
    return usePC;
}

#endif

/* Pending consumers remain ordered even when independent folds overlap. */
static int armDeferWord(unsigned int *words, int *pcs, int *count, int *first,
                         int capacity, int pc, unsigned int word)
{
    int live = *count - *first, insert = 0, i;
    if (live >= capacity) return 0;
    while (insert < live && pcs[*first + insert] < pc) ++insert;
    if (insert < live && pcs[*first + insert] == pc) return 0;
    for (i = 0; i < live; ++i)
    {
        pcs[i] = pcs[*first + i];
        words[i] = words[*first + i];
    }
    for (i = live; i > insert; --i)
    {
        pcs[i] = pcs[i - 1];
        words[i] = words[i - 1];
    }
    pcs[insert] = pc;
    words[insert] = word;
    *first = 0;
    *count = live + 1;
    return 1;
}

/* sub_24734: MAXS, MAXU, MINS, MINU. */
static void armGenMinMax(ArmEmitCtx_s *ctx, HCInstr_s *instr,
                         HCInstr_s *instrs, int pc, void *info)
{
    int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
    int op = HC_getOpcodeOper(instr);
    int s0 = (int)(instr->instr0 >> 21);
    int hasImm = (instr->instr0 & 0x100u) != 0;
    unsigned int immVal = instr->instr1;
    int kind = op - HCOP_MAXS;
    unsigned int opndImm;
    unsigned short cmpRm;
    int modeFlags;

    if (hasImm != 0)
    {
        /* Fast path: MAXS dst, s0, #0 -> BIC dst, s0, s0, ASR #31 */
        if (op == HCOP_MAXS && immVal == 0)
        {
            armEmitDataProc(ctx, 14, 14,
                            (unsigned short)((dst << 12) + (s0 << 8) + 16 * s0),
                            31u, 14);
            return;
        }
#if GERBERA_ARMV6_JIT
        /* ARMv6 USAT: MINS/MINU dst, s0, #(2^n-1) where src is provably
         * non-negative clamps to [0, 2^n-1] in one instruction. */
        int width = (op == HCOP_MINS || op == HCOP_MINU) ? armSatWidth(immVal) : 0;
        if (width != 0)
        {
            unsigned int maxVal = armUnsignedMax(instrs, pc, info, s0, 0);
            if (maxVal <= immVal)
            {
                if (dst != s0)
                    armEmitDataProc(ctx, 14, 13, (dst << 12) | (s0 << 4),
                                    0xFFFFFFFFu, 2);
                return;
            }
            if (maxVal <= 0x7FFFFFFFu)
            {
                ctx->regMask |= (1u << dst) | (1u << s0);
                ctx->code[ctx->count++] = armUSatWord(dst, width, s0, 0);
                return;
            }
        }
#endif
        opndImm = immVal;
        cmpRm = 0;
        modeFlags = 0;
        if (dst != s0)
        {
            armEmitDataProc(ctx, 14, 13,
                            (unsigned short)((dst << 12) + 16 * s0),
                            0xFFFFFFFFu, 2);
        }
    }
    else
    {
        int s1 = (int)(immVal & 0x7FFu);
        opndImm = 0xFFFFFFFFu;
        cmpRm = (unsigned short)s0;
        modeFlags = 2;
        if (dst != s1)
        {
            cmpRm = (unsigned short)s1;
            if (dst != s0)
            {
                armEmitDataProc(ctx, 14, 13,
                                (unsigned short)((dst << 12) + 16 * s0),
                                0xFFFFFFFFu, 2);
            }
        }
        else if (dst == s0)
        {
            return;
        }
    }

    /* CMP dst, opnd; MOV<cond> dst, opnd */
    armEmitDataProc(ctx, 14, 10,
                    (unsigned short)((dst << 8) + 16 * cmpRm),
                    opndImm, modeFlags);
    armEmitDataProc(ctx, armMinMaxCond[kind], 13,
                    (unsigned short)((dst << 12) + 16 * cmpRm),
                    opndImm, modeFlags);
}

/* sub_2488C: standard 3-operand / compare ARM DP instruction. */
static void armGenALU(ArmEmitCtx_s *ctx, HCInstr_s *instr, int armOp, int dst)
{
    int hasImm = (instr->instr0 & 0x100u) != 0;
    int s0 = (int)(instr->instr0 >> 21);
    int rm = hasImm ? 0 : (int)(instr->instr1 & 0x7FFu);
    unsigned int immVal = hasImm ? instr->instr1 : 0xFFFFFFFFu;
    int modeFlags = hasImm ? 0 : 2;
    unsigned short regsPacked = (unsigned short)((dst << 12) + (s0 << 8) + 16 * rm);

    armEmitDataProc(ctx, 14, armOp, regsPacked, immVal, modeFlags);
}

/* sub_248F4: shifted-operand ARM DP instruction (standalone shift or fused ALU). */
static void armGenShiftedALU(ArmEmitCtx_s *ctx, HCInstr_s *shiftInstr,
                             int armOp, int dst, int rn, int rm, int shiftType)
{
    int hasImm = (shiftInstr->instr0 & 0x100u) != 0;
    unsigned int immOrAmt = hasImm ? shiftInstr->instr1 : 0xFFFFFFFFu;
    int opndMode = hasImm ? 2 : 1;
    int rs = hasImm ? 0 : (int)(shiftInstr->instr1 & 0x7FFu);
    unsigned short regsPacked = (unsigned short)((dst << 12) + (rn << 8) + 16 * rm + rs);

    armEmitDataProc(ctx, 14, armOp, regsPacked, immOrAmt, opndMode + 4 * shiftType);
}

/* sub_2496C: conditional branch with CPSR S-bit fusion onto preceding ALU op. */
static void armGenCondBranch(ArmEmitCtx_s *ctx, HCInstr_s *instrs, int pc, void *info,
                             const int *recentWords)
{
    HCInstr_s *instr = &instrs[pc];
    int op = HC_getOpcodeOper(instr);
    int hasImm = (instr->instr0 & 0x100u) != 0;
    int s0 = (int)(instr->instr0 >> 21);
    int dstLabel = (int)((instr->instr0 >> 10) & 0x7FFu);
    int tryFuseFlags = 0;

    if (hasImm != 0 && instr->instr1 == 0)
    {
        if (op != HCOP_CBA && op != HCOP_CBNA)
            tryFuseFlags = 1;
    }
    if (!tryFuseFlags
        && (op == HCOP_CBA || op == HCOP_CBNA)
        && hasImm == 0
        && s0 == (int)(instr->instr1 & 0x7FFu))
    {
        tryFuseFlags = 1;
    }

    if (tryFuseFlags && pc != 0 && ctx->count > 0)
    {
        int previous = pc - 1, wordPC = ctx->count - 1;
#if GERBERA_ARMV6_JIT
        if (hasImm && instr->instr1 == 0 && (op == HCOP_CBEQ || op == HCOP_CBNEQ))
        {
            void *chain = HC_getUDChain(info, pc, s0);
            if (chain != NULL && HCChain_getLength(chain) == 1)
            {
                int def = HCChain_getValue(chain, 0), i;
                if (def >= 0 && def < pc - 1 && pc - def < 8 &&
                    recentWords[8 + (def & 7)] == def + 1 &&
                    recentWords[8 + ((def + 1) & 7)] == def + 2 &&
                    recentWords[(def + 1) & 7] == recentWords[def & 7] + 1)
                {
                    for (i = def + 1; i < pc; ++i)
                    {
                        int middle = HC_getOpcodeOper(&instrs[i]);
                        if ((middle >= HCOP_MAXS && middle <= HCOP_MINU) ||
                            middle == HCOP_LABEL || HC_isBranchOp(middle)) break;
                    }
                    if (i == pc)
                    {
                        previous = def;
                        wordPC = recentWords[def & 7];
                    }
                }
            }
        }
#else
        (void)recentWords;
#endif
        int prevOp = HC_getOpcodeOper(&instrs[previous]);
        unsigned int flagCls = ((unsigned int)armOpcodeInfo[prevOp] >> 8) & 3u;
        unsigned int word = ctx->code[wordPC];
        unsigned int actualOp = (word >> 21) & 15u;
        if (flagCls != 0 && (word & 0x0C000000u) == 0 &&
            ((word & 0x02000000u) != 0 || (word & 0x90u) != 0x90u) &&
            ((word >> 12) & 15u) == (unsigned int)s0 &&
            !(actualOp >= 8 && actualOp <= 11))
        {
            if (flagCls != 1u
                || op == HCOP_CBEQ || op == HCOP_CBNEQ
                || op == HCOP_CBA  || op == HCOP_CBNA)
            {
                void *ud = HC_getUDChain(info, pc, s0);
                if (ud != NULL
                    && HCChain_getLength(ud) == 1
                    && HCChain_getValue(ud, 0) == previous)
                {
                    /* EQ/NE may reuse Z across independent instructions;
                     * signed comparisons retain the adjacent-only rule. */
                    ctx->code[wordPC] |= 0x00100000u;
                    armEmitBranch(ctx, armBranchCond[op - HCOP_CBA], dstLabel);
                    return;
                }
            }
        }
    }

    armGenALU(ctx, instr, (op == HCOP_CBA || op == HCOP_CBNA) ? 8 : 10, 0);
    armEmitBranch(ctx, armBranchCond[op - HCOP_CBA], dstLabel);
}

/* sub_24ABC */
static void armGrowCodeBuf(HCArena_s *mgr, int *allocated, int needed)
{
    if (needed - *allocated > 64)
    {
        HCArena_growUnaligned(mgr, 128u);
        *allocated += 128;
    }
}

/* --- HCARMBackend_codegen (sub_24AEC) ------------------------------------- */

static int HCARMBackend_codegen(void *mgrVoid, void **destination,
                                HCInstr_s *instrs, void *infoVoid, int spillStart)
{
    HCArena_s *mgr = (HCArena_s *)mgrVoid;
    HCBackendInfo_s *info = (HCBackendInfo_s *)infoVoid;
    int pc = 0;
    int deferredCount = 0;
    int deferredIdx = 0;
    enum { maxDeferred = 8 };
    unsigned int deferredWords[maxDeferred];
    int deferredPCs[maxDeferred];
    int recentWords[16] = {0};
    int running = 1;
    int allocated = 0;
    int topMark = HCArena_markTop(mgr);
    ArmLabelNode_s *labelHead = NULL;
    ArmLabelNode_s *labelTail = NULL;
    ArmEmitCtx_s ctx;
#if !GERBERA_ARMV6_JIT
    ArmScratchState_s scratch;
#endif
    int j;

    (void)spillStart;

    *destination = HCArena_current(mgr);
    ctx.code = (unsigned int *)(*destination);
    ctx.count = 0;
    ctx.regMask = 0;

#if !GERBERA_ARMV6_JIT
    armScratchInit(&scratch);
#endif
    armGrowCodeBuf(mgr, &allocated, 8);
    /* Prologue: STMFD sp!, {r0-r12, lr} (0xE92D5FFF). */
    armEmitBlockMem(&ctx, 14, 22, 13, 0x5FFFu, 0, 1, 0);
#if GERBERA_ARMV6_JIT
    ctx.regMask = 0;
#endif

    while (running != 0)
    {
        HCInstr_s *instr = &instrs[pc];
        int op = HC_getOpcodeOper(instr);
        unsigned int cgClass;

        armGrowCodeBuf(mgr, &allocated, 4 * ctx.count + 16);
        recentWords[pc & 7] = ctx.count;
        recentWords[8 + (pc & 7)] = pc + 1;

        if (deferredIdx < deferredCount && deferredPCs[deferredIdx] == pc)
        {
            if (deferredWords[deferredIdx] != 0)
                ctx.code[ctx.count++] = deferredWords[deferredIdx];
            ++pc;
            ++deferredIdx;
            continue;
        }

#if !GERBERA_ARMV6_JIT
        scratch.freeHardRegs = HC_getFreeHardRegs(info, pc) & 0xFFFFFFFEu;
#endif
        cgClass = ((unsigned int)armOpcodeInfo[op] >> 11) & 0xFu;

        switch (cgClass)
        {
        case 1u: /* HCOP_MOV, HCOP_MOVN */
            if ((instr->instr0 & 0x100u) != 0)
            {
                int imm = (int)instr->instr1;
                int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
                if (op == HCOP_MOVN)
                    imm = ~imm;
                if ((unsigned int)(imm + 256) >= 0x200u)
                {
                    int posChunks = armCountImmChunks((unsigned int)imm);
                    int negImm = ~imm;
                    int negChunks = armCountImmChunks((unsigned int)negImm);
                    int useNeg = (posChunks > negChunks);
                    int armOp = useNeg ? 15 : 13; /* MVN : MOV */
                    int bit = 0;
                    int first = 1;
                    int rn = 0;

                    if (useNeg)
                        imm = negImm;
                    do
                    {
                        if (((unsigned int)imm & (3u << bit)) != 0)
                        {
                            if (!first)
                            {
                                armOp = useNeg ? 14 : 12; /* BIC : ORR */
                                rn = dst;
                            }
                            first = 0;
                            armEmitDataProc(&ctx, 14, armOp,
                                            (unsigned short)((dst << 12) + (rn << 8)),
                                            (unsigned int)imm & (255u << bit), 0);
                            bit += 6;
                        }
                        bit += 2;
                    }
                    while (bit < 32);
                }
                else
                {
                    int armOp = 13;
                    if (imm < 0)
                    {
                        armOp = 15;
                        imm = ~imm;
                    }
                    armEmitDataProc(&ctx, 14, armOp,
                                    (unsigned short)(dst << 12),
                                    (unsigned int)imm, 0);
                }
            }
            else
            {
                int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
                int s0 = (int)(instr->instr0 >> 21);
                armEmitDataProc(&ctx, 14, armOpcodeInfo[op] & 0x1F,
                                (unsigned short)((dst << 12) + 16 * s0),
                                0xFFFFFFFFu, 2);
            }
            break;

        case 2u: /* ADD, AND, BIC, OR, SUB, SUBR, XOR */
#if GERBERA_ARMV6_JIT
            if (deferredIdx == deferredCount && op == HCOP_ADD && !HC_opcodeHasImm(instr))
            {
                int a = HC_getOpcodeSrc0Raw(instr), b = HC_getOpcodeSrc1Raw(instr);
                int rd = HC_getOpcodeDst(instr);
                int usePC = armSingleUse(instrs, pc, info, rd, a, b);
                if (usePC >= 0)
                {
                    HCInstr_s *use = &instrs[usePC];
                    int useOp = HC_getOpcodeOper(use);
                    /* If both operands are byte values, the other UQADD8
                     * lanes are zero. Never truncate an unbounded color. */
                    if ((useOp == HCOP_MINU || useOp == HCOP_MINS) &&
                        HC_opcodeHasImm(use) && use->instr1 == 255u &&
                        armUnsignedMax(instrs, pc, info, a, 0) <= 255u &&
                        armUnsignedMax(instrs, pc, info, b, 0) <= 255u)
                    {
                        int dst = HC_getOpcodeDst(use);
                        deferredWords[0] = armUQAdd8Word(dst, a, b);
                        deferredPCs[0] = usePC;
                        deferredCount = 1;
                        deferredIdx = 0;
                        ctx.regMask |= (1u << dst) | (1u << a) | (1u << b);
                        break;
                    }
                }
            }
            if (op == HCOP_AND && HC_opcodeHasImm(instr) && instr->instr1 == 0xFFFFu)
            {
                int rd = HC_getOpcodeDst(instr), rm = HC_getOpcodeSrc0Raw(instr);
                ctx.regMask |= (1u << rd) | (1u << rm);
                ctx.code[ctx.count++] = 0xE6FF0070u | ((unsigned int)rd << 12) | (unsigned int)rm;
                break;
            }
#endif
            armGenALU(&ctx, instr, armOpcodeInfo[op] & 0x1F,
                      (int)((instr->instr0 >> 10) & 0x7FFu));
            break;

        case 3u: /* ASR, LSL, LSR, ROR (+ barrel-shifter consumer fusion) */
#if GERBERA_ARMV6_JIT
            /* An adjacent byte mask needs no deferred slot. An unrelated
             * pending fold (e.g. the fog weight shift) need not block it. */
            if (deferredIdx < deferredCount && deferredPCs[deferredIdx] > pc + 1 &&
                op == HCOP_LSR && HC_opcodeHasImm(instr) &&
                (instr->instr1 == 8u || instr->instr1 == 16u || instr->instr1 == 24u))
            {
                HCInstr_s *use = &instrs[pc + 1];
                int src = HC_getOpcodeSrc0Raw(instr), dst = HC_getOpcodeDst(instr);
                if (HC_getOpcodeOper(use) == HCOP_AND && HC_opcodeHasImm(use) &&
                    use->instr1 == 255u && HC_getOpcodeSrc0Raw(use) == dst &&
                    armSingleUse(instrs, pc, info, dst, -1, -1) == pc + 1)
                {
                    int out = HC_getOpcodeDst(use);
                    ctx.code[ctx.count++] = armUXTBWord(out, src, instr->instr1, 0);
                    ctx.regMask |= (1u << out) | (1u << src);
                    ++pc; /* the usual increment also skips the mask */
                    break;
                }
            }
            if (op == HCOP_LSR &&
                HC_opcodeHasImm(instr) && instr->instr1 > 0 && instr->instr1 < 32u)
            {
                int src = HC_getOpcodeSrc0Raw(instr), dst = HC_getOpcodeDst(instr);
                int usePC = armSingleUse(instrs, pc, info, dst, -1, -1);
                if (usePC >= 0)
                {
                    HCInstr_s *use = &instrs[usePC];
                    int useOp = HC_getOpcodeOper(use), out = HC_getOpcodeDst(use);
                    unsigned int shift = instr->instr1, word = 0;
                    if (HC_opcodeHasImm(use) && HC_getOpcodeSrc0Raw(use) == dst)
                    {
                        if (useOp == HCOP_AND && use->instr1 == 255u &&
                            (shift == 8u || shift == 16u || shift == 24u))
                            word = armUXTBWord(out, src, shift, 0);
                        /* USAT has ASR, not LSR. For a nonnegative input they
                         * agree; otherwise LSR could saturate high, not low. */
                        else if ((useOp == HCOP_MINU || useOp == HCOP_MINS) &&
                                 armSatWidth(use->instr1) != 0 &&
                                 armUnsignedMax(instrs, pc, info, src, 0) <= 0x7FFFFFFFu)
                            word = armUSatWord(out, armSatWidth(use->instr1), src, shift);
                    }
                    if (word != 0 && (out == dst || armSingleUse(instrs, pc, info, dst, src, -1) >= 0) &&
                        armDeferWord(deferredWords, deferredPCs, &deferredCount, &deferredIdx,
                                     maxDeferred, usePC, out == dst ? 0 : word))
                    {
                        /* With an in-place consumer, replace the shift's result
                         * directly: the unclamped value has no other uses. */
                        if (out == dst) ctx.code[ctx.count++] = word;
                        ctx.regMask |= (1u << out) | (1u << src);
                        break;
                    }
                }
            }
#endif
            if (deferredIdx == deferredCount ||
                (GERBERA_ARMV6_JIT && deferredCount - deferredIdx < maxDeferred))
            {
                void *du = HC_getDUChain(info, pc);
                if (du != NULL)
                {
                    int nUses = HCChain_getLength(du);
                    int hasImm = (instr->instr0 & 0x100u) != 0;
                    if (nUses > 0 && nUses <= maxDeferred - (deferredCount - deferredIdx)
                        && !(hasImm != 0 && instr->instr1 == 0))
                    {
                        int shiftDst = (int)((instr->instr0 >> 10) & 0x7FFu);
                        int shiftS0 = (int)(instr->instr0 >> 21);
                        int shiftS1 = hasImm ? -1 : (int)(instr->instr1 & 0x7FFu);
                        int scan = pc + 1;
                        int ok = 1;
                        int u;

                        for (u = 0; u < nUses; ++u)
                        {
                            int usePC = HCChain_getValue(du, u);
                            HCInstr_s *useInstr = &instrs[usePC];
                            int useOp = HC_getOpcodeOper(useInstr);
                            void *ud;
                            int pending;

                            for (pending = deferredIdx; pending < deferredCount; ++pending)
                                if (deferredPCs[pending] == usePC) ok = 0;

                            if (!ok || usePC <= pc || (armOpcodeInfo[useOp] & 0x400u) == 0 ||
                                (useOp == HCOP_BIC && HC_getOpcodeSrc0Raw(useInstr) == shiftDst))
                            {
                                ok = 0;
                                break;
                            }
                            while (scan < usePC)
                            {
                                HCInstr_s *mid = &instrs[scan];
                                int midOp = HC_getOpcodeOper(mid);
                                int midDef = HC_getDef(mid);
                                if (midDef == shiftS0
                                    || (!hasImm && midDef == shiftS1)
                                    || midOp == HCOP_LABEL
                                    || HC_getOpcodeTypeOp(midOp) == HCOP_TYPE_BRANCH)
                                {
                                    ok = 0;
                                    break;
                                }
                                ++scan;
                            }
                            if (!ok)
                                break;
                            ud = HC_getUDChain(info, usePC, shiftDst);
                            if ((useInstr->instr0 & 0x100u) != 0
                                || (HC_getOpcodeSrc0Raw(useInstr) == shiftDst &&
                                    HC_getOpcodeSrc1Raw(useInstr) == shiftDst)
                                || ud == NULL
                                || HCChain_getLength(ud) != 1 || HCChain_getValue(ud, 0) != pc)
                            {
                                ok = 0;
                                break;
                            }
                        }

                        if (ok)
                        {
                            int shiftType = armShiftTypeForOp(op);
                            for (u = 0; u < nUses; ++u)
                            {
                                int usePC = HCChain_getValue(du, u);
                                HCInstr_s *useInstr = &instrs[usePC];
                                int rn = (int)(useInstr->instr0 >> 21);
                                int useOp = HC_getOpcodeOper(useInstr);
                                int useDst = (int)((useInstr->instr0 >> 10) & 0x7FFu);
                                ArmEmitCtx_s tmpCtx;
                                unsigned int word;

                                if (rn == shiftDst)
                                {
                                    rn = (int)(useInstr->instr1 & 0x7FFu);
                                    if (useOp == HCOP_SUB)
                                        useOp = HCOP_SUBR;
                                    else if (useOp == HCOP_SUBR)
                                        useOp = HCOP_SUB;
                                }
                                tmpCtx.code = &word;
                                tmpCtx.count = 0;
                                tmpCtx.regMask = 0;
                                armGenShiftedALU(&tmpCtx, instr,
                                                 armOpcodeInfo[useOp] & 0x1F,
                                                 useDst, rn, shiftS0, shiftType);
                                ctx.regMask |= tmpCtx.regMask;
                                armDeferWord(deferredWords, deferredPCs, &deferredCount,
                                             &deferredIdx, maxDeferred, usePC, word);
                            }
                            break;
                        }
                    }
                }
            }

            if ((instr->instr0 & 0x100u) == 0 || instr->instr1 != 0)
            {
                int shiftType = armShiftTypeForOp(op);
                armGenShiftedALU(&ctx, instr, 13,
                                 (int)((instr->instr0 >> 10) & 0x7FFu),
                                 0, (int)(instr->instr0 >> 21), shiftType);
            }
            else
            {
                int dst = (int)((instr->instr0 >> 10) & 0x7FFu);
                int s0 = (int)(instr->instr0 >> 21);
                if (dst != s0)
                {
                    armEmitDataProc(&ctx, 14, 13,
                                    (unsigned short)((dst << 12) + 16 * s0),
                                    0xFFFFFFFFu, 2);
                }
            }
            break;

#if GERBERA_ARMV6_JIT
        case 4u: /* HCOP_MULS, HCOP_MULU. ARMv6 allows Rd==Rm. */
        {
            int rd = (int)((instr->instr0 >> 10) & 0x7FFu);
            int rm = (int)(instr->instr0 >> 21);
            int rs = (int)(instr->instr1 & 0x7FFu);
            int fused = 0;

            /* Sink MUL into its only ADD use if none of its inputs change.
             * The scheduler may have inserted an unrelated load between them. */
            int addPC = armSingleUse(instrs, pc, info, rd, rm, rs);
            int insert = deferredIdx;
            while (insert < deferredCount && deferredPCs[insert] < addPC)
                ++insert;
            if (addPC > pc && (insert == deferredCount || deferredPCs[insert] != addPC) &&
                (addPC == pc + 1 || deferredCount - deferredIdx < maxDeferred))
            {
                HCInstr_s *add = &instrs[addPC];
                int addS0 = HC_getOpcodeSrc0Raw(add), addS1 = HC_getOpcodeSrc1Raw(add);
                if (HC_getOpcodeOper(add) == HCOP_ADD && !HC_opcodeHasImm(add) &&
                    (addS0 == rd) != (addS1 == rd))
                {
                    int dst = HC_getOpcodeDst(add), acc = (addS0 == rd) ? addS1 : addS0;
                    int use16 = armIsProvably16Bit(instrs, pc, info, rm)
                             && armIsProvably16Bit(instrs, pc, info, rs);
                    unsigned int word = (use16 ? 0xE1000080u : 0xE0200090u)
                        | ((unsigned int)dst << 16) | ((unsigned int)acc << 12)
                        | ((unsigned int)rs << 8) | (unsigned int)rm;
                    ctx.regMask |= (1u << dst) | (1u << rm) | (1u << rs) | (1u << acc);
                    if (addPC == pc + 1)
                    {
                        ctx.code[ctx.count++] = word;
                        ++pc;
                    }
                    else
                    {
                        /* Keep other independent folds. An existing entry for
                         * this ADD would compete for the same machine word. */
                        int i, live = deferredCount - deferredIdx;
                        insert -= deferredIdx;
                        for (i = 0; i < live; ++i)
                        {
                            deferredPCs[i] = deferredPCs[i + deferredIdx];
                            deferredWords[i] = deferredWords[i + deferredIdx];
                        }
                        for (i = live; i > insert; --i)
                        {
                            deferredPCs[i] = deferredPCs[i - 1];
                            deferredWords[i] = deferredWords[i - 1];
                        }
                        deferredPCs[insert] = addPC;
                        deferredWords[insert] = word;
                        deferredIdx = 0;
                        deferredCount = live + 1;
                    }
                    fused = 1;
                }
            }

            if (!fused)
            {
                int use16 = armIsProvably16Bit(instrs, pc, info, rm)
                         && armIsProvably16Bit(instrs, pc, info, rs);
                unsigned int base = use16 ? 0xE1600080u : 0xE0000090u;
                ctx.regMask |= (1u << rd) | (1u << rm) | (1u << (rs & 0xF));
                ctx.code[ctx.count++] = base
                    | ((unsigned int)rd << 16)
                    | ((unsigned int)(rs & 0xF) << 8)
                    | (unsigned int)(rm & 0xF);
            }
            break;
        }

#else
        case 4u: /* HCOP_MULS, HCOP_MULU (ARM `MUL Rd, Rm, Rs`, Rd != Rm) */
        {
            int rd = (int)((instr->instr0 >> 10) & 0x7FFu);
            int rm = (int)(instr->instr0 >> 21);
            int rs = (int)(instr->instr1 & 0x7FFu);
            int needScratch = 0;

            if (rm == rd)
            {
                if (rs == rd)
                {
                    rm = armAllocScratchReg(&scratch, &ctx, 1u << rd);
                    armEmitDataProc(&ctx, 14, 13,
                                    (unsigned short)((rm << 12) + 16 * rd),
                                    0xFFFFFFFFu, 2);
                    needScratch = 1;
                }
                else
                {
                    rs = (int)(instr->instr0 >> 21);
                    rm = (int)(instr->instr1 & 0x7FFu);
                }
            }

            /* 0xE0000090 | (rd << 16) | (rs << 8) | rm (-536870768 == 0xE0000090) */
            ctx.regMask |= (1u << rd) | (1u << rm) | (1u << (rs & 0xF));
            ctx.code[ctx.count++] = 0xE0000090u
                | ((unsigned int)rd << 16)
                | ((unsigned int)(rs & 0xF) << 8)
                | (unsigned int)(rm & 0xF);
            if (needScratch != 0)
                armFreeScratchReg(&scratch, &ctx, rm);
            break;
        }

#endif
        case 5u: /* HCOP_MAXS..HCOP_MINU */
#if GERBERA_ARMV6_JIT
            /* MAXS(x, 0) followed by MIN(x, 2^n-1) is a signed-to-unsigned
             * saturate, including when x is negative (COMBINE SUBTRACT). */
            if (deferredIdx == deferredCount && op == HCOP_MAXS &&
                HC_opcodeHasImm(instr) && instr->instr1 == 0)
            {
                int rd = HC_getOpcodeDst(instr), src = HC_getOpcodeSrc0Raw(instr);
                int usePC = armSingleUse(instrs, pc, info, rd, -1, -1);
                if (usePC >= 0)
                {
                    HCInstr_s *use = &instrs[usePC];
                    int useOp = HC_getOpcodeOper(use);
                    int dst = HC_getOpcodeDst(use), width = armSatWidth(use->instr1);
                    if ((useOp == HCOP_MINU || useOp == HCOP_MINS) &&
                        HC_opcodeHasImm(use) && width != 0 &&
                        (dst == rd || armSingleUse(instrs, pc, info, rd, src, -1) >= 0))
                    {
                        unsigned int word = armUSatWord(dst, width, src, 0);
                        if (dst == rd) ctx.code[ctx.count++] = word;
                        deferredWords[0] = dst == rd ? 0 : word;
                        deferredPCs[0] = usePC;
                        deferredIdx = 0;
                        deferredCount = 1;
                        ctx.regMask |= (1u << dst) | (1u << src);
                        break;
                    }
                }
            }
#endif
            armGenMinMax(&ctx, instr, instrs, pc, info);
            break;

        case 6u: /* HCOP_BRA */
            armEmitBranch(&ctx, 14, (int)((instr->instr0 >> 10) & 0x7FFu));
            break;

        case 7u: /* HCOP_CBA..HCOP_CBNEQ */
            armGenCondBranch(&ctx, instrs, pc, info, recentWords);
            break;

        case 8u: /* Loads & stores */
            armGenLoadStore(&ctx, 14, instr);
            break;

        case 11u: /* HCOP_EOP */
            running = 0;
            break;

        case 12u: /* HCOP_LABEL */
        {
            ArmLabelNode_s *node = (ArmLabelNode_s *)HCArena_allocTop(mgr, (unsigned int)sizeof(ArmLabelNode_s));
            node->next = NULL;
            node->label = (int)instr->instr1;
            node->offset = ctx.count;
            if (labelHead != NULL)
                labelTail->next = node;
            else
                labelHead = node;
            labelTail = node;
            break;
        }

#if GERBERA_ARMV6_JIT
        case 13u: /* ARMv6 packed media instructions */
        {
            int rd = HC_getOpcodeDst(instr), a = HC_getOpcodeSrc0Raw(instr), b = HC_getOpcodeSrc1Raw(instr);
            unsigned int word;
            ctx.regMask |= (1u << rd) | (1u << a);
            if (op == HCOP_UXTB16 || op == HCOP_UXTB_ROR)
            {
                word = armUXTBWord(rd, a, instr->instr1, op == HCOP_UXTB16);
            }
            else
            {
                ctx.regMask |= 1u << b;
                word = op == HCOP_UXTAB16_ROR8 ? 0xE6C00470u :
                       op == HCOP_UQSUB8 ? 0xE6600FF0u : 0xE6600F90u;
                word |= ((unsigned int)a << 16) | ((unsigned int)rd << 12) | (unsigned int)b;
            }
            ctx.code[ctx.count++] = word;
            break;
        }

#endif
        default:
            break;
        }

        ++pc;
    }

    armGrowCodeBuf(mgr, &allocated, 4 * ctx.count + 8);
#if GERBERA_ARMV6_JIT
    /* AAPCS preserves r4-r11. The generated leaf may allocate lr, so save the
     * return address too; caller-saved registers need no stack traffic. */
    {
        unsigned int saved = (ctx.regMask & 0x0FF0u) | 0x4000u;
        ctx.code[0] = 0xE92D0000u | saved;
        armEmitBlockMem(&ctx, 14, 20, 13, (saved & 0x0FF0u) | 0x8000u, 1, 1, 1);
    }
#else
    armEmitBlockMem(&ctx, 14, 20, 13, 0x9FFFu, 1, 1, 1);
#endif

    /* Patch 24-bit signed word offsets into all emitted `B<cond>` words. */
    for (j = 0; j < ctx.count; ++j)
    {
        unsigned int w = ctx.code[j];
        if (((w >> 24) & 0xEu) == 0xAu)
        {
            int labelId = (int)(w & 0x00FFFFFFu);
            unsigned int hi = w & 0xFF000000u;
            ArmLabelNode_s *cur = labelHead;
            int byteDelta = -1;

            while (cur != NULL)
            {
                if (cur->label == labelId)
                {
                    byteDelta = 4 * (cur->offset - j);
                    break;
                }
                cur = cur->next;
            }
            /* ARM 0002542C..00025434 shifts the wrapped 32-bit displacement. */
            ctx.code[j] = hi | ((((unsigned int)byteDelta - 8u) << 6) >> 8);
        }
    }

    HCArena_releaseTop(mgr, topMark);
    return 4 * ctx.count;
}

/* --- vtable & factory (VA 0x36F6C, sub_25484) ----------------------------- */

#if GERBERA_ARMV6_JIT
static int HCARMBackend_optimizationMask(void)
{
    return HC_BACKEND_ARM11_SCHEDULE;
}
#endif

static const HCBackend_s armBackendDecl =
{
    &HCARMBackend_canCreateImmediate,
    &HCARMBackend_internalStackSpace,
    NULL,
    &HCARMBackend_targetRegisterMapping,
    &HCARMBackend_numHardRegs,
    &HCARMBackend_preRegallocFixer,
    &HCARMBackend_codegen,
    NULL,                               /* no ARM postRegallocFixer */
    &HCARMBackend_emulate,
    &HCARMBackend_disasm,               /* VA 0x25480 (`bx lr`)     */
    &HCARMBackend_isNative,
#if GERBERA_ARMV6_JIT
    &HCARMBackend_optimizationMask
#else
    NULL
#endif
};

HCBackend_s *HCARMBackend_create(void)
{
    return (HCBackend_s *)&armBackendDecl;
}
