/* hcCompiler.c - HC IR optimizer, graph-coloring & panic register allocators,
 * and JIT compilation driver (hcCompiler.c + hcPanicRegalloc.inl).
 *
 * Ported from docs/libGLES_CM.dll.c:46514-51794, cross-referenced against
 * raw x86 PE machine code & CodeView NB11 sstGlobalTypes (TI=0x1737,
 * 0x1A7D, 0x1AAB..0x1B70) and ARM32 docs/libgles_cm.dll.epoc.c (compileWrap
 * sub_2C298, HC_compile sub_2C76C).
 */
#include "gerbera/hc/hcCompiler.h"
#include "gerbera/hc/hcAdjList.h"
#include "gerbera/hc/hcAdjMtx.h"
#include "gerbera/hc/hcBucketDefs.h"
#include "gerbera/hc/hcDUChain.h"
#include "gerbera/hc/hcEmulator.h"
#include "gerbera/hc/hcQueue.h"
#include "gerbera/hc/hcStack.h"
#include "hg/hgDefs.h"
#include "hg/hgInt32.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HC_NIL             (-1)
#define HC_MAX_LABEL_INDEX 0x800u
#define HC_MAX_HARD_REGS   32
#define HC_INFINITE_SPCOST 0x7FFFFFFF
enum { HC_ARM11_SPILL_REGISTER_WINDOW = 9 };

/* --- internal types (CodeView NB11 TI=0x1AAB..0x1B70) --------------------- */

/* TI=0x1AAB: interference-graph register metadata entry (8 bytes). */
typedef struct AdjEntry_s
{
    unsigned int spillCost : 31;
    unsigned int spill     : 1;
    unsigned int color     : 6;
    unsigned int disp      : 26;
} AdjEntry_s;

typedef enum HCShiftType_e
{
    SHIFT_LSR = 0,
    SHIFT_ASR = 1
} HCShiftType_e;

/* TI=0x1ABD / 0x1AC0: local copy / shift-right tracking table. */
typedef struct CopyVal_s
{
    unsigned int dst   : 16;
    unsigned int src   : 16;
    unsigned int shift : 6;
    unsigned int type  : 2;
    unsigned int pad   : 24;
} CopyVal_s;

typedef struct CopySet_s
{
    int       nValues;
    CopyVal_s values[1];
} CopySet_s;

/* TI=0x1AB4: live-range union-find node. */
typedef struct LREntry_s
{
    struct LREntry_s *parent;
    int               reg;
} LREntry_s;

/* TI=0x1AE6: scanNumRegs return pair. */
typedef struct RegInstrCount_s
{
    int numRegs;
    int numInstrs;
} RegInstrCount_s;

/* TI=0x1A7D / 0x1B05: packed and unpacked peephole rewrite rules. */
typedef enum PeepholeMask_e
{
    REG_A  = 0,
    REG_B  = 1,
    REG_C  = 2,
    IMM    = 3,
    UNUSED = 4
} PeepholeMask_e;

typedef struct PackedRule_s
{
    unsigned char op;
    unsigned char maskDst;
    unsigned char maskSrc0;
    unsigned char maskSrc1;
    signed char   immediate;
} PackedRule_s;

typedef struct UnpackedRule_s
{
    int op;
    int maskDst;
    int maskSrc0;
    int maskSrc1;
    int immediate;
} UnpackedRule_s;

/* TI=0x1B0D: recognized loop-fill descriptor for peepholeFills. */
typedef struct FillBlock_s
{
    struct FillBlock_s *next;
    int                 startNdx;
    int                 length;
    int                 dst;
    int                 cnt;
    int                 pattern;
    HCOP_e              fillOp;
} FillBlock_s;

/* TI=0x1B1E: computeSpillCosts result bundle. */
typedef struct SpillCostInfo_s
{
    int          numSymRegs;
    AdjEntry_s  *adjLists;
    HCAdjList_s *graph;
} SpillCostInfo_s;

/* TI=0x1B2C: spill load/store insertion record (8 bytes). */
typedef struct SpillEntry_s
{
    unsigned short loc;
    unsigned short reg;
    unsigned short disp;
    unsigned short type; /* 0 = load before instr, 1 = store after instr */
} SpillEntry_s;

/* TI=0x1B39: per-spill-slot symbolic-register list for biased coloring. */
typedef struct SpillRegMap_s
{
    int             numRegs;
    unsigned short *regs;
} SpillRegMap_s;

/* TI=0x1B70: setjmpWrap argument block for compileWrap. */
typedef struct CompParams_s
{
    HCArena_s     *mgr;
    void         **output;
    HCInstr_s     *emuDst;
    HCInstr_s     *instructions;
    HCBackend_s   *backend;
    unsigned int   optimizationMask;
    unsigned char *inputParamsUsed;
    unsigned char *inputParamMutable;
    int            numStackParameters;
} CompParams_s;

/* --- verified static data tables ------------------------------------------ */

/* swap[12] at VA 0x47E50C: negated conditional branch opcodes for op - 41. */
static const unsigned char swap[12] = {
    HCOP_CBNA,  /* 41 HCOP_CBA   -> 51 */
    HCOP_CBNEQ, /* 42 HCOP_CBEQ  -> 52 */
    HCOP_CBLTS, /* 43 HCOP_CBGES -> 49 */
    HCOP_CBLTU, /* 44 HCOP_CBGEU -> 50 */
    HCOP_CBLES, /* 45 HCOP_CBGTS -> 47 */
    HCOP_CBLEU, /* 46 HCOP_CBGTU -> 48 */
    HCOP_CBGTS, /* 47 HCOP_CBLES -> 45 */
    HCOP_CBGTU, /* 48 HCOP_CBLEU -> 46 */
    HCOP_CBGES, /* 49 HCOP_CBLTS -> 43 */
    HCOP_CBGEU, /* 50 HCOP_CBLTU -> 44 */
    HCOP_CBA,   /* 51 HCOP_CBNA  -> 41 */
    HCOP_CBEQ   /* 52 HCOP_CBNEQ -> 42 */
};

/* rewrites[52] at VA 0x47E518: 26 algebraic peephole match/replace pairs. */
static const PackedRule_s rewrites[52] = {
    /*  0 */ { HCOP_ADD,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  1 */ { HCOP_AND,  REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  2 */ { HCOP_AND,  REG_A, REG_B, IMM,   -1 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  3 */ { HCOP_AND,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 },
    /*  4 */ { HCOP_ASR,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  5 */ { HCOP_LSL,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  6 */ { HCOP_LSR,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  7 */ { HCOP_ROL,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  8 */ { HCOP_ROR,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /*  9 */ { HCOP_MAXS, REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 10 */ { HCOP_MAXU, REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 11 */ { HCOP_MINS, REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 12 */ { HCOP_MINU, REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 13 */ { HCOP_MULS, REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 },
    /* 14 */ { HCOP_MULU, REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 },
    /* 15 */ { HCOP_MULS, REG_A, REG_B, IMM,    1 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 16 */ { HCOP_MULU, REG_A, REG_B, IMM,    1 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 17 */ { HCOP_MULS, REG_A, REG_B, IMM,   -1 }, { HCOP_SUBR, REG_A, REG_B, IMM,     0 },
    /* 18 */ { HCOP_OR,   REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 19 */ { HCOP_OR,   REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 20 */ { HCOP_OR,   REG_A, REG_B, IMM,   -1 }, { HCOP_MOV,  REG_A, IMM,   UNUSED, -1 },
    /* 21 */ { HCOP_SUB,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 22 */ { HCOP_SUB,  REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 },
    /* 23 */ { HCOP_SUBR, REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 },
    /* 24 */ { HCOP_XOR,  REG_A, REG_B, IMM,    0 }, { HCOP_MOV,  REG_A, REG_B, UNUSED,  0 },
    /* 25 */ { HCOP_XOR,  REG_A, REG_B, REG_B,  0 }, { HCOP_MOV,  REG_A, IMM,   UNUSED,  0 }
};

/* --- forward declarations for mutually calling internal passes ------------ */
static int  swapToLowMem(HCArena_s *mgr, HCInstr_s **dst, HCInstr_s *lst);
static int  swapToHiMem(HCArena_s *mgr, HCInstr_s **dst, HCInstr_s *lst);
static void removeUnreachableCode(HCArena_s *mgr, HCInstr_s *lst);
static void computeDUChains(HCArena_s *mgr, hcChains_s *dst,
                            BasicBlock_s *basicBlocks, int numBlocks,
                            HCInstr_s *lst, unsigned int duUdMask);

/* --- basic entry & CopySet helpers ---------------------------------------- */

static void AdjEntry_init(AdjEntry_s *self)
{
    memset(self, 0, sizeof(*self));
    self->color = 63u;
    self->disp  = 0x3FFFFFFu;
}

static void AdjEntry_setSpillCost(AdjEntry_s *self, int cost)
{
    if ((unsigned int)cost > 0x7FFFFFFFu)
        HC_assertFail("HC_IN_RANGE(cost, 0, (1u<<31)-1u)", "hcCompiler.c", 177, NULL);
    self->spillCost = (unsigned int)cost;
}

static int AdjEntry_getSpillCost(const AdjEntry_s *self)
{
    return (int)self->spillCost;
}

static void LREntry_init(LREntry_s *self)
{
    self->parent = NULL;
    self->reg    = -1;
}

static CopySet_s *CopySet_create(HCArena_s *mgr)
{
    CopySet_s *self = (CopySet_s *)HCArena_current(mgr);
    HCArena_growCurrent(mgr, (unsigned int)offsetof(CopySet_s, values));
    self->nValues = 0;
    return self;
}

static CopyVal_s *CopySet_vals(CopySet_s *cpSet)
{
    return (CopyVal_s *)((char *)cpSet + offsetof(CopySet_s, values));
}

static void CopySet_addCopyGeneric(HCArena_s *mgr, CopySet_s *cpSet,
                                   int dst, int src, int shift, HCShiftType_e ty)
{
    CopyVal_s *vals = CopySet_vals(cpSet);
    int nVals = cpSet->nValues;
    int i;

    for (i = 0; i < nVals; ++i)
    {
        if (vals[i].dst == 0xFFFFu)
        {
            vals[i].dst   = (unsigned int)dst;
            vals[i].src   = (unsigned int)src;
            vals[i].shift = (unsigned int)shift;
            vals[i].type  = (unsigned int)ty;
            return;
        }
    }
    HCArena_growCurrent(mgr, (unsigned int)sizeof(CopyVal_s));
    vals[nVals].dst   = (unsigned int)dst;
    vals[nVals].src   = (unsigned int)src;
    vals[nVals].shift = (unsigned int)shift;
    vals[nVals].type  = (unsigned int)ty;
    vals[nVals].pad   = 0;
    ++cpSet->nValues;
}

static void CopySet_addCopy(HCArena_s *mgr, CopySet_s *cpSet, int dst, int src)
{
    CopySet_addCopyGeneric(mgr, cpSet, dst, src, 0, SHIFT_LSR);
}

static void CopySet_addShiftRight(HCArena_s *mgr, CopySet_s *cpSet,
                                  int dst, int src, int shift, HCShiftType_e type)
{
    CopySet_addCopyGeneric(mgr, cpSet, dst, src, shift, type);
}

static CopyVal_s *CopySet_valueGeneric(CopySet_s *cpSet, int reg)
{
    CopyVal_s *vals = CopySet_vals(cpSet);
    int i;

    for (i = 0; i < cpSet->nValues; ++i)
    {
        if (reg == (int)vals[i].dst)
            return &vals[i];
    }
    return NULL;
}

static int CopySet_copyValue(CopySet_s *cpSet, int reg)
{
    CopyVal_s *v = CopySet_valueGeneric(cpSet, reg);
    return (v != NULL) ? (int)v->src : reg;
}

static void CopySet_remove(CopySet_s *cpSet, int reg)
{
    CopyVal_s *vals = CopySet_vals(cpSet);
    int i;

    for (i = 0; i < cpSet->nValues; ++i)
    {
        if (reg == (int)vals[i].dst || reg == (int)vals[i].src)
        {
            vals[i].dst = 0xFFFFu;
            vals[i].src = 0xFFFFu;
        }
    }
}

static HCBitSet_s **allocBitSetArray(HCMemory_s *mem, int nSets, int size)
{
    HCBitSet_s **arr;
    int i;

    arr = (HCBitSet_s **)HCMemory_alloc(
        mem, (unsigned int)((size_t)nSets * sizeof(HCBitSet_s *)));
    for (i = 0; i < nSets; ++i)
        arr[i] = HCBitSet_create(mem, size);
    return arr;
}

static int contains(const unsigned short *arr, int length, int v)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        if (v == (int)arr[i])
            return 1;
    }
    return 0;
}

int HC_containsInt(const int *arr, int length, int v)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        if (v == arr[i])
            return 1;
    }
    return 0;
}

static HCInstr_s genSpillStore(int hardReg, int loc)
{
    return HC_genOp3Imm(HCOP_SD, hardReg, 0, loc);
}

static HCInstr_s genSpillLoad(int hardReg, int loc)
{
    return HC_genOp3Imm(HCOP_LD, hardReg, 0, loc);
}

int HC_isSpillStore(HCInstr_s *instr)
{
    return HC_getOpcodeOper(instr) == HCOP_SD
        && HC_getOpcodeSrc0(instr) == 0
        && HC_opcodeHasImm(instr) != 0;
}

int HC_isSpillLoad(HCInstr_s *instr)
{
    return HC_getOpcodeOper(instr) == HCOP_LD
        && HC_getOpcodeSrc0(instr) == 0
        && HC_opcodeHasImm(instr) != 0;
}

static int isInvariantLoad(HCInstr_s *instr, int variantEnd)
{
    if (HC_isSpillLoad(instr) != 0)
        return HC_getOpcodeImm(instr) < variantEnd;
    return 0;
}

static int isInstrNOP(HCInstr_s *instr)
{
    int op = HC_getOpcodeOper(instr);
    if (op == HCOP_NOP)
        return 1;
    if (op != HCOP_MOV || HC_opcodeHasImm(instr) != 0)
        return 0;
    return HC_getOpcodeDst(instr) == HC_getOpcodeSrc0(instr);
}

static void compactInstructionList(HCInstr_s *lst)
{
    int i = 0;
    int newCount = 0;
    int prev;

    do
    {
        if (isInstrNOP(&lst[i]) == 0)
            lst[newCount++] = lst[i];
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
}

#if HG_ASSERT_ENABLED
static int findLabel(HCInstr_s *lst, int label)
{
    int i = 0;
    int prev;

    do
    {
        if (HC_getOpcodeOper(&lst[i]) == HCOP_LABEL)
        {
            unsigned int lbl = (unsigned int)HC_getOpcodeLabel(&lst[i]);
            if (lbl > HC_MAX_LABEL_INDEX)
                HC_assertFail("HC_IN_RANGE(lbl, 0, HC_MAX_LABEL_INDEX)", "hcCompiler.c", 454, NULL);
            if ((int)lbl == label)
                return i;
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    return -1;
}
#endif

static RegInstrCount_s scanNumRegs(HCInstr_s *lst)
{
    RegInstrCount_s res;
    int maxReg = 0;
    int nInstrs = 0;
    int uses[4];
    int prev;

    do
    {
        HCInstr_s *instr = &lst[nInstrs];
        int nUses = HC_getUses(instr, uses);
        int def;
        int j;

        for (j = 0; j < nUses; ++j)
            maxReg = HC_max(maxReg, uses[j]);
        def = HC_getDef(instr);
        maxReg = HC_max(maxReg, def);
        prev = nInstrs++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    res.numRegs   = maxReg + 1;
    res.numInstrs = nInstrs;
    return res;
}

static int scanMaxLabel(HCInstr_s *lst)
{
    int i = 0;
    int maxLabel = 0;
    int prev;

    do
    {
        if (HC_getOpcodeOper(&lst[i]) == HCOP_LABEL)
        {
            int lbl = HC_getOpcodeLabel(&lst[i]);
            if (maxLabel < lbl)
                maxLabel = lbl;
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    return maxLabel + 1;
}

static int swapToLowMem(HCArena_s *mgr, HCInstr_s **dst, HCInstr_s *lst)
{
    int mark = HCArena_markBottom(mgr);
    HCInstr_s *d = (HCInstr_s *)HCArena_current(mgr);
    int i = 0;
    int prev;

    *dst = d;
    do
    {
        HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
        d[i] = lst[i];
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    return mark;
}

static int swapToHiMem(HCArena_s *mgr, HCInstr_s **dst, HCInstr_s *lst)
{
    int nInstrs = scanNumRegs(lst).numInstrs;
    int mark = HCArena_markTop(mgr);
    unsigned int bytes = (unsigned int)nInstrs * (unsigned int)sizeof(HCInstr_s);

    *dst = (HCInstr_s *)HCArena_allocTop(mgr, bytes);
    memcpy(*dst, lst, bytes);
    return mark;
}

#if HG_ASSERT_ENABLED
static void validateProgramInternal(HCArena_s *mgr, int nHardRegs, HCInstr_s *src)
{
    HCMark_s mark = HCArena_mark(mgr);
    HCInstr_s *lst = NULL;
    HCMemory_s botMem;
    HCBitSet_s *labels;
    BasicBlock_s *blocks = NULL;
    int nBlocks;
    hcChains_s chains;
    int uses[4];
    int i;
    int prev;

    swapToLowMem(mgr, &lst, src);
    botMem = HCArena_bottomInterface(mgr);
    labels = HCBitSet_create(&botMem, 2048);

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_getOpcodeOper(instr) == HCOP_LABEL)
        {
            int lbl = HC_getOpcodeLabel(instr);
            if (HCBitSet_contains(labels, lbl) != 0)
                HC_assertFail("!HCBitSet_contains(labels, lbl) && \"Duplicate labels!\"",
                              "hcCompiler.c", 501, NULL);
            HCBitSet_or(labels, lbl);
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    removeUnreachableCode(mgr, lst);
    nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 2u);

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);
        UDChainStub_s *perUse;
        int nUses;
        int u;

        switch (op)
        {
        case HCOP_ASR:
        case HCOP_LSL:
        case HCOP_LSR:
        case HCOP_ROL:
        case HCOP_ROR:
            if (HC_opcodeHasImm(instr) != 0)
            {
                unsigned int imm = (unsigned int)HC_getOpcodeImm(instr);
                if (imm > 31u)
                    HC_assertFail("HC_IN_RANGE(imm, 0, 31)", "hcCompiler.c", 531, NULL);
            }
            break;
        default:
            break;
        }

        if (HC_isBranchOp(op) != 0)
        {
            int target = HC_getOpcodeDst(instr);
            /* The reference rejects a label at instruction index zero
             * (x86 0x4537CB), even though it is a valid index. */
            if (findLabel(lst, target) == 0)
                HC_assertFail("findLabel(lst, target)", "hcCompiler.c", 542, NULL);
        }

        perUse = chains.udChains[i];
        nUses = HC_getUses(instr, uses);
        for (u = 0; u < nUses; ++u)
        {
            if (uses[u] >= nHardRegs)
            {
                DUChain_s *ud = (perUse != NULL) ? perUse->chains[u] : NULL;
                if (ud == NULL)
                    HC_assertFail("ud && \"Referencing an uninitialized variable!\"",
                                  "hcCompiler.c", 563, NULL);
                if (ud->length == 0)
                    HC_assertFail("ud->length != 0", "hcCompiler.c", 564, NULL);
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    HCArena_release(mgr, &mark);
}

static void validateProgram(HCArena_s *mgr, int nHardRegs, HCInstr_s *lst)
{
    validateProgramInternal(mgr, nHardRegs, lst);
}
#else
/* Validation builds temporary analyses solely for the assertions above. */
#define validateProgram(mgr, nHardRegs, lst) ((void)0)
#endif

/* --- CFG, dominators, loop nesting, liveness, DU/UD chains ---------------- */

static int findBasicBlockByLabel(HCInstr_s *lst, BasicBlock_s *blocks,
                                 int nBlocks, int label)
{
    int i;
    for (i = 0; i < nBlocks; ++i)
    {
        HCInstr_s *instr = &lst[BasicBlock_getStart(&blocks[i])];
        if (HC_getOpcodeOper(instr) == HCOP_LABEL
            && label == HC_getOpcodeLabel(instr))
        {
            return i;
        }
    }
    HC_assertFail("!\"Couldn't find a label!\"", "hcCompiler.c", 672, NULL);
    return -1;
}

static int findBasicBlockByPos(BasicBlock_s *blocks, int nBlocks, int pos)
{
    int i;
    for (i = 0; i < nBlocks; ++i)
    {
        int start = BasicBlock_getStart(&blocks[i]);
        if ((unsigned int)(pos - start) <= (unsigned int)(BasicBlock_getEnd(&blocks[i]) - start))
            return i;
    }
    HC_assertFail("!\"Compiler::findBasicBlockByPos: Couldn't find a label!\"",
                  "hcCompiler.c", 696, NULL);
    return -1;
}

int HC_buildBasicBlocks(HCArena_s *mgr, BasicBlock_s **result, HCInstr_s *lst)
{
    BasicBlock_s *blocks = (BasicBlock_s *)HCArena_current(mgr);
    int curBlock = 0;
    int prevWasBranch = 0;
    int i = 0;
    int prev;
    int nInstrs;
    int nBlocks;
    int pass;
    int b;

    HCArena_growCurrent(mgr, (unsigned int)sizeof(BasicBlock_s));
    BasicBlock_init(&blocks[0]);

    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);

        if ((i != 0 && op == HCOP_LABEL) || prevWasBranch != 0)
        {
            ++curBlock;
            HCArena_growCurrent(mgr, (unsigned int)sizeof(BasicBlock_s));
            BasicBlock_init(&blocks[curBlock]);
            BasicBlock_setStart(&blocks[curBlock], i);
            prevWasBranch = HC_isBranchOp(op);
        }
        else
        {
            prevWasBranch = (HC_isBranchOp(op) != 0);
        }
        BasicBlock_setEnd(&blocks[curBlock], i);
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    nInstrs = i;
    nBlocks = curBlock + 1;

    for (pass = 0; pass < 2; ++pass)
    {
        for (b = 0; b < nBlocks; ++b)
        {
            BasicBlock_s *bb = &blocks[b];
            if (pass == 0 || bb->succ != NULL)
            {
                int endPos = BasicBlock_getEnd(bb);
                HCInstr_s *endInstr = &lst[endPos];
                int targets[2];
                int nTargets = 0;
                int t;

                if (endPos != nInstrs - 1 && HC_isAbsBranch(endInstr) == 0)
                    targets[nTargets++] = findBasicBlockByPos(blocks, nBlocks, endPos + 1);
                if (HC_isBranch(endInstr) != 0)
                    targets[nTargets++] = findBasicBlockByLabel(
                        lst, blocks, nBlocks, HC_getOpcodeDst(endInstr));

                for (t = 0; t < nTargets; ++t)
                {
                    if (pass != 0)
                    {
                        int succBlk = targets[t];
                        if (bb->numSuccs == 0)
                        {
                            unsigned int cnt = (unsigned int)(uintptr_t)bb->succ;
                            bb->succ = (unsigned char *)HCArena_allocBottom(mgr, cnt);
                        }
                        bb->succ[bb->numSuccs++] = (unsigned char)succBlk;
                        blocks[succBlk].pred =
                            (unsigned char *)((uintptr_t)blocks[succBlk].pred + 1u);
                    }
                    else
                    {
                        bb->succ = (unsigned char *)((uintptr_t)bb->succ + 1u);
                    }
                }
            }
        }
    }

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int s;
        for (s = 0; s < (int)bb->numSuccs; ++s)
        {
            BasicBlock_s *succBB = &blocks[bb->succ[s]];
            if (succBB->numPreds == 0)
            {
                unsigned int cnt = (unsigned int)(uintptr_t)succBB->pred;
                succBB->pred = (unsigned char *)HCArena_allocBottom(mgr, cnt);
            }
            succBB->pred[succBB->numPreds++] = (unsigned char)b;
        }
    }

    *result = blocks;
    return nBlocks;
}

static void computeDominators(HCArena_s *mgr, BasicBlock_s *basicBlocks, int numBlocks)
{
    HCMemory_s botMem = HCArena_bottomInterface(mgr);

    if (numBlocks > 2)
    {
        HCMemory_s topMem = HCArena_topInterface(mgr);
        int topMark = HCMemory_mark(&topMem);
        HCBitSet_s *inter = HCBitSet_create(&topMem, numBlocks);
        HCBitSet_s *cand  = HCBitSet_create(&topMem, numBlocks);
        HCBitSet_s **doms = allocBitSetArray(&botMem, numBlocks, numBlocks);
        int i;
        int changed;

        for (i = 1; i < numBlocks; ++i)
            HCBitSet_set(doms[i]);
        HCBitSet_or(doms[0], 0);

        /* The reference repeats only when nothing changed (x86 0x454144).
         * This likely inverted condition can stop before convergence. */
        do
        {
            changed = 0;
            for (i = 1; i < numBlocks; ++i)
            {
                BasicBlock_s *bb = &basicBlocks[i];
                int p;

                HCBitSet_set(inter);
                for (p = 0; p < (int)bb->numPreds; ++p)
                    HCBitSet_intersect(inter, doms[bb->pred[p]]);
                HCBitSet_copy(cand, inter);
                HCBitSet_or(cand, i);
                if (HCBitSet_equal(cand, doms[i]) == 0)
                {
                    changed = 1;
                    HCBitSet_copy(doms[i], cand);
                }
            }
        }
        while (changed == 0);

        for (i = 0; i < numBlocks; ++i)
            BasicBlock_setDominators(&basicBlocks[i], doms[i]);
        HCMemory_release(&topMem, topMark);
    }
    else
    {
        HCBitSet_s *d0;
        if (numBlocks <= 0)
            HC_assertFail("numBlocks > 0", "hcCompiler.c", 854, NULL);
        d0 = HCBitSet_create(&botMem, numBlocks);
        HCBitSet_or(d0, 0);
        BasicBlock_setDominators(&basicBlocks[0], d0);
        if (numBlocks == 2)
        {
            HCBitSet_s *d1 = HCBitSet_create(&botMem, 2);
            HCBitSet_or(d1, 0);
            HCBitSet_or(d1, 1);
            BasicBlock_setDominators(&basicBlocks[1], d1);
        }
    }
}

static void computeLoopNesting(HCArena_s *mgr, BasicBlock_s *basicBlocks, int numBlocks)
{
    int loopId = 0;
    HCMark_s outerMark = HCArena_mark(mgr);
    int i;

    for (i = 0; i < numBlocks; ++i)
    {
        if (basicBlocks[i].dominators == NULL)
            HC_assertFail("basicBlocks[i].dominators", "hcCompiler.c", 930, NULL);
        basicBlocks[i].nesting = 0;
    }

    if (numBlocks > 2)
    {
        if (HCBitSet_getFirst(basicBlocks[0].dominators) != 0)
            HC_assertFail("HCBitSet_getFirst(basicBlocks[0].dominators) == 0",
                          "hcCompiler.c", 938, NULL);

        for (i = 0; i < numBlocks; ++i)
        {
            BasicBlock_s *bb = &basicBlocks[i];
            int s;

            for (s = 0; s < (int)bb->numSuccs; ++s)
            {
                int tail   = i;
                int header = bb->succ[s];
                HCMark_s innerMark = HCArena_mark(mgr);
                HCMemory_s botMem  = HCArena_bottomInterface(mgr);

                if (HCBitSet_contains(basicBlocks[tail].dominators, header) != 0)
                {
                    HCBitSet_s *loopNodes = HCBitSet_create(&botMem, numBlocks);
                    HCStack_s  *stack     = HCStack_create(mgr);
                    int k;

                    HCBitSet_or(loopNodes, tail);
                    HCBitSet_or(loopNodes, header);
                    if (tail != header)
                        HCStack_push(mgr, stack, tail);

                    while (HCStack_isEmpty(stack) == 0)
                    {
                        int cur = HCStack_pop(stack);
                        BasicBlock_s *curBB = &basicBlocks[cur];
                        int p;
                        for (p = 0; p < (int)curBB->numPreds; ++p)
                        {
                            int pred = curBB->pred[p];
                            if (HCBitSet_contains(loopNodes, pred) == 0)
                            {
                                HCBitSet_or(loopNodes, pred);
                                HCStack_push(mgr, stack, pred);
                            }
                        }
                    }

                    if (loopId >= 32)
                        HC_assertFail("loopId < 32", "hcCompiler.c", 980, NULL);

                    for (k = HCBitSet_getFirst(loopNodes);
                         k != -1;
                         k = HCBitSet_getNext(loopNodes, k))
                    {
                        if (k >= numBlocks)
                            HC_assertFail("k < numBlocks", "hcCompiler.c", 984, NULL);
                        ++basicBlocks[k].nesting;
                        basicBlocks[k].loopGroup |= (1u << loopId);
                    }
                    ++loopId;
                }
                HCArena_release(mgr, &innerMark);
            }
        }
        HCArena_release(mgr, &outerMark);
    }
}

static void removeUnreachableCode(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    HCBitSet_s *visited = HCBitSet_create(&botMem, nBlocks);
    HCStack_s *stack = HCStack_create(mgr);
    int modified = 0;
    int b;

    HCStack_push(mgr, stack, 0);
    do
    {
        int cur = HCStack_pop(stack);
        if (HCBitSet_contains(visited, cur) == 0)
        {
            BasicBlock_s *bb = &blocks[cur];
            int s;
            HCBitSet_or(visited, cur);
            for (s = 0; s < (int)bb->numSuccs; ++s)
                HCStack_push(mgr, stack, bb->succ[s]);
        }
    }
    while (HCStack_isEmpty(stack) == 0);

    for (b = 0; b < nBlocks; ++b)
    {
        if (HCBitSet_contains(visited, b) == 0)
        {
            int start = BasicBlock_getStart(&blocks[b]);
            int end   = BasicBlock_getEnd(&blocks[b]);
            int j;
            for (j = start; j <= end; ++j)
                lst[j] = HC_genOp0(HCOP_NOP);
            modified = 1;
        }
    }
    if (modified != 0)
        compactInstructionList(lst);
    HCArena_release(mgr, &mark);
}

static void liveness(HCArena_s *mgr, BasicBlock_s *basicBlocks,
                     int numBlocks, int nSymRegs, HCInstr_s *lst)
{
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    int numRegs;
    int mark;
    HCBitSet_s *outTmp;
    HCBitSet_s *inTmp;
    int changed;
    int i;
    int uses[4];

    if (nSymRegs != 0)
    {
        int n = scanNumRegs(lst).numRegs;
        if (nSymRegs < n)
            HC_assertFail("nSymRegs >= n", "hcCompiler.c", 1100, NULL);
        numRegs = nSymRegs;
    }
    else
    {
        numRegs = scanNumRegs(lst).numRegs;
    }

    for (i = 0; i < numBlocks; ++i)
    {
        basicBlocks[i].liveIn  = HCBitSet_create(&botMem, numRegs);
        basicBlocks[i].liveOut = HCBitSet_create(&botMem, numRegs);
    }

    mark   = HCMemory_mark(&botMem);
    outTmp = HCBitSet_createNoClear(&botMem, numRegs);
    inTmp  = HCBitSet_createNoClear(&botMem, numRegs);

    do
    {
        changed = 0;
        for (i = numBlocks - 1; i >= 0; --i)
        {
            BasicBlock_s *bb = &basicBlocks[i];
            int start = BasicBlock_getStart(bb);
            int end   = BasicBlock_getEnd(bb);
            int s;
            int j;

            HCBitSet_clear(outTmp);
            for (s = 0; s < (int)bb->numSuccs; ++s)
                HCBitSet_union(outTmp, basicBlocks[bb->succ[s]].liveIn);
            HCBitSet_copy(inTmp, outTmp);

            for (j = end; j >= start; --j)
            {
                HCInstr_s *instr = &lst[j];
                int nUses = HC_getUses(instr, uses);
                int def   = HC_getDef(instr);
                int u;

                if (def != -1)
                    HCBitSet_sub(inTmp, def);
                for (u = 0; u < nUses; ++u)
                    HCBitSet_or(inTmp, uses[u]);
            }

            if (HCBitSet_equal(bb->liveIn, inTmp) == 0
                || HCBitSet_equal(bb->liveOut, outTmp) == 0)
            {
                changed = 1;
                HCBitSet_copy(bb->liveIn, inTmp);
                HCBitSet_copy(bb->liveOut, outTmp);
            }
        }
    }
    while (changed != 0);

    HCMemory_release(&botMem, mark);
}

static void reachingDefinitions(HCMemory_s *mem, BasicBlock_s *basicBlocks,
                                int numBlocks, HCBucketDefs_s *defs,
                                int numInstrs, HCInstr_s *lst)
{
    int mark;
    HCBitSet_s *outTmp;
    HCBitSet_s *inTmp;
    int changed;
    int i;

    for (i = 0; i < numBlocks; ++i)
    {
        basicBlocks[i].reachIn  = HCBitSet_create(mem, numInstrs);
        basicBlocks[i].reachOut = HCBitSet_create(mem, numInstrs);
    }

    mark   = HCMemory_mark(mem);
    outTmp = HCBitSet_createNoClear(mem, numInstrs);
    inTmp  = HCBitSet_createNoClear(mem, numInstrs);

    for (i = 0; i < numBlocks; ++i)
    {
        BasicBlock_s *bb = &basicBlocks[i];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        HCBitSet_s *killMask = HCBitSet_createNoClear(mem, numInstrs);
        HCBitSet_s *genSet   = HCBitSet_create(mem, numInstrs);
        int j;

        bb->prsv = killMask;
        bb->gen  = genSet;
        HCBitSet_set(killMask);

        for (j = start; j <= end; ++j)
        {
            int def = HC_getDef(&lst[j]);
            if (def != -1)
            {
                int nVals = HCBucketDefs_numValues(defs, def);
                int k;
                HCBucketDefs_sub(defs, killMask, def);
                for (k = 0; k < nVals; ++k)
                    HCBitSet_sub(genSet, HCBucketDefs_getValue(defs, def, k));
                HCBitSet_or(genSet, j);
            }
        }
    }

    do
    {
        changed = 0;
        for (i = 0; i < numBlocks; ++i)
        {
            BasicBlock_s *bb = &basicBlocks[i];
            int p;

            HCBitSet_clear(inTmp);
            for (p = 0; p < (int)bb->numPreds; ++p)
                HCBitSet_union(inTmp, basicBlocks[bb->pred[p]].reachOut);
            HCBitSet_copy(outTmp, inTmp);
            HCBitSet_intersect(outTmp, bb->prsv);
            HCBitSet_union(outTmp, bb->gen);

            if (HCBitSet_equal(bb->reachIn, inTmp) == 0
                || HCBitSet_equal(bb->reachOut, outTmp) == 0)
            {
                changed = 1;
                HCBitSet_copy(bb->reachIn, inTmp);
                HCBitSet_copy(bb->reachOut, outTmp);
            }
        }
    }
    while (changed != 0);

    HCMemory_release(mem, mark);
}

static void updateDefUseChain(HCMemory_s *mem, hcChains_s *duChainsDst,
                              unsigned short *duChains, int pass,
                              HCBitSet_s *defVisit, int usePos, int defPos)
{
    if (pass != 0)
    {
        DUChain_s *c;
        if (HCBitSet_contains(defVisit, defPos) == 0)
        {
            DUChain_s *created = DUChain_create(mem, duChains[defPos]);
            ptrdiff_t diff = (const unsigned short *)duChains - (const unsigned short *)created;
            if (diff < 0 || diff > 0xFFFF)
                HC_assertFail("HC_IN_RANGE(ptr, 0, 0xFFFF)", "hcCompiler.c", 1290, NULL);
            duChains[defPos] = (unsigned short)diff;
            HCBitSet_or(defVisit, defPos);
        }
        c = (DUChain_s *)DUChains_getDUChain(duChainsDst, defPos);
        c->values[c->length++] = (unsigned short)usePos;
    }
    else
    {
        ++duChains[defPos];
    }
}

static void updateUseDefChain(HCMemory_s *mem, UDChainStub_s **udChains,
                              int pass, HCBitSet_s *useVisit,
                              int instrNdx, int useNdx, int useId, int defPos)
{
    DUChain_s **slots = udChains[instrNdx]->chains;

    if (pass != 0)
    {
        DUChain_s *ud;
        if (pass != 1)
            HC_assertFail("pass == 1", "hcCompiler.c", 1314, NULL);
        if (HCBitSet_contains(useVisit, useId) == 0)
        {
            int count = (int)(uintptr_t)slots[useNdx];
            slots[useNdx] = DUChain_create(mem, count);
            HCBitSet_or(useVisit, useId);
        }
        ud = slots[useNdx];
        if (contains(ud->values, ud->length, defPos) != 0)
            HC_assertFail("!contains(ud->values, ud->length, defPos)", "hcCompiler.c", 1323, NULL);
        ud->values[ud->length++] = (unsigned short)defPos;
    }
    else
    {
        slots[useNdx] = (DUChain_s *)((uintptr_t)slots[useNdx] + 1u);
    }
}

static void computeDUChains(HCArena_s *mgr, hcChains_s *dst,
                            BasicBlock_s *basicBlocks, int numBlocks,
                            HCInstr_s *lst, unsigned int duUdMask)
{
    int perOperandDU = (duUdMask & 4u) != 0;
    HCMemory_s topMem = HCArena_topInterface(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    int botMark = HCMemory_mark(&botMem);
    RegInstrCount_s counts = scanNumRegs(lst);
    int numInstrs = counts.numInstrs;
    int numRegs   = counts.numRegs;
    unsigned short *duChains = NULL;
    UDChainStub_s **udChains = NULL;
    HCBitSet_s *reachSet  = HCBitSet_createNoClear(&botMem, numInstrs);
    HCBucketDefs_s *bdefs = HCBucketDefs_create(&botMem, lst, numRegs, numInstrs);
    HCBitSet_s *defVisit  = HCBitSet_create(&botMem, numInstrs);
    HCBitSet_s *useVisit  = HCBitSet_create(&botMem, 3 * numInstrs);
    int uses[4];
    int pass;
    int b;
    int i;

    reachingDefinitions(&botMem, basicBlocks, numBlocks, bdefs, numInstrs, lst);

    if ((duUdMask & 2u) != 0)
    {
        unsigned int bytes = (unsigned int)((size_t)numInstrs * sizeof(UDChainStub_s *));
        udChains = (UDChainStub_s **)HCMemory_alloc(&topMem, bytes);
        if (((uintptr_t)udChains & 3u) != 0)
            HC_assertFail("(((HCuintptr)udChains) & 3) == 0", "hcCompiler.c", 1391, NULL);
        memset(udChains, 0, bytes);
        for (i = 0; i < numInstrs; ++i)
        {
            int nUses = HC_getUses(&lst[i], uses);
            if (nUses != 0)
                udChains[i] = UDChainStub_create(&topMem, nUses);
        }
    }

    if ((duUdMask & 1u) != 0)
    {
        unsigned int bytes = 2u * (unsigned int)numInstrs;
        duChains = (unsigned short *)HCMemory_alloc(&topMem, bytes);
        if (((uintptr_t)duChains & 3u) != 0)
            HC_assertFail("(((HCuintptr)duChains) & 3) == 0", "hcCompiler.c", 1408, NULL);
        memset(duChains, 0, bytes);
    }

    dst->duChains = duChains;
    dst->udChains = udChains;

    for (pass = 0; pass < 2; ++pass)
    {
        for (b = 0; b < numBlocks; ++b)
        {
            BasicBlock_s *bb = &basicBlocks[b];
            int start = BasicBlock_getStart(bb);
            int end   = BasicBlock_getEnd(bb);
            int pos;

            HCBitSet_copy(reachSet, bb->reachIn);
            for (pos = start; pos <= end; ++pos)
            {
                HCInstr_s *instr = &lst[pos];
                int def;

                if ((duUdMask & 1u) != 0)
                {
                    int nUses = (perOperandDU != 0)
                        ? HC_getRegUses(uses, instr)
                        : HC_getUses(instr, uses);
                    int u;
                    for (u = 0; u < nUses; ++u)
                    {
                        int reg = uses[u];
                        int nDefs = HCBucketDefs_numValues(bdefs, reg);
                        int k;
                        for (k = 0; k < nDefs; ++k)
                        {
                            int defPos = HCBucketDefs_getValue(bdefs, reg, k);
                            if (HCBitSet_contains(reachSet, defPos) != 0)
                            {
                                int usePos = (perOperandDU != 0) ? (3 * pos + u) : pos;
                                updateDefUseChain(&topMem, dst, duChains, pass,
                                                  defVisit, usePos, defPos);
                            }
                        }
                    }
                }

                if ((duUdMask & 2u) != 0)
                {
                    int nUses = HC_getUses(instr, uses);
                    int u;
                    for (u = 0; u < nUses; ++u)
                    {
                        int useId = 3 * pos + u;
                        int reg = uses[u];
                        int nDefs = HCBucketDefs_numValues(bdefs, reg);
                        int k;
                        for (k = 0; k < nDefs; ++k)
                        {
                            int defPos = HCBucketDefs_getValue(bdefs, reg, k);
                            if (HCBitSet_contains(reachSet, defPos) != 0)
                            {
                                updateUseDefChain(&topMem, udChains, pass,
                                                  useVisit, pos, u, useId, defPos);
                            }
                        }
                    }
                }

                def = HC_getDef(instr);
                if (def != -1)
                {
                    HCBucketDefs_sub(bdefs, reachSet, def);
                    HCBitSet_or(reachSet, pos);
                }
            }
        }
    }

    HCMemory_release(&botMem, botMark);
}

/* --- IR optimization passes ----------------------------------------------- */

static void constantPropagation(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    hcChains_s chains;
    int modified = 0;
    int i = 0;
    int prev;
    int uses[4];

    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 2u);
    do
    {
        UDChainStub_s *perUse = chains.udChains[i];
        HCInstr_s *instr = &lst[i];
        int nUses = HC_getUses(instr, uses);
        int u;

        for (u = 0; u < nUses; ++u)
        {
            if (perUse != NULL && perUse->chains[u] != NULL)
            {
                DUChain_s *ud = perUse->chains[u];
                if (ud == NULL)
                    HC_assertFail("ud", "hcCompiler.c", 1530, NULL);
                if (ud->length == 1)
                {
                    HCInstr_s *defInstr = &lst[ud->values[0]];
                    if (HC_getOpcodeOper(defInstr) == HCOP_MOV
                        && HC_opcodeHasImm(defInstr) != 0)
                    {
                        int cVal = HC_getOpcodeImm(defInstr);
                        *instr = emulator_substitute(instr, uses[u], cVal);
                        modified = 1;
                    }
                }
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (modified != 0)
        compactInstructionList(lst);
    HCArena_release(mgr, &mark);
}

static void localCopyPropagation(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s outerMark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int b;
    int uses[4];

    for (b = 0; b < nBlocks; ++b)
    {
        HCMark_s innerMark = HCArena_mark(mgr);
        CopySet_s *cpSet = CopySet_create(mgr);
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;

        for (pos = start; pos <= end; ++pos)
        {
            HCInstr_s *instr = &lst[pos];
            int def   = HC_getDef(instr);
            int nUses = HC_getRegUses(uses, instr);
            int u;

            for (u = 0; u < nUses; ++u)
                HC_setUse(instr, u, CopySet_copyValue(cpSet, uses[u]));
            if (def != -1)
                CopySet_remove(cpSet, def);
            if (HC_getOpcodeOper(instr) == HCOP_MOV && HC_opcodeHasImm(instr) == 0)
                CopySet_addCopy(mgr, cpSet, HC_getOpcodeDst(instr), HC_getOpcodeSrc0(instr));
        }
        HCArena_release(mgr, &innerMark);
    }
    HCArena_release(mgr, &outerMark);
}

static void deadCodeElimination(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    hcChains_s chains;
    int numInstrs;
    HCBitSet_s *live;
    HCQueue_s *queue;
    int modified = 0;
    int i;
    int uses[4];

    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 2u);
    numInstrs = scanNumRegs(lst).numInstrs;
    live  = HCBitSet_create(&botMem, numInstrs);
    queue = HCQueue_create(mgr);

    for (i = 0; i < numInstrs; ++i)
    {
        int op = HC_getOpcodeOper(&lst[i]);
        if (HC_isStoreOp(op) != 0 || HC_getOpcodeTypeOp(op) == HCOP_TYPE_BRANCH)
        {
            HCBitSet_or(live, i);
            HCQueue_enqueue(mgr, queue, i);
        }
    }

    while (HCQueue_isEmpty(queue) == 0)
    {
        int cur = HCQueue_dequeue(queue);
        UDChainStub_s *perUse = chains.udChains[cur];
        int nUses = HC_getUses(&lst[cur], uses);
        int u;

        for (u = 0; u < nUses; ++u)
        {
            if (perUse != NULL && perUse->chains[u] != NULL)
            {
                DUChain_s *ud = perUse->chains[u];
                int k;
                for (k = 0; k < (int)ud->length; ++k)
                {
                    int defPos = ud->values[k];
                    if (HCBitSet_contains(live, defPos) == 0)
                    {
                        HCBitSet_or(live, defPos);
                        HCQueue_enqueue(mgr, queue, defPos);
                    }
                }
            }
        }
    }

    for (i = 0; i < numInstrs; ++i)
    {
        if (HCBitSet_contains(live, i) == 0)
        {
            lst[i] = HC_genOp0(HCOP_NOP);
            modified = 1;
        }
    }
    if (modified != 0)
        compactInstructionList(lst);
    HCArena_release(mgr, &mark);
}

static int findLabelPos(BasicBlock_s *basicBlocks, int numBlocks,
                        HCInstr_s *lst, int label)
{
    int i;
    for (i = 0; i < numBlocks; ++i)
    {
        int start = BasicBlock_getStart(&basicBlocks[i]);
        if (HC_getOpcodeOper(&lst[start]) == HCOP_LABEL
            && label == HC_getOpcodeLabel(&lst[start]))
        {
            return start;
        }
    }
    HC_assertFail("!\"undefined label!\"", "hcCompiler.c", 2304, NULL);
    return -1;
}

static void removeRedundantBranches(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int modified = 0;
    HCBitSet_s *usedLabels = HCBitSet_create(&botMem, 2048);
    int numInstrs = BasicBlock_getEnd(&blocks[nBlocks - 1]) + 1;
    int b;

    for (b = 0; b < nBlocks; ++b)
    {
        int bounds[2];
        int k;

        bounds[0] = BasicBlock_getStart(&blocks[b]);
        bounds[1] = BasicBlock_getEnd(&blocks[b]);
        for (k = 0; k < 2; ++k)
        {
            int pos = bounds[k];
            HCInstr_s *instr = &lst[pos];
            if (HC_isBranch(instr) != 0)
            {
                int target = HC_getOpcodeDst(instr);
                int dstPos = findLabelPos(blocks, nBlocks, lst, target);
                if (dstPos == pos + 1)
                {
                    *instr = HC_genOp0(HCOP_NOP);
                    modified = 1;
                }
                else
                {
                    if (dstPos + 1 >= numInstrs)
                        HC_assertFail("dstPos+1 < numInstrs", "hcCompiler.c", 2360, NULL);
                    if (HC_isAbsBranch(instr) != 0)
                    {
                        HCInstr_s *nextInstr = &lst[dstPos + 1];
                        if (HC_getOpcodeOper(&lst[dstPos]) != HCOP_LABEL)
                            HC_assertFail("HC_getOpcodeOper(&lst[dstPos]) == HCOP_LABEL",
                                          "hcCompiler.c", 2364, NULL);
                        if (HC_isAbsBranch(nextInstr) != 0)
                        {
                            int newTarget = HC_getOpcodeDst(nextInstr);
                            *instr = HC_genOp1(HCOP_BRA, newTarget);
                            HCBitSet_or(usedLabels, newTarget);
                            modified = 1;
                            continue;
                        }
                    }
                    HCBitSet_or(usedLabels, target);
                }
            }
        }
    }

    for (b = 0; b < nBlocks; ++b)
    {
        HCInstr_s *instr = &lst[BasicBlock_getStart(&blocks[b])];
        if (HC_getOpcodeOper(instr) == HCOP_LABEL)
        {
            int lbl = HC_getOpcodeLabel(instr);
            if (HCBitSet_contains(usedLabels, lbl) == 0)
            {
                *instr = HC_genOp0(HCOP_NOP);
                modified = 1;
            }
        }
    }

    if (modified != 0)
        compactInstructionList(lst);
    HCArena_release(mgr, &mark);
}

static void swapConditionalBranches(HCInstr_s *lst)
{
    int modified = 0;
    int i = 0;
    int prev;

    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);

        if (HC_isCondBranchOp(op) != 0
            && HC_isAbsBranch(&lst[i + 1]) != 0
            && HC_getOpcodeOper(&lst[i + 2]) == HCOP_LABEL)
        {
            int afterLbl  = HC_getOpcodeLabel(&lst[i + 2]);
            int braTarget = HC_getOpcodeDst(&lst[i + 1]);
            if (afterLbl == HC_getOpcodeDst(instr))
            {
                int ndx = op - HCOP_CBA;
                int newOp;
                if (ndx < 0 || ndx >= 12)
                    HC_assertFail("ndx >= 0 && ndx < N_CBRANCH", "hcCompiler.c", 2451, NULL);
                newOp = swap[ndx];
                if (HC_opcodeHasImm(instr) != 0)
                {
                    lst[i] = HC_genOp3Imm(newOp, braTarget,
                                          HC_getOpcodeSrc0(instr),
                                          HC_getOpcodeImm(instr));
                }
                else
                {
                    lst[i] = HC_genOp3(newOp, braTarget,
                                       HC_getOpcodeSrc0(instr),
                                       HC_getOpcodeSrc1(instr));
                }
                lst[i + 1] = HC_genOp0(HCOP_NOP);
                modified = 1;
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (modified != 0)
        compactInstructionList(lst);
}

static void unpackRule(UnpackedRule_s *r, PackedRule_s pr)
{
    r->op        = pr.op;
    r->maskDst   = pr.maskDst;
    r->maskSrc0  = pr.maskSrc0;
    r->maskSrc1  = pr.maskSrc1;
    r->immediate = pr.immediate;
}

static void peephole(HCInstr_s *lst)
{
    struct { int matched; int val; } regs[5];
    int modified = 0;
    int i = 0;
    int prev;

    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);
        int r;

        for (r = 0; r < 52; r += 2)
        {
            if (HC_getOpcodeType(instr) == HCOP_TYPE_ALUOP
                && HC_getOpcodeArgCount(instr) == 3)
            {
                UnpackedRule_s match;
                UnpackedRule_s rewrite;

                unpackRule(&match, rewrites[r]);
                unpackRule(&rewrite, rewrites[r + 1]);
                memset(regs, 0, sizeof(regs));

                if (op != match.op)
                    continue;

                regs[match.maskDst].matched = 1;
                regs[match.maskDst].val     = HC_getOpcodeDst(instr);

                if (regs[match.maskSrc0].matched != 0)
                {
                    if (regs[match.maskSrc0].val != HC_getOpcodeSrc0(instr))
                        continue;
                }
                else
                {
                    regs[match.maskSrc0].matched = 1;
                    regs[match.maskSrc0].val     = HC_getOpcodeSrc0(instr);
                }

                if (HC_opcodeHasImm(instr) != 0)
                {
                    int imm = HC_getOpcodeImm(instr);
                    if (match.maskSrc1 != IMM || match.immediate != imm)
                        continue;
                    if (regs[match.maskSrc1].matched != 0)
                    {
                        if (regs[match.maskSrc1].val != imm)
                            continue;
                    }
                    else
                    {
                        regs[match.maskSrc1].matched = 1;
                        regs[match.maskSrc1].val     = imm;
                    }
                }
                else
                {
                    int s1;
                    if (match.maskSrc1 == IMM)
                        continue;
                    s1 = HC_getOpcodeSrc1(instr);
                    if (regs[match.maskSrc1].matched != 0)
                    {
                        if (regs[match.maskSrc1].val != s1)
                            continue;
                    }
                    else
                    {
                        regs[match.maskSrc1].matched = 1;
                        regs[match.maskSrc1].val     = s1;
                    }
                }

                if (regs[rewrite.maskDst].matched == 0)
                    HC_assertFail("regs[(int)rewrite.maskDst].matched", "hcCompiler.c", 2635, NULL);
                if (rewrite.maskDst > REG_C)
                    HC_assertFail("rewrite.maskDst <= REG_C", "hcCompiler.c", 2636, NULL);

                modified = 1;
                if (HC_getOpcodeArgCountOp(rewrite.op) == 2)
                {
                    if (rewrite.maskSrc0 > REG_C)
                    {
                        if (rewrite.maskSrc0 != IMM)
                            HC_assertFail("rewrite.maskSrc0 == IMM", "hcCompiler.c", 2652, NULL);
                        *instr = HC_genOp2Imm(rewrite.op, regs[rewrite.maskDst].val,
                                              rewrite.immediate);
                    }
                    else
                    {
                        *instr = HC_genOp2(rewrite.op, regs[rewrite.maskDst].val,
                                           regs[rewrite.maskSrc0].val);
                    }
                }
                else if (HC_getOpcodeArgCountOp(rewrite.op) == 3)
                {
                    if (rewrite.maskSrc0 > REG_C)
                        HC_assertFail("rewrite.maskSrc0 <= REG_C", "hcCompiler.c", 2659, NULL);
                    if (regs[rewrite.maskSrc0].matched == 0)
                        HC_assertFail("regs[rewrite.maskSrc0].matched", "hcCompiler.c", 2660, NULL);
                    if (rewrite.maskSrc1 > REG_C)
                    {
                        if (rewrite.maskSrc1 != IMM)
                            HC_assertFail("rewrite.maskSrc1 == IMM", "hcCompiler.c", 2671, NULL);
                        *instr = HC_genOp3Imm(rewrite.op,
                                              regs[rewrite.maskDst].val,
                                              regs[rewrite.maskSrc0].val,
                                              rewrite.immediate);
                    }
                    else
                    {
                        if (regs[rewrite.maskSrc1].matched == 0)
                            HC_assertFail("regs[rewrite.maskSrc1].matched", "hcCompiler.c", 2663, NULL);
                        *instr = HC_genOp3(rewrite.op,
                                           regs[rewrite.maskDst].val,
                                           regs[rewrite.maskSrc0].val,
                                           regs[rewrite.maskSrc1].val);
                    }
                }
            }
        }

        if (HC_opcodeHasImm(instr) != 0 && (op == HCOP_MULU || op == HCOP_MULS))
        {
            unsigned int imm = (unsigned int)HC_getOpcodeImm(instr);
            if (imm != 0 && hgIsPowerOfTwo32((HGuint32)imm) != 0)
            {
                int shift = HC_bsf(imm);
                *instr = HC_genOp3Imm(HCOP_LSL,
                                      HC_getOpcodeDst(instr),
                                      HC_getOpcodeSrc0(instr),
                                      shift);
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (modified != 0)
        compactInstructionList(lst);
}

/* Bit provenance for a locally unpacked/repacked texel. Each output bit is
 * either zero or a bit of one loaded word. This handles byte swizzles as well
 * as the replicated bits used to expand RGB565/RGBA4444 to 8-bit components. */
typedef struct RepackBits_s
{
    int source;
    signed char bit[32]; /* -1 = constant zero */
} RepackBits_s;

static int traceRepack(HCInstr_s *lst, const int *defPos, int first, int end,
                       int reg, RepackBits_s *out, int *budget, int depth)
{
    int pc = defPos[reg], op, src, i;
    unsigned int imm;
    HCInstr_s *ins;

    if (depth == 0 || --*budget < 0 || pc < first || pc >= end)
        return 0;
    ins = &lst[pc];
    op = HC_getOpcodeOper(ins);
    if (HC_isLoadOp(op))
    {
        int width;
        switch (op)
        {
        case HCOP_LBU: width = 8; break;
        case HCOP_LWU: case HCOP_LW2U: width = 16; break;
        case HCOP_LD: case HCOP_LD4: width = 32; break;
        default: return 0;
        }
        out->source = reg;
        for (i = 0; i < 32; ++i) out->bit[i] = i < width ? i : -1;
        return 1;
    }
    src = HC_getOpcodeSrc0Raw(ins);
    if (op == HCOP_OR && !HC_opcodeHasImm(ins))
    {
        RepackBits_s rhs;
        if (!traceRepack(lst, defPos, first, pc, src, out, budget, depth - 1) ||
            !traceRepack(lst, defPos, first, pc, HC_getOpcodeSrc1Raw(ins), &rhs, budget, depth - 1) ||
            out->source != rhs.source)
            return 0;
        for (i = 0; i < 32; ++i)
        {
            if (out->bit[i] < 0) out->bit[i] = rhs.bit[i];
            else if (rhs.bit[i] >= 0 && out->bit[i] != rhs.bit[i]) return 0;
        }
        return 1;
    }
    if (op == HCOP_MOV && !HC_opcodeHasImm(ins))
        return traceRepack(lst, defPos, first, pc, src, out, budget, depth - 1);
    if (!HC_opcodeHasImm(ins) || (op != HCOP_AND && op != HCOP_LSL &&
        op != HCOP_LSR && op != HCOP_MINS && op != HCOP_MINU))
        return 0;
    imm = ins->instr1;
    if (!traceRepack(lst, defPos, first, pc, src, out, budget, depth - 1))
        return 0;
    if (op == HCOP_AND)
    {
        for (i = 0; i < 32; ++i)
            if (((imm >> i) & 1u) == 0) out->bit[i] = -1;
    }
    else if (op == HCOP_LSL || op == HCOP_LSR)
    {
        if (imm >= 32u) return 0;
        if (op == HCOP_LSR)
            for (i = 0; i < 32; ++i)
                out->bit[i] = i + (int)imm < 32 ? out->bit[i + (int)imm] : -1;
        else
            for (i = 31; i >= 0; --i)
                out->bit[i] = i >= (int)imm ? out->bit[i - (int)imm] : -1;
    }
    else
    {
        /* Skip a clamp only when it cannot change the value. */
        unsigned int max = 0;
        for (i = 0; i < 32; ++i)
            if (out->bit[i] >= 0) max |= 1u << i;
        if (max > imm || (op == HCOP_MINS && (max | imm) >= 0x80000000u))
            return 0;
    }
    return 1;
}

static HCInstr_s *repackDef(HCInstr_s *lst, const int *defPos, int first, int end, int reg)
{
    int pc = defPos[reg];
    return pc >= first && pc < end ? &lst[pc] : NULL;
}

static int traceLoadedByte(HCInstr_s *lst, const int *defPos, int first, int end,
                           int reg, int lane, int *budget)
{
    RepackBits_s value;
    int i;
    if (!traceRepack(lst, defPos, first, end, reg, &value, budget, 8)) return -1;
    for (i = 0; i < 32; ++i)
        if (value.bit[i] != (i < 8 ? i + lane : -1)) return -1;
    return value.source;
}

static int repackOp(HCInstr_s *ins, int op, int hasImm, unsigned int imm)
{
    return ins != NULL && HC_getOpcodeOper(ins) == op && HC_opcodeHasImm(ins) == hasImm &&
           (!hasImm || ins->instr1 == imm);
}

/* Trace one independently truncated framebuffer term, (color*factor+color)>>8.
 * The bias must be the color, not the factor (as in SRC_ALPHA_SATURATE). */
static int traceAlphaTerm(HCInstr_s *lst, const int *defPos, int first, HCInstr_s *consumer,
                          int term, int lane, int *budget, int *alpha, int *inverse)
{
    HCInstr_s *shift, *add, *mul, *weight;
    int pc = (int)(consumer - lst), i;
    shift = repackDef(lst, defPos, first, pc, term);
    if (!repackOp(shift, HCOP_LSR, 1, 8)) return -1;
    add = repackDef(lst, defPos, first, (int)(shift - lst), HC_getOpcodeSrc0Raw(shift));
    if (!repackOp(add, HCOP_ADD, 0, 0)) return -1;
    for (i = 0; i < 2; ++i)
    {
        int idx = (int)(add - lst);
        int color = i == 0 ? HC_getOpcodeSrc0Raw(add) : HC_getOpcodeSrc1Raw(add);
        int product = i == 0 ? HC_getOpcodeSrc1Raw(add) : HC_getOpcodeSrc0Raw(add);
        int a, b, factor, packed;
        mul = repackDef(lst, defPos, first, idx, product);
        if ((!repackOp(mul, HCOP_MULS, 0, 0) && !repackOp(mul, HCOP_MULU, 0, 0))) continue;
        a = HC_getOpcodeSrc0Raw(mul);
        b = HC_getOpcodeSrc1Raw(mul);
        if (a != color && b != color) continue;
        factor = a == color ? b : a;
        idx = (int)(mul - lst);
        packed = traceLoadedByte(lst, defPos, first, idx, color, lane, budget);
        if (packed < 0) continue;
        weight = repackDef(lst, defPos, first, idx, factor);
        *inverse = repackOp(weight, HCOP_XOR, 1, 255) || repackOp(weight, HCOP_SUBR, 1, 255);
        if (*inverse)
        {
            factor = HC_getOpcodeSrc0Raw(weight);
            idx = (int)(weight - lst);
        }
        *alpha = traceLoadedByte(lst, defPos, first, idx, factor, 24, budget);
        if (*alpha >= 0) return packed;
    }
    return -1;
}

typedef struct PackedBlend_s
{
    int op, a, b;
    unsigned int lanes;
} PackedBlend_s;

/* Match the entire OR tree, with the same packed operands and operation in
 * every written lane. No assumptions about the GL state or gradient range. */
static int tracePackedBlend(HCInstr_s *lst, const int *defPos, int first, int end,
                            int reg, PackedBlend_s *blend, int *budget, int depth)
{
    HCInstr_s *ins = repackDef(lst, defPos, first, end, reg);
    int lane = 0, a = -1, b = -1, op = HCOP_UQADD8;
    unsigned int laneBit;

    if (ins == NULL || depth == 0 || --*budget < 0) return 0;
    end = (int)(ins - lst);
    if (repackOp(ins, HCOP_OR, 0, 0))
        return tracePackedBlend(lst, defPos, first, end, HC_getOpcodeSrc0Raw(ins), blend, budget, depth - 1) &&
               tracePackedBlend(lst, defPos, first, end, HC_getOpcodeSrc1Raw(ins), blend, budget, depth - 1);
    if (HC_getOpcodeOper(ins) == HCOP_LSL && HC_opcodeHasImm(ins))
    {
        if (ins->instr1 > 24u || (ins->instr1 & 7u) != 0) return 0;
        lane = (int)ins->instr1;
        ins = repackDef(lst, defPos, first, end, HC_getOpcodeSrc0Raw(ins));
    }
    laneBit = 1u << (lane / 8);
    if ((blend->lanes & laneBit) || (!repackOp(ins, HCOP_MINU, 1, 255) && !repackOp(ins, HCOP_MINS, 1, 255)))
        return 0;
    ins = repackDef(lst, defPos, first, (int)(ins - lst), HC_getOpcodeSrc0Raw(ins));
    if (repackOp(ins, HCOP_MAXS, 1, 0))
    {
        op = HCOP_UQSUB8;
        ins = repackDef(lst, defPos, first, (int)(ins - lst), HC_getOpcodeSrc0Raw(ins));
    }
    if ((op == HCOP_UQADD8 && repackOp(ins, HCOP_ADD, 0, 0)) ||
        (op == HCOP_UQSUB8 && (repackOp(ins, HCOP_SUB, 0, 0) || repackOp(ins, HCOP_SUBR, 0, 0))))
    {
        int x = HC_getOpcodeSrc0Raw(ins), y = HC_getOpcodeSrc1Raw(ins);
        if (HC_getOpcodeOper(ins) == HCOP_SUBR) { int tmp = x; x = y; y = tmp; }
        end = (int)(ins - lst);
        a = traceLoadedByte(lst, defPos, first, end, x, lane, budget);
        b = traceLoadedByte(lst, defPos, first, end, y, lane, budget);
        if ((a < 0 || b < 0) && op == HCOP_UQADD8)
        {
            int ax, ay, ix, iy;
            a = traceAlphaTerm(lst, defPos, first, ins, x, lane, budget, &ax, &ix);
            b = traceAlphaTerm(lst, defPos, first, ins, y, lane, budget, &ay, &iy);
            if (a < 0 || b < 0 || ax != ay || ix == iy) return 0;
            if (ix) { int tmp = a; a = b; b = tmp; } /* first term uses the direct alpha */
            if (ax == a) op = HCOP_PACKED_ALPHA_A;
            else if (ax == b) op = HCOP_PACKED_ALPHA_B;
            else return 0;
        }
    }
    if (a < 0 || b < 0) return 0;
    if (op == HCOP_UQADD8 && a > b) { int tmp = a; a = b; b = tmp; }
    if (blend->lanes == 0)
    {
        blend->op = op;
        blend->a = a;
        blend->b = b;
    }
    else if (op != blend->op || a != blend->a || b != blend->b)
        return 0;
    blend->lanes |= laneBit;
    return 1;
}

static int peepholeRepackStore(HCArena_s *mgr, HCInstr_s *lst, int packedBlend)
{
    HCMark_s mark = HCArena_mark(mgr);
    RegInstrCount_s info = scanNumRegs(lst);
    int *defPos, i, first = 0, changed = 0;

    if (info.numRegs <= 0) return 0;
    defPos = (int *)HCArena_allocBottom(mgr, sizeof(int) * (unsigned int)info.numRegs);
    for (i = 0; i < info.numRegs; ++i) defPos[i] = -1;
    for (i = 0; i < info.numInstrs; ++i)
    {
        int def = HC_getDef(&lst[i]);
        if (def >= 0) defPos[def] = defPos[def] == -1 ? i : -2;
    }
    for (i = 0; i < info.numInstrs; ++i)
    {
        HCInstr_s *ins = &lst[i];
        int op = HC_getOpcodeOper(ins);
        int reg, pc, width, bit, budget = 48;
        RepackBits_s bits;
        if (op == HCOP_LABEL || HC_isBranchOp(op))
            first = i + 1;
        if (op == HCOP_SW || op == HCOP_SW2) width = 16;
        else if (op == HCOP_SD || op == HCOP_SD4) width = 32;
        else continue;
        reg = HC_getOpcodeDst(ins);
        pc = defPos[reg];
        if (pc < first || pc >= i || HC_getOpcodeOper(&lst[pc]) != HCOP_OR)
            continue;
        if (packedBlend && width == 32)
        {
            PackedBlend_s blend = { 0, 0, 0, 0 };
            int blendBudget = 120;
            if (tracePackedBlend(lst, defPos, first, i, reg, &blend, &blendBudget, 8))
            {
                int op = blend.op;
                if (blend.lanes == 7u)
                {
                    /* The tree proves bits 24..31 are zero, not a preserved
                     * byte. RGB-only media pseudo-ops keep that exact value. */
                    switch (op)
                    {
                    case HCOP_UQADD8: op = HCOP_UQADD8_RGB; break;
                    case HCOP_UQSUB8: op = HCOP_UQSUB8_RGB; break;
                    case HCOP_PACKED_ALPHA_A: op = HCOP_PACKED_ALPHA_RGB_A; break;
                    case HCOP_PACKED_ALPHA_B: op = HCOP_PACKED_ALPHA_RGB_B; break;
                    }
                }
                if (blend.lanes == 7u || blend.lanes == 15u)
                {
                    lst[pc] = HC_genOp3(op, reg, blend.a, blend.b);
                    changed = 1;
                }
            }
            continue;
        }
        if (packedBlend || !traceRepack(lst, defPos, first, i, reg, &bits, &budget, 12))
            continue;
        for (bit = 0; bit < width; ++bit)
            if (bits.bit[bit] != bit) break;
        if (bit == width)
        {
            HC_setOpcodeDst(ins, bits.source);
            changed = 1;
        }
    }
    HCArena_release(mgr, &mark);
    return changed;
}

static void peepholeFills(HCArena_s *mgr, HCInstr_s *lst)
{
    int i = 0;
    int removedInstrs = 0;
    int numFills = 0;
    int nextReg = scanNumRegs(lst).numRegs;
    int modified = 0;
    FillBlock_s *head = NULL;
    int topMark = HCArena_markTop(mgr);
    int prev;

    if (HCArena_inLowMem(mgr, lst) == 0)
        HC_assertFail("HCArena_inLowMem(mgr, lst) && \"source program must be in low memory\"",
                      "hcCompiler.c", 2726, NULL);

    do
    {
        HCInstr_s *i0 = &lst[i];
        HCInstr_s *i1 = &lst[i + 1];
        HCInstr_s *i2 = &lst[i + 2];
        HCInstr_s *i3 = &lst[i + 3];

        if (HC_getOpcodeOper(i0) == HCOP_LABEL
            && (HC_getOpcodeOper(i1) == HCOP_SD4
                || HC_getOpcodeOper(i1) == HCOP_SW2
                || HC_getOpcodeOper(i1) == HCOP_SB)
            && HC_opcodeHasImm(i1) == 0
            && HC_getOpcodeOper(i2) == HCOP_ADD
            && HC_opcodeHasImm(i2) != 0
            && HC_getOpcodeOper(i3) == HCOP_CBNEQ
            && HC_opcodeHasImm(i3) != 0)
        {
            int storeOp = HC_getOpcodeOper(i1);
            int lbl     = HC_getOpcodeLabel(i0);
            if (lbl == HC_getOpcodeDst(i3))
            {
                int pat = HC_getOpcodeDst(i1);
                int dst = HC_getOpcodeSrc0(i1);
                int cnt = HC_getOpcodeSrc1(i1);
                if (cnt == HC_getOpcodeDst(i2)
                    && cnt == HC_getOpcodeSrc0(i2)
                    && HC_getOpcodeImm(i2) == 1
                    && cnt == HC_getOpcodeSrc0(i3))
                {
                    HCOP_e fillOp = HCOP_FILLD;
                    FillBlock_s *fb;
                    if (storeOp == HCOP_SB)
                        fillOp = HCOP_FILLB;
                    else if (storeOp == HCOP_SW2)
                        fillOp = HCOP_FILLW;

                    fb = (FillBlock_s *)HCArena_allocTop(mgr, (unsigned int)sizeof(FillBlock_s));
                    fb->next     = head;
                    head         = fb;
                    fb->startNdx = i;
                    fb->length   = 4;
                    fb->dst      = dst;
                    fb->cnt      = cnt;
                    fb->pattern  = pat;
                    fb->fillOp   = fillOp;
                    ++numFills;
                    removedInstrs += 4;
                    i += 4;
                    modified = 1;
                }
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (numFills != 0)
    {
        int oldTail = i - 1;
        HCInstr_s *tail = &lst[i + 5 * numFills - removedInstrs - 1];

        HCArena_setBottom(mgr, &tail[8]);
        if (HC_getOpcodeOper(&lst[oldTail]) != HCOP_EOP)
            HC_assertFail("HC_getOpcodeOper(&lst[oldTail]) == HCOP_EOP",
                          "hcCompiler.c", 2808, NULL);

        while (head != NULL)
        {
            int cnt   = head->cnt;
            int dst   = head->dst;
            int pat   = head->pattern;
            int shift = (int)head->fillOp - (int)HCOP_FILLB;
            int rByte = nextReg++;
            int rAddr = nextReg++;
            int rLen  = nextReg++;
            int fillEnd = head->startNdx + head->length;
            int k;

            for (k = oldTail; k >= fillEnd; --k)
            {
                if (tail < lst)
                    HC_assertFail("tail >= lst", "hcCompiler.c", 2822, NULL);
                *tail-- = lst[k];
            }
            oldTail = head->startNdx - 1;
            tail -= 5;
            tail[1] = HC_genOp3Imm(HCOP_SUBR, rLen, cnt, 0);
            tail[2] = HC_genOp3Imm(HCOP_LSL,  rByte, rLen, shift);
            tail[3] = HC_genOp3(HCOP_SUB,     rAddr, dst, rByte);
            tail[4] = HC_genOp3(head->fillOp, rAddr, pat, rLen);
            tail[5] = HC_genOp2Imm(HCOP_MOV,  cnt, 0);
            head = head->next;
        }

        for (i = oldTail; i >= 0; --i)
            *tail-- = lst[i];
        if (tail + 1 != lst)
            HC_assertFail("tail+1 == lst", "hcCompiler.c", 2843, NULL);
    }

    HCArena_releaseTop(mgr, topMark);
    if (modified != 0)
        compactInstructionList(lst);
}

static HCInstr_s *inlineFills(HCArena_s *mgr, HCInstr_s *lst)
{
    int i = 0;
    int nextReg = scanNumRegs(lst).numRegs;
    int nextLbl = scanMaxLabel(lst);
    HCInstr_s *out = (HCInstr_s *)HCArena_current(mgr);
    HCInstr_s *dst = out;
    int prev;

    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);

        if (op == HCOP_FILLB || op == HCOP_FILLW || op == HCOP_FILLD)
        {
            int rPat  = nextReg++;
            int rCnt  = nextReg++;
            int rAddr = nextReg++;
            int lblTailLoop  = nextLbl++;
            int lblUnroll    = nextLbl++;
            int lblDone      = nextLbl++;
            int lblAligned   = nextLbl++;
            int lblWordReady = nextLbl++;
            int lblHeadLoop  = nextLbl++;
            int lblByteTail  = nextLbl++;

            if (HC_opcodeHasImm(instr) != 0)
                HC_assertFail("!HC_opcodeHasImm(instr)", "hcCompiler.c", 2881, NULL);

            HCArena_growCurrent(mgr, 3u * (unsigned int)sizeof(HCInstr_s));
            *dst++ = HC_genOp2(HCOP_MOV, rAddr, HC_getOpcodeDst(instr));
            *dst++ = HC_genOp2(HCOP_MOV, rPat,  HC_getOpcodeSrc0(instr));
            *dst++ = HC_genOp2(HCOP_MOV, rCnt,  HC_getOpcodeSrc1(instr));

            if (op == HCOP_FILLB)
            {
                HCInstr_s *cur = dst;
                HCArena_growCurrent(mgr, 20u * (unsigned int)sizeof(HCInstr_s));
                *dst++ = HC_genOp3Imm(HCOP_AND,   rPat, rPat, 255);
                *dst++ = HC_genOp3Imm(HCOP_LSL,   nextReg, rPat, 8);
                *dst++ = HC_genOp3(HCOP_OR,       rPat, rPat, nextReg);
                *dst++ = HC_genOp3Imm(HCOP_LSL,   nextReg, rPat, 16);
                *dst++ = HC_genOp3(HCOP_OR,       rPat, rPat, nextReg);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblHeadLoop);
                *dst++ = HC_genOp3Imm(HCOP_CBNA,  lblAligned, rAddr, 3);
                *dst++ = HC_genOp3Imm(HCOP_SB,    rPat, rAddr, 0);
                *dst++ = HC_genOp3Imm(HCOP_ADD,   rAddr, rAddr, 1);
                *dst++ = HC_genOp3Imm(HCOP_SUB,   rCnt, rCnt, 1);
                *dst++ = HC_genOp3(HCOP_CBA,      lblHeadLoop, rCnt, rCnt);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblAligned);
                *dst++ = HC_genOp3Imm(HCOP_CBNA,  lblWordReady, rCnt, 3);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblByteTail);
                *dst++ = HC_genOp3Imm(HCOP_SUB,   rCnt, rCnt, 1);
                *dst++ = HC_genOp3(HCOP_SB,       rPat, rAddr, rCnt);
                *dst++ = HC_genOp3Imm(HCOP_CBA,   lblByteTail, rCnt, 3);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblWordReady);
                *dst++ = HC_genOp3Imm(HCOP_LSR,   rCnt, rCnt, 2);
                *dst++ = HC_genOp3(HCOP_CBNA,     lblDone, rCnt, rCnt);
                if (dst - cur != 20)
                    HC_assertFail("dst - cur == NUM_NEW", "hcCompiler.c", 2917, NULL);
            }
            else if (op == HCOP_FILLW)
            {
                HCInstr_s *cur = dst;
                HCArena_growCurrent(mgr, 15u * (unsigned int)sizeof(HCInstr_s));
                *dst++ = HC_genOp3Imm(HCOP_AND,   rPat, rPat, 0xFFFF);
                *dst++ = HC_genOp3Imm(HCOP_LSL,   nextReg, rPat, 16);
                *dst++ = HC_genOp3(HCOP_OR,       rPat, rPat, nextReg);
                *dst++ = HC_genOp3Imm(HCOP_CBNA,  lblAligned, rAddr, 3);
                *dst++ = HC_genOp3Imm(HCOP_SW,    rPat, rAddr, 0);
                *dst++ = HC_genOp3Imm(HCOP_ADD,   rAddr, rAddr, 2);
                *dst++ = HC_genOp3Imm(HCOP_SUB,   rCnt, rCnt, 1);
                *dst++ = HC_genOp3(HCOP_CBNA,     lblDone, rCnt, rCnt);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblAligned);
                *dst++ = HC_genOp3Imm(HCOP_CBNA,  lblWordReady, rCnt, 1);
                *dst++ = HC_genOp3Imm(HCOP_SUB,   nextReg, rAddr, 2);
                *dst++ = HC_genOp3(HCOP_SW2,      rPat, nextReg, rCnt);
                *dst++ = HC_genOp1Imm(HCOP_LABEL, lblWordReady);
                *dst++ = HC_genOp3Imm(HCOP_LSR,   rCnt, rCnt, 1);
                *dst++ = HC_genOp3(HCOP_CBNA,     lblDone, rCnt, rCnt);
                if (dst - cur != 15)
                    HC_assertFail("dst - cur == NUM_NEW", "hcCompiler.c", 2941, NULL);
            }

            HCArena_growCurrent(mgr, 20u * (unsigned int)sizeof(HCInstr_s));
            *dst++ = HC_genOp3Imm(HCOP_CBLTS, lblTailLoop, rCnt, 8);
            *dst++ = HC_genOp1Imm(HCOP_LABEL, lblUnroll);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 0);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 4);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 8);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 12);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 16);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 20);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 24);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 28);
            *dst++ = HC_genOp3Imm(HCOP_ADD,   rAddr, rAddr, 32);
            *dst++ = HC_genOp3Imm(HCOP_SUB,   rCnt, rCnt, 8);
            *dst++ = HC_genOp3Imm(HCOP_CBGES, lblUnroll, rCnt, 8);
            *dst++ = HC_genOp3(HCOP_CBNA,     lblDone, rCnt, rCnt);
            *dst++ = HC_genOp1Imm(HCOP_LABEL, lblTailLoop);
            *dst++ = HC_genOp3Imm(HCOP_SD,    rPat, rAddr, 0);
            *dst++ = HC_genOp3Imm(HCOP_ADD,   rAddr, rAddr, 4);
            *dst++ = HC_genOp3Imm(HCOP_SUB,   rCnt, rCnt, 1);
            *dst++ = HC_genOp3(HCOP_CBA,      lblTailLoop, rCnt, rCnt);
            *dst++ = HC_genOp1Imm(HCOP_LABEL, lblDone);
        }
        else
        {
            HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
            *dst++ = *instr;
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    return out;
}

static void shiftChainCombine(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s outerMark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int b;

    for (b = 0; b < nBlocks; ++b)
    {
        HCMark_s innerMark = HCArena_mark(mgr);
        CopySet_s *cpSet = CopySet_create(mgr);
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;

        for (pos = start; pos <= end; ++pos)
        {
            HCInstr_s *instr = &lst[pos];
            int op  = HC_getOpcodeOper(instr);
            int def = HC_getDef(instr);

            if ((op == HCOP_LSR || op == HCOP_ASR) && HC_opcodeHasImm(instr) != 0)
            {
                int s0 = HC_getOpcodeSrc0(instr);
                CopyVal_s *prevShift = CopySet_valueGeneric(cpSet, s0);
                if (prevShift != NULL
                    && ((prevShift->type == SHIFT_LSR && op == HCOP_LSR)
                        || (prevShift->type == SHIFT_ASR && op == HCOP_ASR)))
                {
                    int total = (int)prevShift->shift + HC_getOpcodeImm(instr);
                    if (total <= 31)
                    {
                        HC_setOpcodeSrc0(instr, (int)prevShift->src);
                        HC_setOpcodeImm(instr, total);
                    }
                    else
                    {
                        if (prevShift->type != SHIFT_LSR && prevShift->type != SHIFT_ASR)
                            HC_assertFail("shift->type == SHIFT_LSR || shift->type == SHIFT_ASR",
                                          "hcCompiler.c", 3042, NULL);
                        if (prevShift->type != SHIFT_LSR)
                            *instr = HC_genOp3Imm(HCOP_ASR, def, s0, 31);
                        else
                            *instr = HC_genOp2Imm(HCOP_MOV, def, 0);
                    }
                }
            }

            if (def != -1)
                CopySet_remove(cpSet, def);

            if ((op == HCOP_LSR || op == HCOP_ASR) && HC_opcodeHasImm(instr) != 0)
            {
                HCShiftType_e ty = (op != HCOP_LSR) ? SHIFT_ASR : SHIFT_LSR;
                CopySet_addShiftRight(mgr, cpSet,
                                      HC_getOpcodeDst(instr),
                                      HC_getOpcodeSrc0(instr),
                                      HC_getOpcodeImm(instr), ty);
            }
        }
        HCArena_release(mgr, &innerMark);
    }
    HCArena_release(mgr, &outerMark);
}

static void shiftLogicalAndCombine(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    hcChains_s chains;
    int modified = 0;
    int i = 0;
    int prev;
    int uses[4];

    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 2u);
    do
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);
        UDChainStub_s *perUse = chains.udChains[i];

        if (op == HCOP_AND && HC_opcodeHasImm(instr) != 0)
        {
            DUChain_s *ud = NULL;
            int nUses = HC_getUses(instr, uses);
            int s0 = HC_getOpcodeSrc0(instr);
            int u;

            for (u = 0; u < nUses; ++u)
            {
                if (perUse != NULL && perUse->chains[u] != NULL && s0 == uses[u])
                {
                    ud = perUse->chains[u];
                    break;
                }
            }

            if (ud != NULL && ud->length == 1)
            {
                HCInstr_s *defInstr = &lst[ud->values[0]];
                int defOp = HC_getOpcodeOper(defInstr);
                if (HC_opcodeHasImm(defInstr) != 0 && (defOp == HCOP_LSR || defOp == HCOP_LSL))
                {
                    unsigned int shiftAmt = (unsigned int)HC_getOpcodeImm(defInstr) & 31u;
                    unsigned int andMask  = (unsigned int)HC_getOpcodeImm(instr);
                    unsigned int liveBits = (defOp == HCOP_LSR)
                        ? (0xFFFFFFFFu >> shiftAmt)
                        : (0xFFFFFFFFu << shiftAmt);
                    if (liveBits == (andMask & liveBits))
                    {
                        *instr = HC_genOp2(HCOP_MOV, HC_getOpcodeDst(instr), s0);
                        modified = 1;
                    }
                }
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    HCArena_release(mgr, &mark);
    if (modified != 0)
        compactInstructionList(lst);
}

static void offsetRegs(HCInstr_s *lst, int nHardRegs)
{
    int i = 0;
    int prev;
    int uses[4];

    do
    {
        HCInstr_s *instr = &lst[i];
        int nUses = HC_getRegUses(uses, instr);
        int def   = HC_getDef(instr);
        int u;

        if (def != -1 && def != 0)
            HC_setOpcodeDst(instr, nHardRegs + def);
        for (u = 0; u < nUses; ++u)
        {
            if (uses[u] != 0)
                HC_setUse(instr, u, uses[u] + nHardRegs);
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
}

/* --- live-range splitting & graph-coloring register allocation ------------ */

static LREntry_s *UnionFind_root(LREntry_s *e)
{
    LREntry_s *e1 = e;
    if (e == NULL)
        HC_assertFail("e", "hcCompiler.c", 3198, NULL);
    while (e->parent != NULL)
        e = e->parent;
    while (e1->parent != NULL)
    {
        LREntry_s *e2 = e1->parent;
        e1->parent = e;
        e1 = e2;
    }
    return e;
}

static void UnionFind_union(LREntry_s *first, LREntry_s *second)
{
    LREntry_s *r1;
    LREntry_s *r2;

    if (first == NULL || second == NULL)
        HC_assertFail("first && second", "hcCompiler.c", 3214, NULL);
    r1 = UnionFind_root(first);
    r2 = UnionFind_root(second);
    if (r1 != r2)
        r2->parent = r1;
}

static int findWeb(LREntry_s *w, int *regCount)
{
    LREntry_s *root;
    if (w == NULL || regCount == NULL)
        HC_assertFail("w && regCount", "hcCompiler.c", 3226, NULL);
    root = UnionFind_root(w);
    if (root->reg == -1)
        root->reg = (*regCount)++;
    return root->reg;
}

static int liveRangeAnalysis(HCArena_s *mgr, HCInstr_s *lst, int numHardRegs)
{
    int regCount = numHardRegs;
    int numInstrs = scanNumRegs(lst).numInstrs;
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    hcChains_s chains;
    int nUseSlots = 3 * numInstrs;
    LREntry_s *useEntries;
    LREntry_s *defEntries;
    int uses[4];
    int i;

    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 5u);
    useEntries = (LREntry_s *)HCArena_allocBottom(
        mgr, (unsigned int)((size_t)nUseSlots * sizeof(LREntry_s)));
    defEntries = (LREntry_s *)HCArena_allocBottom(
        mgr, (unsigned int)((size_t)numInstrs * sizeof(LREntry_s)));

    for (i = 0; i < nUseSlots; ++i)
        LREntry_init(&useEntries[i]);
    for (i = 0; i < numInstrs; ++i)
        LREntry_init(&defEntries[i]);

    for (i = 0; i < numInstrs; ++i)
    {
        DUChain_s *du = (DUChain_s *)DUChains_getDUChain(&chains, i);
        if (du != NULL)
        {
            int k;
            for (k = 0; k < (int)du->length; ++k)
                UnionFind_union(&useEntries[du->values[k]], &defEntries[i]);
        }
    }

    for (i = 0; i < numInstrs; ++i)
    {
        HCInstr_s *instr = &lst[i];
        int nUses = HC_getRegUses(uses, instr);
        int def;
        int u;

        for (u = 0; u < nUses; ++u)
        {
            if (uses[u] >= numHardRegs)
                HC_setUse(instr, u, findWeb(&useEntries[3 * i + u], &regCount));
        }
        def = HC_getDef(instr);
        if (def != -1 && def >= numHardRegs)
        {
            int d = findWeb(&defEntries[i], &regCount);
            if (d == 0)
                HC_assertFail("d != 0", "hcCompiler.c", 3299, NULL);
            HC_setOpcodeDst(instr, d);
        }
    }

    HCArena_release(mgr, &mark);
    return regCount;
}

static HCAdjMtx_s *buildInterferenceGraph(HCArena_s *mgr, HCInstr_s *lst,
                                          int nHardRegs, int nSymRegs)
{
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    HCAdjMtx_s *mtx = HCAdjMtx_create(&botMem, nSymRegs);
    HCMark_s mark = HCArena_mark(mgr);
    HCBitSet_s *live = HCBitSet_createNoClear(&botMem, nSymRegs);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int uses[4];
    int i;
    int j;

    liveness(mgr, blocks, nBlocks, nSymRegs, lst);

    for (i = 1; i < nHardRegs; ++i)
    {
        for (j = 0; j < i; ++j)
            HCAdjMtx_connect(mtx, i, j);
    }
    for (i = nHardRegs; i < nSymRegs; ++i)
        HCAdjMtx_connect(mtx, i, 0);

    for (i = 0; i < nBlocks; ++i)
    {
        BasicBlock_s *bb = &blocks[i];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;

        HCBitSet_copy(live, bb->liveOut);
        for (pos = end; pos >= start; --pos)
        {
            HCInstr_s *instr = &lst[pos];
            int nUses = HC_getUses(instr, uses);
            int def   = HC_getDef(instr);
            int u;

            if (def != -1)
            {
                int k;
                if (HC_getOpcodeOper(instr) == HCOP_MOV && HC_opcodeHasImm(instr) == 0)
                    HCBitSet_sub(live, HC_getOpcodeSrc0(instr));
                for (k = HCBitSet_getFirst(live); k != -1; k = HCBitSet_getNext(live, k))
                    HCAdjMtx_connect(mtx, HC_max(def, k), HC_min(def, k));
                HCBitSet_sub(live, def);
            }
            for (u = 0; u < nUses; ++u)
                HCBitSet_or(live, uses[u]);
        }
    }

    HCArena_release(mgr, &mark);
    return mtx;
}

static int coalescableInstr(HCInstr_s *instr)
{
    int hasImm = HC_opcodeHasImm(instr);
    int op = HC_getOpcodeOper(instr);

    if (op == HCOP_MOV && hasImm == 0)
        return 1;
    if (HC_getOpcodeArgCountOp(op) == 2 && hasImm == 0)
        return 1;
    if (HC_opcodeIsCommutative(instr) != 0
        && HC_getOpcodeTypeOp(op) == HCOP_TYPE_ALUOP
        && HC_getOpcodeArgCountOp(op) == 3)
    {
        return 1;
    }
    return 0;
}

static void coalesceRegisters(HCAdjMtx_s *adjMtx, int nSymRegs,
                              int nHardRegs, HCInstr_s *lst)
{
    int coalesced = 0;
    int i = 0;
    int prev;
    int uses[4];

    do
    {
        HCInstr_s *instr = &lst[i];
        if (coalescableInstr(instr) != 0)
        {
            int dst = HC_getOpcodeDst(instr);
            int s0  = HC_getOpcodeSrc0(instr);
            if (dst == -1)
                HC_assertFail("dst != HC_NIL", "hcCompiler.c", 3459, NULL);
            if (dst >= nHardRegs && s0 >= nHardRegs
                && HCAdjMtx_hasEdge(adjMtx, HC_max(dst, s0), HC_min(dst, s0)) == 0)
            {
                int j = 0;
                int prev2;
                int r;

                coalesced = 1;
                do
                {
                    HCInstr_s *cur = &lst[j];
                    int def   = HC_getDef(cur);
                    int nUses = HC_getRegUses(uses, cur);
                    int u;

                    for (u = 0; u < nUses; ++u)
                    {
                        if (uses[u] == s0)
                            HC_setUse(cur, u, dst);
                    }
                    if (def != -1 && def == s0)
                        HC_setOpcodeDst(cur, dst);
                    prev2 = j++;
                }
                while (HC_getOpcodeOper(&lst[prev2]) != HCOP_EOP);

                for (r = 0; r < nSymRegs; ++r)
                {
                    if (HCAdjMtx_hasEdge(adjMtx, HC_max(r, s0), HC_min(r, s0)) != 0)
                        HCAdjMtx_connect(adjMtx, HC_max(r, dst), HC_min(r, dst));
                }
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (coalesced != 0)
        compactInstructionList(lst);
}

static SpillCostInfo_s computeSpillCosts(HCArena_s *mgr, int invariantCount,
                                         int nHardRegs, HCInstr_s *lst)
{
    typedef struct { unsigned short loadCost, storeCost, cantSpill; } RegCostTmp_s;
    HCMemory_s topMem = HCArena_topInterface(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    int topMark = HCMemory_mark(&topMem);
    int botMark = HCMemory_mark(&botMem);
    int nSymRegs = HC_max(scanNumRegs(lst).numRegs, nHardRegs);
    HCAdjMtx_s *mtx = buildInterferenceGraph(mgr, lst, nHardRegs, nSymRegs);
    HCAdjList_s *graph;
    AdjEntry_s *adjLists;
    HCMark_s mark;
    RegCostTmp_s *tmp;
    HCBitSet_s *usedInBlk;
    HCBitSet_s *live;
    HCBitSet_s *liveOutInit;
    BasicBlock_s *blocks = NULL;
    int nBlocks;
    SpillCostInfo_s res;
    int uses[4];
    int i;
    int b;

    HCMemory_release(&topMem, topMark);
    graph = HCAdjList_create(&topMem, mtx);
    HCMemory_release(&botMem, botMark);

    adjLists = (AdjEntry_s *)HCMemory_alloc(
        &topMem, (unsigned int)((size_t)nSymRegs * sizeof(AdjEntry_s)));
    for (i = 0; i < nSymRegs; ++i)
        AdjEntry_init(&adjLists[i]);

    mark        = HCArena_mark(mgr);
    tmp         = (RegCostTmp_s *)HCMemory_alloc(&topMem, (unsigned int)((size_t)nSymRegs * sizeof(RegCostTmp_s)));
    usedInBlk   = HCBitSet_createNoClear(&topMem, nSymRegs);
    live        = HCBitSet_createNoClear(&topMem, nSymRegs);
    liveOutInit = HCBitSet_createNoClear(&topMem, nSymRegs);
    nBlocks     = HC_buildBasicBlocks(mgr, &blocks, lst);

    computeDominators(mgr, blocks, nBlocks);
    computeLoopNesting(mgr, blocks, nBlocks);
    liveness(mgr, blocks, nBlocks, nSymRegs, lst);
    memset(tmp, 0, (size_t)nSymRegs * sizeof(RegCostTmp_s));

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        unsigned short weight = (unsigned short)(1u << (3u * bb->nesting));
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;
        int m;

        HCBitSet_clear(usedInBlk);
        HCBitSet_copy(live, bb->liveOut);
        HCBitSet_copy(liveOutInit, live);

        for (pos = end; pos >= start; --pos)
        {
            HCInstr_s *instr = &lst[pos];
            int nUses = HC_getUses(instr, uses);
            int def   = HC_getDef(instr);
            int u;

            if (def != -1)
            {
                if (isInvariantLoad(instr, invariantCount) == 0)
                {
                    tmp[def].storeCost = (unsigned short)(tmp[def].storeCost + weight);
                    if (HC_isSpillStore(instr) != 0 || HC_isSpillLoad(instr) != 0)
                        tmp[def].storeCost = (unsigned short)(tmp[def].storeCost + 10u * weight);
                }
                if (HCBitSet_contains(usedInBlk, def) != 0)
                {
                    HCBitSet_sub(usedInBlk, def);
                    if (HCBitSet_contains(liveOutInit, def) == 0)
                        tmp[def].cantSpill = 1;
                }
                HCBitSet_sub(live, def);
            }

            for (u = 0; u < nUses; ++u)
            {
                if (HCBitSet_contains(live, uses[u]) == 0)
                {
                    for (m = HCBitSet_getFirst(usedInBlk);
                         m != -1;
                         m = HCBitSet_getNext(usedInBlk, m))
                    {
                        tmp[m].loadCost = (unsigned short)(tmp[m].loadCost + weight);
                    }
                    HCBitSet_clear(usedInBlk);
                    break;
                }
            }

            for (u = 0; u < nUses; ++u)
            {
                HCBitSet_or(live, uses[u]);
                HCBitSet_or(usedInBlk, uses[u]);
            }

            if (HC_isSpillStore(instr) != 0)
            {
                int dst = HC_getOpcodeDst(instr);
                tmp[dst].loadCost = (unsigned short)(tmp[dst].loadCost + 10u * weight);
            }
        }

        for (m = HCBitSet_getFirst(usedInBlk);
             m != -1;
             m = HCBitSet_getNext(usedInBlk, m))
        {
            tmp[m].loadCost = (unsigned short)(tmp[m].loadCost + weight);
        }
    }

    for (i = 0; i < nSymRegs; ++i)
    {
        int cost = 2 * (int)tmp[i].loadCost + 2 * (int)tmp[i].storeCost;
        if (tmp[i].cantSpill != 0 || i < nHardRegs)
            cost = HC_INFINITE_SPCOST;
        AdjEntry_setSpillCost(&adjLists[i], cost);
    }

    HCArena_release(mgr, &mark);
    res.numSymRegs = nSymRegs;
    res.adjLists   = adjLists;
    res.graph      = graph;
    return res;
}

static int symToReg(const AdjEntry_s *adjLists, const unsigned short *colorToReg,
                    int nHardRegs, int symReg)
{
    int col;
    if (symReg < nHardRegs)
        return symReg;
    col = (int)adjLists[symReg].color;
    if (col >= nHardRegs)
        HC_assertFail("col >= 0 && col < nHardRegs", "hcCompiler.c", 3740, NULL);
    return colorToReg[col];
}

static void assignColors(const AdjEntry_s *adjLists, const unsigned short *colorToReg,
                         int nHardRegs, HCInstr_s *lst)
{
    int i = 0;
    int prev;
    int uses[4];

    do
    {
        HCInstr_s *instr = &lst[i];
        int nUses = HC_getRegUses(uses, instr);
        int def   = HC_getDef(instr);
        int u;

        if (def != -1)
        {
            int newD = symToReg(adjLists, colorToReg, nHardRegs, def);
            if (def == 0 && newD != 0)
                HC_assertFail("newD == 0", "hcCompiler.c", 3760, NULL);
            HC_setOpcodeDst(instr, newD);
        }
        for (u = 0; u < nUses; ++u)
            HC_setUse(instr, u, symToReg(adjLists, colorToReg, nHardRegs, uses[u]));
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
}

static int compDisp(AdjEntry_s *entry, int *dispCount)
{
    if (entry->disp == 0x3FFFFFFu)
    {
        entry->disp = (unsigned int)(*dispCount);
        *dispCount += 4;
    }
    return (int)entry->disp;
}

static int Spill_equal(const SpillEntry_s *a, const SpillEntry_s *b)
{
    return a->loc == b->loc
        && a->reg == b->reg
        && a->disp == b->disp
        && a->type == b->type;
}

static void sortSpills(SpillEntry_s *a, int N)
{
    int i;
    if (a == NULL || N < 0)
        HC_assertFail("a && N >= 0", "hcCompiler.c", 3812, NULL);
    for (i = 1; i < N; ++i)
    {
        if (a[i - 1].loc > a[i].loc)
        {
            SpillEntry_s v = a[i];
            int j = i;
            do
            {
                if (a[j - 1].loc <= v.loc)
                    break;
                a[j] = a[j - 1];
                --j;
            }
            while (j != 0);
            a[j] = v;
        }
    }
}

static int reorderSpills(SpillEntry_s *spills, int numSpills,
                         int blockEnd, HCInstr_s *lst)
{
    int newSpills = 0;
    int i;
    (void)blockEnd;
    (void)lst;

    sortSpills(spills, numSpills);
    for (i = 1; i < numSpills; ++i)
    {
        if (Spill_equal(&spills[newSpills], &spills[i]) == 0)
            spills[++newSpills] = spills[i];
    }
    return (numSpills != 0) ? (newSpills + 1) : 0;
}

static void insertSpill(HCArena_s *mgr, SpillEntry_s *dst,
                        int loc, int reg, int disp, int type)
{
    if (reg == 0)
        HC_assertFail("reg != 0 && \"cannot spill r0\"", "hcCompiler.c", 3856, NULL);
    HCArena_growCurrent(mgr, (unsigned int)sizeof(SpillEntry_s));
    dst->loc  = (unsigned short)loc;
    dst->reg  = (unsigned short)reg;
    dst->disp = (unsigned short)disp;
    dst->type = (unsigned short)type;
}

static HCInstr_s *genSpillCode(HCArena_s *mgr, int *dispCount,
                               AdjEntry_s *adjLists, HCInstr_s *lst)
{
    HCMemory_s topMem = HCArena_topInterface(mgr);
    int topMark = HCMemory_mark(&topMem);
    BasicBlock_s *blocks = NULL;
    int nBlocks  = HC_buildBasicBlocks(mgr, &blocks, lst);
    int nSymRegs = scanNumRegs(lst).numRegs;
    HCBitSet_s *spilledInBlk = HCBitSet_createNoClear(&topMem, nSymRegs);
    HCBitSet_s *live         = HCBitSet_createNoClear(&topMem, nSymRegs);
    unsigned short *firstUse = (unsigned short *)HCMemory_alloc(&topMem, 2u * (unsigned int)nSymRegs);
    HCInstr_s *out;
    HCInstr_s *dst;
    int uses[4];
    int b;

    computeDominators(mgr, blocks, nBlocks);
    computeLoopNesting(mgr, blocks, nBlocks);
    liveness(mgr, blocks, nBlocks, nSymRegs, lst);

    out = (HCInstr_s *)HCArena_current(mgr);
    dst = out;

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int start   = BasicBlock_getStart(bb);
        int end     = BasicBlock_getEnd(bb);
        int botMark = HCArena_markBottom(mgr);
        int nSpills = 0;
        SpillEntry_s *spillsBot = (SpillEntry_s *)HCArena_current(mgr);
        SpillEntry_s *spillsTop;
        int blkTopMark;
        int sIdx;
        int pos;
        int m;

        HCBitSet_clear(spilledInBlk);
        HCBitSet_copy(live, bb->liveOut);

        for (pos = end; pos >= start; --pos)
        {
            HCInstr_s *instr = &lst[pos];
            int nUses = HC_getUses(instr, uses);
            int def   = HC_getDef(instr);
            int u;

            if (def != -1)
            {
                if (adjLists[def].spill != 0)
                {
                    int disp = compDisp(&adjLists[def], dispCount);
                    insertSpill(mgr, &spillsBot[nSpills++], pos, def, disp, 1);
                }
                HCBitSet_sub(spilledInBlk, def);
                HCBitSet_sub(live, def);
            }

            for (u = 0; u < nUses; ++u)
            {
                if (HCBitSet_contains(live, uses[u]) == 0)
                {
                    for (m = HCBitSet_getFirst(spilledInBlk);
                         m != -1;
                         m = HCBitSet_getNext(spilledInBlk, m))
                    {
                        if (adjLists[m].spill == 0)
                            HC_assertFail("adjLists[m].spill", "hcCompiler.c", 3936, NULL);
                        insertSpill(mgr, &spillsBot[nSpills++],
                                    firstUse[m], m, compDisp(&adjLists[m], dispCount), 0);
                    }
                    HCBitSet_clear(spilledInBlk);
                    break;
                }
            }

            for (u = 0; u < nUses; ++u)
            {
                int r = uses[u];
                HCBitSet_or(live, r);
                if (adjLists[r].spill != 0)
                {
                    HCBitSet_or(spilledInBlk, r);
                    firstUse[r] = (unsigned short)pos;
                }
            }
        }

        for (m = HCBitSet_getFirst(spilledInBlk);
             m != -1;
             m = HCBitSet_getNext(spilledInBlk, m))
        {
            if (adjLists[m].spill == 0)
                HC_assertFail("adjLists[m].spill", "hcCompiler.c", 3963, NULL);
            insertSpill(mgr, &spillsBot[nSpills++],
                        firstUse[m], m, compDisp(&adjLists[m], dispCount), 0);
        }

        blkTopMark = HCMemory_mark(&topMem);
        spillsTop = (SpillEntry_s *)HCMemory_alloc(
            &topMem, (unsigned int)((size_t)nSpills * sizeof(SpillEntry_s)));
        memcpy(spillsTop, spillsBot, (size_t)nSpills * sizeof(SpillEntry_s));
        HCArena_releaseBottom(mgr, botMark);
        nSpills = reorderSpills(spillsTop, nSpills, end, lst);

        if (HC_getOpcodeOper(&lst[start]) == HCOP_LABEL)
        {
            HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
            *dst++ = lst[start];
        }

        sIdx = 0;
        for (pos = start; pos <= end; ++pos)
        {
            int savedIdx = sIdx;
            HCInstr_s *instr = &lst[pos];

            while (sIdx < nSpills && (int)spillsTop[sIdx].loc <= pos)
            {
                if (spillsTop[sIdx].type == 0)
                {
                    HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
                    *dst++ = genSpillLoad(spillsTop[sIdx].reg, spillsTop[sIdx].disp);
                }
                ++sIdx;
            }
            if (HC_getOpcodeOper(instr) != HCOP_LABEL)
            {
                HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
                *dst++ = *instr;
            }
            for (sIdx = savedIdx; sIdx < nSpills && (int)spillsTop[sIdx].loc <= pos; ++sIdx)
            {
                if (spillsTop[sIdx].type == 1)
                {
                    if (HC_isSpillLoad(instr) &&
                        HC_getOpcodeDst(instr) == spillsTop[sIdx].reg &&
                        HC_getOpcodeImm(instr) == (int)spillsTop[sIdx].disp)
                        continue;
                    HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
                    *dst++ = genSpillStore(spillsTop[sIdx].reg, spillsTop[sIdx].disp);
                }
            }
        }
        HCMemory_release(&topMem, blkTopMark);
    }

    HCMemory_release(&topMem, topMark);
    return out;
}

static int minColor(HCMemory_s *mgr, HCBitSet_s *offStack,
                    const AdjEntry_s *adjLists, HCAdjList_s *igraph,
                    int nSymRegs, int nHardRegs,
                    const SpillRegMap_s *spillRegs,
                    const unsigned short *symToLoc, int r, int rotate)
{
    int mark = HCMemory_mark(mgr);
    HCBitSet_s *usedColors = HCBitSet_create(mgr, nHardRegs);
    unsigned short *edges  = (unsigned short *)HCMemory_alloc(mgr, 2u * (unsigned int)nSymRegs);
    int nEdges = HCAdjList_getEdges(igraph, r, edges);
    int colorBias[32];
    int bestColor;
    int i;

    memset(colorBias, 0, sizeof(colorBias));
    for (i = 0; i < nEdges; ++i)
    {
        int neigh = edges[i];
        if (HCBitSet_contains(offStack, neigh) != 0 && adjLists[neigh].color != 0x3Fu)
            HCBitSet_or(usedColors, (int)adjLists[neigh].color);
    }

    if (symToLoc[r] == 0xFFFFu)
    {
        bestColor = HCBitSet_getFirstUnset(usedColors);
        HCMemory_release(mgr, mark);
        return bestColor;
    }
    else
    {
        const SpillRegMap_s *sr = &spillRegs[symToLoc[r]];
        int bestScore = 0;
        int c;

        for (i = 0; i < sr->numRegs; ++i)
        {
            unsigned int col = adjLists[sr->regs[i]].color;
            if (col != 0x3Fu)
                ++colorBias[col];
        }
        bestColor = HCBitSet_getFirstUnset(usedColors);
        for (c = HCBitSet_getFirstUnset(usedColors);
             c != -1;
             c = HCBitSet_getNextUnset(usedColors, c))
        {
            if (colorBias[c] > bestScore)
            {
                bestColor = c;
                bestScore = colorBias[c];
            }
        }
        /* Distinct spill temporaries leave independent loads available to
         * the scheduler instead of repeatedly overwriting the first register. */
        if (bestScore == 0 && rotate)
        {
            int hint = r % HC_ARM11_SPILL_REGISTER_WINDOW;
            for (c = HCBitSet_getFirstUnset(usedColors); c != -1;
                 c = HCBitSet_getNextUnset(usedColors, c))
                if (c >= hint) { bestColor = c; break; }
        }
        HCMemory_release(mgr, mark);
        return bestColor;
    }
}

static int scanMaxSpillLoc(HCInstr_s *lst)
{
    int i = 0;
    int maxLoc = 0;
    int prev;

    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
        {
            int loc = HC_getOpcodeImm(instr);
            if (loc < 0)
                HC_assertFail("loc >= 0", "hcCompiler.c", 4156, NULL);
            if (maxLoc < loc)
                maxLoc = loc;
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    return maxLoc + 4;
}

static SpillRegMap_s *initializeSpillRegMapping(HCMemory_s *mgr,
                                                unsigned short *symToLoc,
                                                HCInstr_s *lst)
{
    int nSlots = scanMaxSpillLoc(lst) >> 2;
    unsigned int bytes = (unsigned int)((size_t)nSlots * sizeof(SpillRegMap_s));
    SpillRegMap_s *sr = (SpillRegMap_s *)HCMemory_alloc(mgr, bytes);
    int pass;
    int i;

    memset(sr, 0, bytes);
    for (pass = 0; pass < 2; ++pass)
    {
        int prev;
        i = 0;
        do
        {
            HCInstr_s *instr = &lst[i];
            if (HC_isSpillLoad(instr) != 0)
            {
                int slot = HC_getOpcodeImm(instr) >> 2;
                int dst  = HC_getOpcodeDst(instr);
                if (pass != 0)
                {
                    sr[slot].regs[sr[slot].numRegs++] = (unsigned short)dst;
                }
                else
                {
                    ++sr[slot].numRegs;
                    symToLoc[dst] = (unsigned short)slot;
                }
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

        if (pass == 0)
        {
            for (i = 0; i < nSlots; ++i)
            {
                if (sr[i].numRegs != 0)
                {
                    sr[i].regs = (unsigned short *)HCMemory_alloc(
                        mgr, 2u * (unsigned int)sr[i].numRegs);
                    sr[i].numRegs = 0;
                }
            }
        }
    }
    return sr;
}

static int assignRegisters(HCMemory_s *mgr, const unsigned short *stack,
                           int nSymRegs, AdjEntry_s *adjLists,
                           HCAdjList_s *igraph, int nHardRegs,
                           unsigned short *colorToReg, HCInstr_s *lst, int rotate)
{
    int ok = 1;
    int mark = HCMemory_mark(mgr);
    HCBitSet_s *offStack = HCBitSet_create(mgr, nSymRegs);
    int sp = nSymRegs;
    HCBitSet_s *chkDup   = HCBitSet_create(mgr, nSymRegs);
    unsigned short *symToLoc = (unsigned short *)HCMemory_alloc(mgr, 2u * (unsigned int)nSymRegs);
    SpillRegMap_s *spillRegs;
    int i;

    for (i = 0; i < nSymRegs; ++i)
        symToLoc[i] = 0xFFFFu;
    spillRegs = initializeSpillRegMapping(mgr, symToLoc, lst);

    while (sp > 0)
    {
        int r = stack[--sp];
        int col;

        if (HCBitSet_contains(chkDup, r) != 0)
            HC_assertFail("!HCBitSet_contains(chkDup, r)", "hcCompiler.c", 4255, NULL);
        HCBitSet_or(chkDup, r);
        HCBitSet_or(offStack, r);

        col = minColor(mgr, offStack, adjLists, igraph, nSymRegs, nHardRegs,
                       spillRegs, symToLoc, r, rotate);
        if (col == -1)
        {
            if (r < nHardRegs)
                HC_assertFail("r >= nHardRegs && \"cannot spill hard regs\"",
                              "hcCompiler.c", 4271, NULL);
            adjLists[r].spill = 1u;
            ok = 0;
        }
        else
        {
            if (r < nHardRegs)
                colorToReg[col] = (unsigned short)r;
            adjLists[r].color = (unsigned int)col & 0x3Fu;
        }
    }

    HCMemory_release(mgr, mark);
    return ok;
}

static int hcModulo(int x, int y)
{
    if (x < 0)
        HC_assertFail("x >= 0", "hcCompiler.c", 4283, NULL);
    if (y <= 0)
        HC_assertFail("y > 0", "hcCompiler.c", 4284, NULL);
    return x % y;
}

static int pruneGraph(HCArena_s *mgr, HCInstr_s **outputLst,
                      AdjEntry_s *adjLists, HCAdjList_s *igraph,
                      unsigned short *colorToReg, int *dispCount,
                      int nHardRegs, int nSymRegs, HCInstr_s *lst, int rotate)
{
    HCMark_s fullMark = HCArena_mark(mgr);
    HCMemory_s topMem = HCArena_topInterface(mgr);
    int botMark = HCArena_markBottom(mgr);
    int stackSize = 0;
    unsigned short *stack     = (unsigned short *)HCMemory_alloc(&topMem, 2u * (unsigned int)nSymRegs);
    HCBitSet_s     *onStack   = HCBitSet_create(&topMem, nSymRegs);
    unsigned short *edges     = (unsigned short *)HCMemory_alloc(&topMem, 2u * (unsigned int)nSymRegs);
    unsigned short *degrees   = (unsigned short *)HCMemory_alloc(&topMem, 2u * (unsigned int)nSymRegs);
    HCBitSet_s     *lowNodes  = HCBitSet_create(&topMem, nSymRegs);
    HCBitSet_s     *highNodes = HCBitSet_create(&topMem, nSymRegs);
    int success;
    int i;

    for (i = 0; i < nSymRegs; ++i)
    {
        int nEdges = HCAdjList_getEdges(igraph, i, edges);
        int spCost = AdjEntry_getSpillCost(&adjLists[i]);
        int deg = 0;
        int e;

        for (e = 0; e < nEdges; ++e)
        {
            if (HCBitSet_contains(onStack, edges[e]) == 0)
                ++deg;
        }
        degrees[i] = (unsigned short)deg;
        if (spCost != HC_INFINITE_SPCOST)
            HCBitSet_or((deg >= nHardRegs) ? highNodes : lowNodes, i);
    }

    for (;;)
    {
        int spillNode;
        int spillNeigh;
        int spillCost;
        int numTies;
        int first;
        int hNode;

        while (HCBitSet_numElements(lowNodes) > 0)
        {
            int node = HCBitSet_getFirst(lowNodes);
            int nEdges;
            int e;

            if (HCBitSet_contains(onStack, node) != 0)
                HC_assertFail("!HCBitSet_contains(onStack, node)", "hcCompiler.c", 4352, NULL);
            HCBitSet_or(onStack, node);
            stack[stackSize++] = (unsigned short)node;
            HCBitSet_sub(lowNodes, node);
            if (HCBitSet_contains(highNodes, node) != 0)
                HC_assertFail("!HCBitSet_contains(highNodes, node)", "hcCompiler.c", 4358, NULL);

            nEdges = HCAdjList_getEdges(igraph, node, edges);
            for (e = 0; e < nEdges; ++e)
            {
                int neigh = edges[e];
                if (HCBitSet_contains(onStack, neigh) == 0)
                {
                    int degree = (int)degrees[neigh] - 1;
                    if (degree < 0)
                        HC_assertFail("degree >= 0", "hcCompiler.c", 4369, NULL);
                    if (degree == nHardRegs - 1
                        && AdjEntry_getSpillCost(&adjLists[neigh]) != HC_INFINITE_SPCOST)
                    {
                        if (HCBitSet_contains(onStack, neigh) != 0)
                            HC_assertFail("!HCBitSet_contains(onStack, neigh)",
                                          "hcCompiler.c", 4374, NULL);
                        HCBitSet_sub(highNodes, neigh);
                        HCBitSet_or(lowNodes, neigh);
                    }
                    if (degree < nHardRegs && HCBitSet_contains(highNodes, neigh) != 0)
                        HC_assertFail("!HCBitSet_contains(highNodes, neigh)",
                                      "hcCompiler.c", 4380, NULL);
                    degrees[neigh] = (unsigned short)degree;
                }
            }
        }

        if (HCBitSet_numElements(highNodes) == 0)
            break;

        spillNode  = -1;
        spillNeigh = -1;
        spillCost  = 0;
        numTies    = 0;
        first      = 1;

        for (hNode = HCBitSet_getFirst(highNodes);
             hNode != -1;
             hNode = HCBitSet_getNext(highNodes, hNode))
        {
            int nNeigh = degrees[hNode];
            int spCost = AdjEntry_getSpillCost(&adjLists[hNode]);

            if (HCBitSet_contains(onStack, hNode) != 0)
                HC_assertFail("!HCBitSet_contains(onStack, hNode)", "hcCompiler.c", 4407, NULL);
            if (spCost == HC_INFINITE_SPCOST)
                HC_assertFail("spCost != HC_INFINITE_SPCOST", "hcCompiler.c", 4408, NULL);
            if (nNeigh == 0)
                HC_assertFail("nNeigh", "hcCompiler.c", 4409, NULL);

            if (first != 0)
            {
                first      = 0;
                spillNeigh = nNeigh;
                spillCost  = spCost;
                spillNode  = hNode;
            }

            if (nNeigh > 0)
            {
#if !defined(NDEBUG) && !defined(HG_NO_ASSERTS)
                if (HCBitSet_contains(onStack, hNode) != 0)
                    HC_assertFail("!HCBitSet_contains(onStack, hNode)", "hcCompiler.c", 4421, NULL);
                if (!((double)spCost * (double)spillNeigh < (double)0x7FFFFFFF))
                    HC_assertFail("(double)spCost*(double)spillNeigh < (double)0x7FFFFFFF",
                                  "hcCompiler.c", 4422, NULL);
                if (!((double)spillCost * (double)nNeigh < (double)0x7FFFFFFF))
                    HC_assertFail("(double)spillCost*(double)nNeigh < (double)0x7FFFFFFF",
                                  "hcCompiler.c", 4423, NULL);
#endif

                /* The reference assigns the candidate before testing for a
                 * tie, making the equality below always true. Keep the PRNG
                 * consumption and selection order (x86 0x459D5F..0x459DDD). */
                if (spillNeigh * spCost <= nNeigh * spillCost && hNode >= nHardRegs)
                {
                    int rnd;
                    spillNode  = hNode;
                    spillCost  = spCost;
                    spillNeigh = nNeigh;
                    rnd = HCRandom_get(&mgr->random);
                    if (spillNeigh * spCost == nNeigh * spillCost)
                        ++numTies;
                    else
                        numTies = 0;
                    if (numTies == hcModulo(rnd & 0x7FFFFFFF, numTies + 1))
                    {
                        spillNode  = hNode;
                        spillCost  = spCost;
                        spillNeigh = nNeigh;
                    }
                }
            }
        }

        if (spillNode != -1)
        {
            if (HCBitSet_contains(onStack, spillNode) != 0)
                HC_assertFail("!HCBitSet_contains(onStack, spillNode)", "hcCompiler.c", 4454, NULL);
            if (spillNode < nHardRegs || spillNode >= nSymRegs)
                HC_assertFail("spillNode >= nHardRegs && spillNode < nSymRegs",
                              "hcCompiler.c", 4455, NULL);
            HCBitSet_sub(highNodes, spillNode);
            HCBitSet_or(lowNodes, spillNode);
        }
    }

    if (HCBitSet_numElements(highNodes) != 0)
        HC_assertFail("HCBitSet_numElements(highNodes) == 0", "hcCompiler.c", 4462, NULL);

    for (i = nSymRegs - 1; i >= 0; --i)
    {
        if (AdjEntry_getSpillCost(&adjLists[i]) == HC_INFINITE_SPCOST)
        {
            if (HCBitSet_contains(onStack, i) != 0)
                HC_assertFail("!HCBitSet_contains(onStack, i)", "hcCompiler.c", 4471, NULL);
            stack[stackSize++] = (unsigned short)i;
        }
    }
    if (stackSize != nSymRegs)
        HC_assertFail("stackSize == nSymRegs", "hcCompiler.c", 4477, NULL);

    {
        unsigned int *savedColors = NULL;
        if (rotate)
        {
            savedColors = (unsigned int *)HCMemory_alloc(&topMem, sizeof(*savedColors) * (unsigned int)nSymRegs);
            for (i = 0; i < nSymRegs; ++i) savedColors[i] = adjLists[i].color;
        }
        success = assignRegisters(&topMem, stack, nSymRegs, adjLists, igraph,
                                  nHardRegs, colorToReg, lst, rotate);
        if (!success && rotate)
        {
            for (i = 0; i < nSymRegs; ++i)
            {
                adjLists[i].spill = 0;
                adjLists[i].color = savedColors[i];
            }
            success = assignRegisters(&topMem, stack, nSymRegs, adjLists, igraph,
                                      nHardRegs, colorToReg, lst, 0);
        }
    }
    HCArena_releaseBottom(mgr, botMark);
    if (success != 0)
    {
        assignColors(adjLists, colorToReg, nHardRegs, lst);
        *outputLst = lst;
        HCArena_release(mgr, &fullMark);
    }
    else
    {
        *outputLst = genSpillCode(mgr, dispCount, adjLists, lst);
    }
    return success;
}

static HCInstr_s *registerAllocation(HCArena_s *mgr, HCInstr_s *lst,
                                     int spillStart, int nHardRegs, int rotate)
{
    int round = 0;
    int dispCount = spillStart;
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    HCMemory_s topMem = HCArena_topInterface(mgr);
    int lastRoundMrk = -1;
    unsigned short colorToReg[HC_MAX_HARD_REGS];
    int colored;

    memset(colorToReg, 0, sizeof(colorToReg));
    do
    {
        int prevInstrs = scanNumRegs(lst).numInstrs;
        int shrunk;
        int topMrk;
        int botMrk;
        SpillCostInfo_s sc;
        HCInstr_s *outLst = NULL;

        do
        {
            HCMark_s iterMark = HCArena_mark(mgr);
            int nSymRegs = liveRangeAnalysis(mgr, lst, nHardRegs);
            HCAdjMtx_s *mtx = buildInterferenceGraph(mgr, lst, nHardRegs, nSymRegs);
            int curInstrs;

            coalesceRegisters(mtx, nSymRegs, nHardRegs, lst);
            shrunk = 0;
            curInstrs = scanNumRegs(lst).numInstrs;
            if (curInstrs < prevInstrs)
            {
                shrunk = 1;
                prevInstrs = curInstrs;
            }
            HCArena_release(mgr, &iterMark);
        }
        while (shrunk != 0);

        topMrk = HCMemory_mark(&topMem);
        botMrk = HCMemory_mark(&botMem);
        sc = computeSpillCosts(mgr, spillStart, nHardRegs, lst);
        colored = pruneGraph(mgr, &outLst, sc.adjLists, sc.graph,
                             colorToReg, &dispCount, nHardRegs, sc.numSymRegs, lst, rotate);
        if (colored != 0)
        {
            HCMemory_release(&botMem, botMrk);
            HCMemory_release(&topMem, topMrk);
        }
        else
        {
            HCInstr_s *hiLst = NULL;
            HCMemory_release(&topMem, topMrk);
            if (round != 0)
            {
                HCMemory_release(&topMem, lastRoundMrk);
            }
            else if (lastRoundMrk != -1)
            {
                HC_assertFail("lastRoundMrk == HC_NIL", "hcCompiler.c", 4571, NULL);
            }
            lastRoundMrk = HCMemory_mark(&topMem);
            swapToHiMem(mgr, &hiLst, outLst);
            HCMemory_release(&botMem, botMrk);
            lst = hiLst;
            if (++round >= 10)
                HC_assertFail("round < 10", "hcCompiler.c", 4582, NULL);
        }
    }
    while (colored == 0);

    return lst;
}

/* --- post-regalloc lowering, spill optimization, scheduler & driver ------- */

static int fixImmediates(HCArena_s *mgr, HCBackend_s *be, HCInstr_s **source)
{
    HCInstr_s *src = *source;
    int botMark = HCArena_markBottom(mgr);
    HCInstr_s *dst = (HCInstr_s *)HCArena_current(mgr);
    int tmpReg = scanNumRegs(src).numRegs;
    int i = 0;
    int prev;

    *source = dst;
    do
    {
        HCInstr_s *instr = &src[i];
        if (HC_opcodeHasImm(instr) == 0
            || HC_getOpcodeOper(instr) == HCOP_MOV
            || HC_getOpcodeArgCount(instr) < 2
            || be->canCreateImmediate(instr) != 0)
        {
            HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
            *dst++ = *instr;
        }
        else
        {
            HCInstr_s rewritten = *instr;
            HCArena_growCurrent(mgr, 2u * (unsigned int)sizeof(HCInstr_s));
            *dst++ = HC_genOp2Imm(HCOP_MOV, tmpReg, HC_getOpcodeImm(instr));
            HC_setOpcodeNoImm(&rewritten);
            if (HC_getOpcodeArgCount(instr) == 2)
            {
                HC_setOpcodeSrc0(&rewritten, tmpReg);
            }
            else
            {
                if (HC_getOpcodeArgCount(instr) != 3)
                    HC_assertFail("HC_getOpcodeArgCount(instr) == 3", "hcCompiler.c", 4637, NULL);
                HC_setOpcodeSrc1(&rewritten, tmpReg);
            }
            *dst++ = rewritten;
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&src[prev]) != HCOP_EOP);

    return botMark;
}

static unsigned int *getFreeHardRegMasks(HCArena_s *mgr, BasicBlock_s *basicBlocks,
                                         int numBlocks, HCInstr_s *lst, int nHardRegs)
{
    RegInstrCount_s counts = scanNumRegs(lst);
    int numInstrs = counts.numInstrs;
    int numRegs   = counts.numRegs;
    unsigned int *freeMasks = (unsigned int *)HCArena_allocTop(
        mgr, 4u * (unsigned int)numInstrs);
    unsigned int *liveMasks = (unsigned int *)HCArena_allocTop(
        mgr, 4u * (unsigned int)numInstrs);
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    HCBitSet_s *live = HCBitSet_create(&botMem, numRegs);
    unsigned int hardMask = (1u << nHardRegs) - 1u;
    int uses[4];
    int b;

    for (b = numBlocks - 1; b >= 0; --b)
    {
        BasicBlock_s *bb = &basicBlocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;

        HCBitSet_copy(live, bb->liveOut);
        for (pos = end; pos >= start; --pos)
        {
            HCInstr_s *instr = &lst[pos];
            int nUses = HC_getUses(instr, uses);
            int def   = HC_getDef(instr);
            unsigned int usedMask = 0;
            unsigned int outMask  = 0;
            int r;
            int u;

            for (r = HCBitSet_getFirst(live); r != -1; r = HCBitSet_getNext(live, r))
                outMask |= (1u << (r & 31));

            if (def != -1)
            {
                HCBitSet_sub(live, def);
                usedMask |= (1u << (def & 31));
            }
            for (u = 0; u < nUses; ++u)
            {
                HCBitSet_or(live, uses[u]);
                usedMask |= (1u << (uses[u] & 31));
            }
            for (r = HCBitSet_getFirst(live); r != -1; r = HCBitSet_getNext(live, r))
                usedMask |= (1u << (r & 31));

            freeMasks[pos] = (~usedMask) & hardMask;
            liveMasks[pos] = outMask;
        }
    }

    HCArena_release(mgr, &mark);
    return freeMasks;
}

HCBackendInfo_s computeBackEndInfo(HCArena_s *mgr, HCInstr_s *lst, int nHardRegs)
{
    HCBackendInfo_s info;
    int botMark = HCArena_markBottom(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);

    liveness(mgr, blocks, nBlocks, 0, lst);
    info.instructions = lst;
    info.freeRegMasks = getFreeHardRegMasks(mgr, blocks, nBlocks, lst, nHardRegs);
    /* Neither backend uses liveRegs; the reference leaves it uninitialized
     * (x86 0x45A6C0). */
    info.liveRegs         = NULL;
    computeDUChains(mgr, &info.chains, blocks, nBlocks, lst, 3u);
    HCArena_releaseBottom(mgr, botMark);
    return info;
}

static int preRegallocFix(HCArena_s *mgr, HCBackend_s *be,
                          HCInstr_s **source, int nHardRegs)
{
    (void)nHardRegs;
    if (be == NULL)
        HC_assertFail("be", "hcCompiler.c", 4755, NULL);
    return be->preRegallocFixer(mgr, source, *source, NULL);
}

static int postRegallocFix(HCArena_s *mgr, HCBackend_s *be,
                           HCInstr_s **source, int nHardRegs)
{
    if (be == NULL)
        HC_assertFail("be", "hcCompiler.c", 4798, NULL);
    if (be->postRegallocFixer != NULL)
    {
        HCInstr_s *src = *source;
        int topMark = HCArena_markTop(mgr);
        HCBackendInfo_s info = computeBackEndInfo(mgr, src, nHardRegs);
        int res = be->postRegallocFixer(mgr, source, src, &info);
        HCArena_releaseTop(mgr, topMark);
        return res;
    }
    else
    {
        HCInstr_s *low = NULL;
        int mark = swapToLowMem(mgr, &low, *source);
        *source = low;
        return mark;
    }
}

static void coalesceSpillLocations(HCArena_s *mgr, int spillStart, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    hcChains_s chains;
    HCBitSet_s *storeSet = NULL;
    unsigned short *remap;
    int maxSlot = 0;
    int modified = 0;
    int uses[4];
    int pass;
    int i;
    int prev;

    computeDUChains(mgr, &chains, blocks, nBlocks, lst, 3u);

    for (pass = 0; pass < 2; ++pass)
    {
        i = 0;
        if (pass == 1)
        {
            ++maxSlot;
            storeSet = HCBitSet_create(&botMem, 2 * maxSlot);
        }
        do
        {
            HCInstr_s *instr = &lst[i];
            if (HC_isSpillStore(instr) != 0)
            {
                int loc = HC_getOpcodeImm(instr);
                int slot;
                if ((loc & 3) != 0 || loc < 0)
                    HC_assertFail("(loc & 3) == 0 && !(loc & 0x80000000)",
                                  "hcCompiler.c", 4934, NULL);
                slot = loc >> 2;
                if (pass != 0)
                {
                    if (HCBitSet_contains(storeSet, 2 * slot) != 0)
                        HCBitSet_or(storeSet, 2 * slot + 1);
                    HCBitSet_or(storeSet, 2 * slot);
                }
                else if (maxSlot < slot)
                {
                    maxSlot = slot;
                }
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    }

    remap = (unsigned short *)HCArena_allocBottom(mgr, 2u * (unsigned int)maxSlot);
    for (i = 0; i < maxSlot; ++i)
        remap[i] = 0xFFFFu;

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        UDChainStub_s *perUse = chains.udChains[i];

        if (perUse != NULL && HC_isSpillStore(instr) != 0 &&
            HC_getOpcodeImm(instr) >= spillStart)
        {
            int storeSlot = HC_getOpcodeImm(instr) >> 2;
            if (HCBitSet_contains(storeSet, 2 * storeSlot + 1) == 0)
            {
                int nUses = HC_getUses(instr, uses);
                int valReg = HC_getOpcodeDst(instr);
                DUChain_s *ud = NULL;
                int u;

                for (u = 0; u < nUses; ++u)
                {
                    if (perUse->chains[u] != NULL && valReg == uses[u])
                    {
                        ud = perUse->chains[u];
                        break;
                    }
                }
                if (ud != NULL && ud->length == 1)
                {
                    int defSite = ud->values[0];
                    DUChain_s *du;
                    if (chains.duChains[defSite] == 0)
                        HC_assertFail("chains.duChains[defSite]", "hcCompiler.c", 4991, NULL);
                    du = (DUChain_s *)DUChains_getDUChain(&chains, defSite);
                    if (du->length == 1)
                    {
                        HCInstr_s *defInstr = &lst[defSite];
                        if (isInvariantLoad(defInstr, spillStart) != 0)
                        {
                            remap[storeSlot] = (unsigned short)(HC_getOpcodeImm(defInstr) >> 2);
                            *defInstr = HC_genOp0(HCOP_NOP);
                            *instr    = HC_genOp0(HCOP_NOP);
                            modified  = 1;
                        }
                    }
                }
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_isSpillLoad(instr) != 0)
        {
            int spillLoc = HC_getOpcodeImm(instr);
            if ((spillLoc & 3) != 0)
                HC_assertFail("(spillLoc & 3) == 0", "hcCompiler.c", 5016, NULL);
            if (spillLoc < 4 * maxSlot)
            {
                int mapped = (short)remap[spillLoc >> 2];
                if (mapped != -1)
                    HC_setOpcodeImm(instr, 4 * mapped);
            }
        }
        if (HC_isSpillStore(instr) != 0 && HC_getOpcodeImm(instr) < 0)
            HC_assertFail("HC_getOpcodeImm(instr)>=0", "hcCompiler.c", 5025, NULL);
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    if (modified != 0)
        compactInstructionList(lst);
    HCArena_release(mgr, &mark);
}

static int isInvariantNonConstLoad(HCInstr_s *instr,
                                   const unsigned char *inputParamMutable,
                                   int spillStart)
{
    unsigned int loc;
    if (inputParamMutable == NULL || isInvariantLoad(instr, spillStart) == 0)
        return 0;
    loc = (unsigned int)HC_getOpcodeImm(instr);
    return ((int)inputParamMutable[loc >> 5] >> ((loc >> 2) & 7u)) & 1;
}

static void optimizeLoopConstantLoads(HCArena_s *mgr,
                                      const unsigned char *inputParamMutable,
                                      int spillStart, HCInstr_s *lst)
{
    int maxLoc = scanMaxSpillLoc(lst) >> 2;
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    HCBitSet_s *loadCounts = HCBitSet_create(&botMem, 2 * maxLoc);
    unsigned short *remap  = (unsigned short *)HCMemory_alloc(&botMem, 2u * (unsigned int)maxLoc);
    int i;
    int prev;

    for (i = 0; i < maxLoc; ++i)
        remap[i] = 0xFFFFu;

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        if (isInvariantNonConstLoad(instr, inputParamMutable, spillStart) != 0)
        {
            int loc = HC_getOpcodeImm(instr) >> 2;
            if (HCBitSet_contains(loadCounts, 2 * loc) != 0)
                HCBitSet_or(loadCounts, 2 * loc + 1);
            HCBitSet_or(loadCounts, 2 * loc);
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    for (i = 0; ; ++i)
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr);
        if (op == HCOP_LABEL || HC_isCondBranchOp(op) != 0 || op == HCOP_EOP)
            break;
        if (HC_isSpillStore(instr) != 0 && HC_getOpcodeImm(instr) >= spillStart)
        {
            int dstSlot = HC_getOpcodeImm(instr) >> 2;
            int valReg  = HC_getOpcodeDst(instr);
            int k;

            for (k = i - 1; k >= 0; --k)
            {
                HCInstr_s *prevInstr = &lst[k];
                if (isInvariantNonConstLoad(prevInstr, inputParamMutable, spillStart) != 0
                    && valReg == HC_getOpcodeDst(prevInstr))
                {
                    int srcSlot = HC_getOpcodeImm(prevInstr) >> 2;
                    if (HCBitSet_contains(loadCounts, 2 * srcSlot) != 0
                        && HCBitSet_contains(loadCounts, 2 * srcSlot + 1) == 0)
                    {
                        if (srcSlot < 0 || srcSlot >= maxLoc)
                            HC_assertFail("loc >= 0 && loc < maxLoc", "hcCompiler.c", 5157, NULL);
                        remap[dstSlot] = (unsigned short)srcSlot;
                    }
                    break;
                }
                if (valReg == HC_getDef(prevInstr))
                    break;
            }
        }
    }

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
        {
            int slot = HC_getOpcodeImm(instr) >> 2;
            if (remap[slot] != 0xFFFFu)
                HC_setOpcodeImm(instr, 4 * (int)remap[slot]);
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    HCArena_release(mgr, &mark);
}

static void removeRedundantSpills(HCArena_s *mgr, HCInstr_s *lst)
{
    int modified = 0;
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    HCBitSet_s *slotNeeded = HCBitSet_create(&botMem, scanMaxSpillLoc(lst) >> 2);
    int nRegs = scanNumRegs(lst).numRegs;
    HCBitSet_s *live = HCBitSet_createNoClear(&botMem, nRegs);
    int regSlot[HC_MAX_HARD_REGS];
    int uses[4];
    int b;

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int j;
        int r;

        HCBitSet_set(slotNeeded);
        for (r = 0; r < HC_MAX_HARD_REGS; ++r)
            regSlot[r] = -1;

        for (j = start; j <= end; ++j)
        {
            HCInstr_s *instr = &lst[j];
            if (HC_isSpillStore(instr) != 0 && HC_getOpcodeImm(instr) < 0)
                HC_assertFail("HC_getOpcodeImm(instr) >= 0", "hcCompiler.c", 5227, NULL);

            if (HC_isSpillLoad(instr) != 0)
            {
                int reg  = HC_getOpcodeDst(instr);
                int offs = HC_getOpcodeImm(instr);
                if (reg < 0 || reg >= HC_MAX_HARD_REGS)
                    HC_assertFail("reg >= 0 && reg < HC_MAX_HARD_REGS", "hcCompiler.c", 5233, NULL);
                if (offs < 0)
                    HC_assertFail("offs >= 0", "hcCompiler.c", 5234, NULL);
                if (offs == regSlot[reg])
                {
                    *instr = HC_genOp0(HCOP_NOP);
                    modified = 1;
                }
                else
                {
                    for (r = 1; r < HC_MAX_HARD_REGS; ++r)
                    {
                        if (r != reg && regSlot[r] == offs)
                        {
                            *instr = HC_genOp2(HCOP_MOV, reg, r);
                            break;
                        }
                    }
                    regSlot[reg] = offs;
                }
            }
            else
            {
                int d = HC_getDef(instr);
                if (d != -1)
                {
                    if (d < 0 || d >= HC_MAX_HARD_REGS)
                        HC_assertFail("d >= 0 && d < HC_MAX_HARD_REGS", "hcCompiler.c", 5252, NULL);
                    if (d == 0)
                        for (r = 0; r < HC_MAX_HARD_REGS; ++r) regSlot[r] = -1;
                    else
                        regSlot[d] = -1;
                }
                if (HC_isSpillStore(instr) != 0)
                {
                    int loc = HC_getOpcodeImm(instr);
                    if (loc < 0)
                        HC_assertFail("loc >= 0", "hcCompiler.c", 5262, NULL);
                    for (r = 0; r < HC_MAX_HARD_REGS; ++r)
                    {
                        if (regSlot[r] >= 0 &&
                            ((unsigned int)(loc - regSlot[r]) < 4u ||
                             (unsigned int)(regSlot[r] - loc) < 4u))
                            regSlot[r] = -1;
                    }
                    regSlot[HC_getOpcodeDst(instr)] = loc;
                }
                else if (HC_isStoreOp(HC_getOpcodeOper(instr)))
                {
                    /* A pointer-based or partial-width write may alias any slot. */
                    for (r = 0; r < HC_MAX_HARD_REGS; ++r) regSlot[r] = -1;
                }
            }
        }

        for (j = end; j >= start; --j)
        {
            HCInstr_s *instr = &lst[j];
            if (j < 0)
                HC_assertFail("j >= 0", "hcCompiler.c", 5280, NULL);
            if (HC_isSpillStore(instr) != 0)
            {
                int offs = HC_getOpcodeImm(instr) >> 2;
                if (offs < 0)
                    HC_assertFail("offs >= 0", "hcCompiler.c", 5286, NULL);
                if (HCBitSet_contains(slotNeeded, offs) == 0)
                {
                    *instr = HC_genOp0(HCOP_NOP);
                    modified = 1;
                }
                HCBitSet_sub(slotNeeded, offs);
            }
            else if (HC_isSpillLoad(instr) != 0)
            {
                HCBitSet_or(slotNeeded, HC_getOpcodeImm(instr) >> 2);
            }
            else if (HC_getOpcodeType(instr) == HCOP_TYPE_MEMOP || HC_getDef(instr) == 0)
            {
                HCBitSet_set(slotNeeded);
            }
        }
    }

    liveness(mgr, blocks, nBlocks, nRegs, lst);

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int j;

        HCBitSet_copy(live, bb->liveOut);
        for (j = end; j >= start; --j)
        {
            HCInstr_s *instr;
            int nUses;
            int def;
            int u;

            if (j != start)
            {
                HCInstr_s *prevInstr = &lst[j - 1];
                HCInstr_s *curInstr  = &lst[j];
                if (HC_isSpillLoad(prevInstr) != 0
                    && HC_isSpillStore(curInstr) != 0
                    && HC_getOpcodeImm(prevInstr) == HC_getOpcodeImm(curInstr)
                    && HC_getOpcodeDst(prevInstr) == HC_getOpcodeDst(curInstr))
                {
                    int reg = HC_getOpcodeDst(curInstr);
                    if (HCBitSet_contains(live, reg) == 0)
                        *prevInstr = HC_genOp0(HCOP_NOP);
                    *curInstr = HC_genOp0(HCOP_NOP);
                    modified = 1;
                }
            }

            instr = &lst[j];
            nUses = HC_getUses(instr, uses);
            def   = HC_getDef(instr);
            if (def != -1)
                HCBitSet_sub(live, def);
            for (u = 0; u < nUses; ++u)
                HCBitSet_or(live, uses[u]);
        }
    }

    HCArena_release(mgr, &mark);
    if (modified != 0)
        compactInstructionList(lst);
}

static void packSpillLocations(HCArena_s *mgr, HCInstr_s *lst, int spillStart)
{
    int maxLoc = scanMaxSpillLoc(lst);
    HCMark_s mark = HCArena_mark(mgr);
    HCMemory_s botMem = HCArena_bottomInterface(mgr);
    unsigned short *remap = (unsigned short *)HCMemory_alloc(&botMem, 2u * (unsigned int)maxLoc);

    if (maxLoc != 0)
    {
        HCBitSet_s *used = HCBitSet_create(&botMem, scanMaxSpillLoc(lst) >> 2);
        int i = 0;
        int prev;
        int newSlot = 0;
        int slot;

        do
        {
            HCInstr_s *instr = &lst[i];
            if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
            {
                int loc = HC_getOpcodeImm(instr);
                if (loc >= spillStart)
                    HCBitSet_or(used, loc >> 2);
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

        for (slot = HCBitSet_getFirst(used); slot != -1; slot = HCBitSet_getNext(used, slot))
            remap[slot] = (unsigned short)(newSlot++);

        i = 0;
        do
        {
            HCInstr_s *instr = &lst[i];
            if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
            {
                int loc = HC_getOpcodeImm(instr);
                if (loc >= spillStart)
                    HC_setOpcodeImm(instr, spillStart + 4 * (int)remap[loc >> 2]);
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

        HCArena_release(mgr, &mark);
    }
}

static void offsetStackReferences(HCBackend_s *be, int stackParms, HCInstr_s *lst)
{
    if (be != NULL)
    {
        int offs = HC_extraStackSpace(be);
        int i = 0;
        int prev;

        do
        {
            HCInstr_s *instr = &lst[i];
            if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
            {
                int imm;
                if (HC_opcodeHasImm(instr) == 0)
                    HC_assertFail("HC_opcodeHasImm(instr)", "hcCompiler.c", 5428, NULL);
                imm = HC_getOpcodeImm(instr);
                if (stackParms != 0 || imm != 0)
                    HC_setOpcodeImm(instr, offs + imm);
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    }
}

static void remapRegisters(HCBackend_s *be, HCInstr_s *lst)
{
    if (be != NULL && be->targetRegisterMapping != NULL)
    {
        const unsigned char *map = be->targetRegisterMapping();
        int i = 0;
        int prev;
        int uses[4];

        do
        {
            HCInstr_s *instr = &lst[i];
            int def   = HC_getDef(instr);
            int nUses = HC_getRegUses(uses, instr);
            int u;

            if (def != -1)
            {
                if (def < 0 || def >= be->numHardRegs())
                    HC_assertFail("def >= 0 && def < be->numHardRegs()",
                                  "hcCompiler.c", 5460, NULL);
                HC_setOpcodeDst(instr, map[def]);
            }
            for (u = 0; u < nUses; ++u)
            {
                if (uses[u] < 0 || uses[u] >= be->numHardRegs())
                    HC_assertFail("use >= 0 && use < be->numHardRegs()",
                                  "hcCompiler.c", 5466, NULL);
                HC_setUse(instr, u, map[uses[u]]);
            }
            prev = i++;
        }
        while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
    }
}

static void computeInputParamMask(unsigned char *inputParamsUsed,
                                  int numStackParams, HCInstr_s *lst)
{
    int i = 0;
    int prev;

    memset(inputParamsUsed, 0, (unsigned int)(numStackParams + 7) >> 3);
    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
        {
            int loc = HC_getOpcodeImm(instr) >> 2;
            if (loc < numStackParams && loc >= 0)
                inputParamsUsed[loc >> 3] |= (unsigned char)(1u << (loc & 7));
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);
}

static int conflicts(HCInstr_s *a, HCInstr_s *b)
{
    int usesA[4];
    int usesB[4];
    int defA  = HC_getDef(a);
    int defB  = HC_getDef(b);
    int nUseA = HC_getUses(a, usesA);
    int nUseB = HC_getUses(b, usesB);
    int opA   = HC_getOpcodeOper(a);
    int opB   = HC_getOpcodeOper(b);

    if (HC_isBranchOp(opA) != 0 || HC_isBranchOp(opB) != 0
        || opA == HCOP_LABEL || opB == HCOP_LABEL)
    {
        return 1;
    }
    if (HC_isStoreOp(opA) != 0 || HC_isStoreOp(opB) != 0)
        return 1;
    if (HC_getOpcodeOper(a) == HCOP_LABEL || HC_getOpcodeOper(a) == HCOP_EOP)
        return 1;
    if (HC_containsInt(usesB, nUseB, defA) != 0)
        return 1;
    if (HC_containsInt(usesA, nUseA, defB) != 0)
        return 1;
    return defA == defB && defA != -1;
}

static int conflictingMove(HCInstr_s *lst, int from, int to)
{
    HCInstr_s *instr;
    int i;

    if (lst == NULL)
        HC_assertFail("lst", "hcCompiler.c", 5557, NULL);
    if (from < 0 || to < 0)
        HC_assertFail("from >= 0 && to >= 0", "hcCompiler.c", 5558, NULL);

    instr = &lst[from];
    if (from <= to)
    {
        for (i = from + 1; i <= to; ++i)
        {
            if (conflicts(instr, &lst[i]) != 0)
                return 1;
        }
    }
    else
    {
        for (i = from - 1; i > to; --i)
        {
            if (conflicts(instr, &lst[i]) != 0)
                return 1;
        }
    }
    return 0;
}

static void moveInstrs(HCInstr_s *lst, int start, int dst, int length)
{
    int i;
    HG_ASSERT_AT("hcCompiler.c", 5588, start != dst);
    if (length != 0)
    {
        HG_ASSERT_AT("hcCompiler.c", 5593, length > 0);
        if (start <= dst)
        {
            for (i = length - 1; i >= 0; --i)
                lst[dst + i] = lst[start + i];
        }
        else
        {
            for (i = 0; i < length; ++i)
                lst[dst + i] = lst[start + i];
        }
    }
}

static void loadScheduler(HCArena_s *mgr, HCInstr_s *lst)
{
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int uses[4];
    int b;

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int pos;

        for (pos = start; pos <= end; ++pos)
        {
            HCInstr_s *instr = &lst[pos];
            if (HC_isLoadOp(HC_getOpcodeOper(instr)) != 0 && pos != end)
            {
                int nUses = HC_getUses(&lst[pos + 1], uses);
                int def   = HC_getDef(instr);
                if (HC_containsInt(uses, nUses, def) != 0)
                {
                    int winLo = (pos - 6 < start) ? start : (pos - 6);
                    int winHi = (pos + 6 > end)   ? end   : (pos + 6);
                    int cand;

                    for (cand = winLo; cand < winHi; ++cand)
                    {
                        if (cand != pos + 1 && cand != pos
                            && conflictingMove(lst, cand, pos) == 0)
                        {
                            HCInstr_s moved = lst[cand];
                            if (cand >= pos)
                            {
                                moveInstrs(lst, pos + 1, pos + 2, cand - (pos + 1));
                                lst[pos + 1] = moved;
                            }
                            else
                            {
                                moveInstrs(lst, cand + 1, cand, pos - cand);
                                lst[pos] = moved;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    HCArena_release(mgr, &mark);
}

#if GERBERA_ARMV6_JIT
/* Schedule hard-register instructions by data dependencies and ARM1136
 * producer latency. Preserve memory ordering unless two parameter slots
 * have disjoint byte ranges, and keep backend fusion operands together. */
enum { SCHED_MAX = 120 };

static int schedIsMem(int op) { return op >= HCOP_LBS && op <= HCOP_SD4; }
static int schedIsShift(int op)
{
    return op == HCOP_ASR || op == HCOP_LSL || op == HCOP_LSR || op == HCOP_ROR;
}
static int schedIsBarrier(int op)
{
    return op < 0 || (op > HCOP_SD4 && (op < HCOP_UQADD8 || op > HCOP_UXTB_ROR)) || op == HCOP_NOP;
}
static int schedLat(int op)
{
    if (HC_isLoadOp(op) != 0 || op == HCOP_MULS || op == HCOP_MULU)
        return 3;
    return 1;
}

static int schedMemDep(HCInstr_s *a, HCInstr_s *b, int opA, int opB)
{
    int sa, sb, ra, rb;
    if (!schedIsMem(opA) || !schedIsMem(opB))
        return 0;
    sa = HC_isStoreOp(opA) != 0;
    sb = HC_isStoreOp(opB) != 0;
    if (!sa && !sb)
        return 0;
    ra = ((a->instr0 >> 21) == 0) && (a->instr0 & 0x100u);
    rb = ((b->instr0 >> 21) == 0) && (b->instr0 & 0x100u);
    if (ra && rb)
        return a->instr1 - b->instr1 < 4u || b->instr1 - a->instr1 < 4u;
    return 1;
}

/* returns -1 if b does not depend on a, else the minimum issue distance */
static int schedDep(HCInstr_s *a, HCInstr_s *b, int opA, int opB)
{
    int ua[4], ub[4];
    int da = HC_getDef(a), db = HC_getDef(b);
    int na = HC_getUses(a, ua), nb = HC_getUses(b, ub);
    int d = -1;

    if (da != -1 && HC_containsInt(ub, nb, da) != 0)
    {
        int early = 0;
        if (schedIsMem(opB))
        {
            int base = (int)(b->instr0 >> 21);
            int idx  = (b->instr0 & 0x100u) ? -1 : (int)(b->instr1 & 0x7FFu);
            early = (da == base || da == idx);
        }
        else if (schedIsShift(opB))
            early = 1;
        d = schedLat(opA) + early;
    }
    if (db != -1 && HC_containsInt(ua, na, db) != 0 && d < 0)
        d = 0;
    if (da != -1 && da == db && d < 0)
        d = 0;
    if (schedMemDep(a, b, opA, opB) != 0 && d < 0)
        d = 0;
    return d;
}

static void schedSegment(HCArena_s *mgr, HCInstr_s *lst, int lo, int n, int pinLast)
{
    typedef struct SchedRegisters
    {
        unsigned int defined, used, early;
    } SchedRegisters;
    HCMark_s mark = HCArena_mark(mgr);
    HCInstr_s *orig = (HCInstr_s *)HCArena_allocBottom(mgr, (unsigned int)(n * (int)sizeof(HCInstr_s)));
    signed char *dep = (signed char *)HCArena_allocBottom(mgr, (unsigned int)(n * n));
    signed char *ud  = (signed char *)HCArena_allocBottom(mgr, (unsigned int)(n * n));
    int *op      = (int *)HCArena_allocBottom(mgr, (unsigned int)(n * 4 * 8));
    int *unitOf  = op + n;
    int *cnt     = unitOf + n;     /* members per unit (indexed by unit id = last member) */
    int *npred   = cnt + n;
    int *readyAt = npred + n;
    int *height  = readyAt + n;
    int *done    = height + n;
    int *order   = done + n;
    unsigned char *mem = (unsigned char *)HCArena_allocBottom(mgr, (unsigned int)(n * 4));
    SchedRegisters smallRegisters[sizeof(SchedRegisters)];
    /* Register metadata is dead before the unit dependency matrix is built. */
    SchedRegisters *registers = n >= (int)sizeof(SchedRegisters)
        ? (SchedRegisters *)ud : smallRegisters;
    int maskable = 1;
    int i, j, k, t = 0, emitted = 0, outPos = 0;

    memcpy(orig, &lst[lo], (size_t)n * sizeof(HCInstr_s));
    for (i = 0; i < n; ++i)
    {
        int uses[4], count = HC_getUses(&orig[i], uses);
        int def = HC_getDef(&orig[i]);
        op[i] = HC_getOpcodeOper(&orig[i]);
        unitOf[i] = i; cnt[i] = 1; mem[i * 4] = (unsigned char)i;
        npred[i] = 0; readyAt[i] = 0; height[i] = 0; done[i] = 0;
        registers[i].defined = registers[i].used = registers[i].early = 0;
        if (def >= 0)
        {
            if (def >= HC_MAX_HARD_REGS) maskable = 0;
            else registers[i].defined = 1u << def;
        }
        for (j = 0; j < count; ++j)
        {
            if ((unsigned int)uses[j] >= HC_MAX_HARD_REGS) maskable = 0;
            else registers[i].used |= 1u << uses[j];
        }
        if (schedIsMem(op[i]))
        {
            int base = (int)(orig[i].instr0 >> 21);
            int index = (orig[i].instr0 & 0x100u) ? -1 : (int)(orig[i].instr1 & 0x7FFu);
            if ((unsigned int)base < HC_MAX_HARD_REGS) registers[i].early = 1u << base;
            if ((unsigned int)index < HC_MAX_HARD_REGS) registers[i].early |= 1u << index;
        }
        else if (schedIsShift(op[i]))
            registers[i].early = registers[i].used;
    }
    if (maskable)
    {
        for (i = 0; i < n; ++i)
        {
            unsigned int defined = registers[i].defined, used = registers[i].used;
            int latency = schedLat(op[i]);
            for (j = i + 1; j < n; ++j)
            {
                int d = -1;
                if (defined & registers[j].used)
                    d = latency + ((defined & registers[j].early) != 0);
                else if (registers[j].defined & (used | defined))
                    d = 0;
                else if (schedMemDep(&orig[i], &orig[j], op[i], op[j]))
                    d = 0;
                dep[i * n + j] = (signed char)d;
            }
        }
    }
    else
        for (i = 0; i < n; ++i)
            for (j = i + 1; j < n; ++j)
                dep[i * n + j] = (signed char)schedDep(&orig[i], &orig[j], op[i], op[j]);

    /* glue: MUL + following ADD, shift or MAXS #0 + single consumer */
    for (i = 0; i < n; ++i)
    {
        int target = -1;
        if (op[i] == HCOP_MULS || op[i] == HCOP_MULU || schedIsShift(op[i]) ||
            (op[i] == HCOP_MAXS && HC_opcodeHasImm(&orig[i]) && orig[i].instr1 == 0))
        {
            int def = HC_getDef(&orig[i]), nuse = 0, first = -1;
            for (j = i + 1; j < n; ++j)
            {
                int depends, redefines;
                if (maskable)
                {
                    depends = (registers[j].used & registers[i].defined) != 0;
                    redefines = registers[j].defined == registers[i].defined;
                }
                else
                {
                    int u[4], nu = HC_getUses(&orig[j], u);
                    depends = HC_containsInt(u, nu, def) != 0;
                    redefines = HC_getDef(&orig[j]) == def;
                }
                if (depends)
                {
                    if (first < 0) first = j;
                    ++nuse;
                }
                if (redefines)
                    break;
            }
            if (nuse == 1 &&
                (((op[i] == HCOP_MULS || op[i] == HCOP_MULU) &&
                  op[first] == HCOP_ADD && (orig[first].instr0 & 0x100u) == 0) ||
                 schedIsShift(op[i]) ||
                 ((op[i] == HCOP_MAXS) && (op[first] == HCOP_MINS || op[first] == HCOP_MINU))))
                target = first;
        }
        if (target < 0 || unitOf[i] != i || cnt[i] != 1 || cnt[unitOf[target]] >= 3)
            continue;
        for (k = i + 1; k < target; ++k)          /* i would move past k */
            if (unitOf[k] != unitOf[target] && dep[i * n + k] >= 0)
                break;
        if (k < target)
            continue;
        /* the glued unit id is the unit of target (its last member) */
        {
            int u = unitOf[target], c = cnt[u], m;
            if (c == 1 && unitOf[i] != i) continue;
            for (m = c; m > 0; --m) mem[u * 4 + m] = mem[u * 4 + m - 1];
            mem[u * 4] = (unsigned char)i;
            cnt[u] = c + 1;
            unitOf[i] = u;
            cnt[i] = 0;
        }
    }
    /* members of a unit must stay in original order */
    for (i = 0; i < n; ++i)
        if (cnt[i] > 1)
            for (j = 1; j < cnt[i]; ++j)
                for (k = j; k > 0 && mem[i * 4 + k - 1] > mem[i * 4 + k]; --k)
                {
                    unsigned char tmp = mem[i * 4 + k];
                    mem[i * 4 + k] = mem[i * 4 + k - 1];
                    mem[i * 4 + k - 1] = tmp;
                }

    memset(ud, -1, (size_t)(n * n));
    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j)
            if (dep[i * n + j] >= 0 && unitOf[i] != unitOf[j])
            {
                int u = unitOf[i], v = unitOf[j];
                if (ud[u * n + v] < dep[i * n + j])
                    ud[u * n + v] = dep[i * n + j];
            }
    if (pinLast)
    {
        int last = unitOf[n - 1];
        for (i = 0; i < n; ++i)
            if (cnt[i] > 0 && i != last && ud[i * n + last] < 0)
                ud[i * n + last] = 0;
    }
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j)
            if (cnt[i] > 0 && cnt[j] > 0 && ud[i * n + j] >= 0)
                ++npred[j];
    for (i = n - 1; i >= 0; --i)
    {
        int best = 0;
        if (cnt[i] == 0) continue;
        for (j = i + 1; j < n; ++j)
            if (cnt[j] > 0 && ud[i * n + j] >= 0 && ud[i * n + j] + height[j] > best)
                best = ud[i * n + j] + height[j];
        height[i] = best > schedLat(op[i]) + cnt[i] - 1 ? best : schedLat(op[i]) + cnt[i] - 1;
    }

    {
        int units = 0;
        for (i = 0; i < n; ++i) if (cnt[i] > 0) ++units;
        while (emitted < units)
        {
            int pick = -1, pickReady = 0x7FFFFFFF;
            for (i = 0; i < n; ++i)
            {
                if (cnt[i] == 0 || done[i] || npred[i] != 0) continue;
                {
                    int r = readyAt[i];
                    int better;
                    if (pick < 0) better = 1;
                    else if (r <= t && pickReady <= t) better = height[i] > height[pick];
                    else if (r <= t) better = 1;
                    else if (pickReady <= t) better = 0;
                    else better = r < pickReady || (r == pickReady && height[i] > height[pick]);
                    if (better) { pick = i; pickReady = r; }
                }
            }
            if (pick < 0)
            {
                outPos = -1;
                break;
            }
            if (t < readyAt[pick]) t = readyAt[pick];
            for (k = 0; k < cnt[pick]; ++k)
                order[outPos++] = mem[pick * 4 + k];
            done[pick] = 1;
            ++emitted;
            for (j = 0; j < n; ++j)
                if (cnt[j] > 0 && ud[pick * n + j] >= 0)
                {
                    int r = t + cnt[pick] - 1 + ud[pick * n + j];
                    if (readyAt[j] < r) readyAt[j] = r;
                    --npred[j];
                }
            t += cnt[pick];
        }
    }
    if (outPos == n)
        for (i = 0; i < n; ++i)
            lst[lo + i] = orig[order[i]];
    HCArena_release(mgr, &mark);
}

static void renameScratchLoads(HCArena_s *mgr, HCInstr_s *lst, int nHardRegs)
{
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int nInstrs = scanNumRegs(lst).numInstrs;
    unsigned int *useMask, *defMask, *liveIn, *liveOut, *liveAfter;
    unsigned int usedRegs = 0;
    int b, pos, changed, cursor = 1;
    int uses[4];

    if (nHardRegs <= 2 || nHardRegs > 30 || nInstrs <= 2)
    {
        HCArena_release(mgr, &mark);
        return;
    }

    for (pos = 0; pos < nInstrs; ++pos)
    {
        int def = HC_getDef(&lst[pos]);
        int nUses = HC_getRegUses(uses, &lst[pos]);
        int u;
        if (def >= 1 && def < nHardRegs)
            usedRegs |= 1u << def;
        for (u = 0; u < nUses; ++u)
            if (uses[u] >= 1 && uses[u] < nHardRegs)
                usedRegs |= 1u << uses[u];
    }

    useMask   = (unsigned int *)HCArena_allocBottom(mgr, (unsigned int)(4 * nBlocks * sizeof(unsigned int)));
    defMask   = useMask + nBlocks;
    liveIn    = defMask + nBlocks;
    liveOut   = liveIn + nBlocks;
    liveAfter = (unsigned int *)HCArena_allocBottom(mgr, (unsigned int)(nInstrs * sizeof(unsigned int)));
    memset(useMask, 0, (size_t)(4 * nBlocks) * sizeof(unsigned int));

    for (b = 0; b < nBlocks; ++b)
    {
        int start = BasicBlock_getStart(&blocks[b]);
        int end   = BasicBlock_getEnd(&blocks[b]);
        unsigned int live = 0, defs = 0;
        for (pos = end; pos >= start; --pos)
        {
            int def = HC_getDef(&lst[pos]);
            int nUses = HC_getRegUses(uses, &lst[pos]);
            int u;
            if (def >= 1 && def < nHardRegs)
            {
                live &= ~(1u << def);
                defs |= 1u << def;
            }
            for (u = 0; u < nUses; ++u)
                if (uses[u] >= 1 && uses[u] < nHardRegs)
                    live |= 1u << uses[u];
        }
        useMask[b] = live;
        defMask[b] = defs;
    }

    do
    {
        changed = 0;
        for (b = nBlocks - 1; b >= 0; --b)
        {
            BasicBlock_s *bb = &blocks[b];
            unsigned int out = 0, in;
            int s;
            for (s = 0; s < (int)bb->numSuccs; ++s)
                out |= liveIn[bb->succ[s]];
            in = useMask[b] | (out & ~defMask[b]);
            if (out != liveOut[b] || in != liveIn[b])
            {
                liveOut[b] = out;
                liveIn[b] = in;
                changed = 1;
            }
        }
    }
    while (changed);

    for (b = 0; b < nBlocks; ++b)
    {
        int start = BasicBlock_getStart(&blocks[b]);
        int end   = BasicBlock_getEnd(&blocks[b]);
        unsigned int live = liveOut[b];
        for (pos = end; pos >= start; --pos)
        {
            int def = HC_getDef(&lst[pos]);
            int nUses = HC_getRegUses(uses, &lst[pos]);
            int u;
            liveAfter[pos] = live;
            if (def >= 1 && def < nHardRegs)
                live &= ~(1u << def);
            for (u = 0; u < nUses; ++u)
                if (uses[u] >= 1 && uses[u] < nHardRegs)
                    live |= 1u << uses[u];
        }

        /* Keep inputs of backend-deferred instructions (MULS/MULU -> MLA,
         * shift -> barrel-shifter, MAXS #0 -> USAT) live until their single
         * consumer so scratch renaming never blocks backend fusion. */
        for (pos = start; pos < end; ++pos)
        {
            int mop = HC_getOpcodeOper(&lst[pos]);
            int mdef = HC_getDef(&lst[pos]);
            if (mdef >= 1 && mdef < nHardRegs &&
                (mop == HCOP_MULS || mop == HCOP_MULU || schedIsShift(mop) ||
                 (mop == HCOP_MAXS && HC_opcodeHasImm(&lst[pos]) && lst[pos].instr1 == 0)))
            {
                int mUses[4], nmUses = HC_getRegUses(mUses, &lst[pos]);
                int consumer = -1, nCons = 0, k, u;
                for (k = pos + 1; k <= end; ++k)
                {
                    int cUses[4], ncUses = HC_getRegUses(cUses, &lst[k]);
                    for (u = 0; u < ncUses; ++u)
                        if (cUses[u] == mdef)
                        {
                            consumer = k;
                            ++nCons;
                        }
                    if (HC_getDef(&lst[k]) == mdef)
                        break;
                }
                if (nCons == 1 && consumer > pos)
                {
                    unsigned int pin = 0;
                    for (u = 0; u < nmUses; ++u)
                        if (mUses[u] >= 1 && mUses[u] < nHardRegs)
                            pin |= 1u << mUses[u];
                    for (k = pos; k < consumer; ++k)
                        liveAfter[k] |= pin;
                }
            }
        }

        for (pos = start; pos < end; ++pos)
        {
            int op = HC_getOpcodeOper(&lst[pos]);
            int r = HC_getDef(&lst[pos]);
            int usePos = -1, holdEnd, k, step, cand = -1;
            unsigned int busy = 0;

            if (!HC_isLoadOp(op) || r < 1 || r >= nHardRegs || schedIsBarrier(op))
                continue;
            /* Skip if base register of the load is r itself and kept live. */
            for (k = pos + 1; k <= end && k - pos < SCHED_MAX; ++k)
            {
                int kop = HC_getOpcodeOper(&lst[k]);
                int kdef = HC_getDef(&lst[k]);
                int nUses = HC_getRegUses(uses, &lst[k]);
                int u, usedHere = 0;

                if (schedIsBarrier(kop))
                    break;
                for (u = 0; u < nUses; ++u)
                    if (uses[u] == r)
                        usedHere = 1;
                if (usedHere)
                {
                    if (usePos >= 0) { usePos = -1; break; }
                    usePos = k;
                }
                if (kdef == r && k != usePos)
                    break;
                if (usePos == k && ((liveAfter[k] >> r) & 1u) == 0)
                    break;
            }
            if (usePos < 0 || ((liveAfter[usePos] >> r) & 1u) != 0)
                continue;

            /* Preserve backend instruction fusion windows (MUL+ADD -> MLA,
             * shift+ALU barrel-shifter, MAXS+MIN -> USAT) so nothing can be
             * scheduled in between that overwrites the renamed source reg. */
            holdEnd = usePos;
            {
                int uop = HC_getOpcodeOper(&lst[usePos]);
                int udef = HC_getDef(&lst[usePos]);
                if (udef >= 1 && udef < nHardRegs &&
                    (uop == HCOP_MULS || uop == HCOP_MULU || schedIsShift(uop) ||
                     (uop == HCOP_MAXS && HC_opcodeHasImm(&lst[usePos]) && lst[usePos].instr1 == 0)))
                {
                    for (k = usePos + 1; k <= end && k - pos < SCHED_MAX; ++k)
                    {
                        int kop = HC_getOpcodeOper(&lst[k]);
                        int nUses = HC_getRegUses(uses, &lst[k]);
                        int u;
                        if (schedIsBarrier(kop))
                            break;
                        for (u = 0; u < nUses; ++u)
                            if (uses[u] == udef)
                                holdEnd = k;
                        if (HC_getDef(&lst[k]) == udef)
                            break;
                    }
                }
            }

            for (k = pos; k < holdEnd; ++k)
                busy |= liveAfter[k] & ~(1u << r);
            for (k = pos + 1; k <= holdEnd; ++k)
            {
                int kdef = HC_getDef(&lst[k]);
                int nUses = HC_getRegUses(uses, &lst[k]);
                int u;
                if (k < holdEnd && kdef >= 1 && kdef < nHardRegs && kdef != r)
                    busy |= 1u << kdef;
                for (u = 0; u < nUses; ++u)
                    if (uses[u] >= 1 && uses[u] < nHardRegs && uses[u] != r)
                        busy |= 1u << uses[u];
            }

            for (step = 0; step < nHardRegs - 1; ++step)
            {
                int c = 1 + ((cursor - 1 + step) % (nHardRegs - 1));
                if (((usedRegs >> c) & 1u) != 0 && ((busy >> c) & 1u) == 0)
                {
                    cand = c;
                    break;
                }
            }
            if (cand < 1)
                continue;
            cursor = 1 + (cand % (nHardRegs - 1));
            if (cand == r)
                continue;

            HC_setOpcodeDst(&lst[pos], cand);
            {
                int nUses = HC_getRegUses(uses, &lst[usePos]);
                int u;
                for (u = 0; u < nUses; ++u)
                    if (uses[u] == r)
                        HC_setUse(&lst[usePos], u, cand);
            }
            for (k = pos; k < usePos; ++k)
                liveAfter[k] = (liveAfter[k] & ~(1u << r)) | (1u << cand);
            for (k = usePos; k < holdEnd; ++k)
                liveAfter[k] |= 1u << cand;
        }
    }

    HCArena_release(mgr, &mark);
}

static void armListScheduler(HCArena_s *mgr, HCInstr_s *lst)
{
    int pos = 0;
    for (;;)
    {
        int op = HC_getOpcodeOper(&lst[pos]);
        int lo, n;
        if (op == HCOP_EOP)
            return;
        if (schedIsBarrier(op))
        {
            ++pos;
            continue;
        }
        lo = pos;
        n = 0;
        while (n < SCHED_MAX && !schedIsBarrier(HC_getOpcodeOper(&lst[lo + n])))
            ++n;
        if (n > 1)
        {
            int nextOp = HC_getOpcodeOper(&lst[lo + n]);
            schedSegment(mgr, lst, lo, n, HC_isCondBranchOp(nextOp) != 0 || nextOp == HCOP_BRA);
        }
        pos = lo + n;
    }
}

#endif

static void sort32(unsigned int *a, int N)
{
    int i;
    if (a == NULL || N < 0)
        HC_assertFail("a && N >= 0", "hcCompiler.c", 5688, NULL);
    for (i = 1; i < N; ++i)
    {
        if (a[i - 1] < a[i])
        {
            unsigned int v = a[i];
            int j = i;
            do
            {
                if (a[j - 1] >= v)
                    break;
                a[j] = a[j - 1];
                --j;
            }
            while (j != 0);
            a[j] = v;
        }
    }
}

static void sortSpillSlots(HCArena_s *mgr, HCInstr_s *lst, int spillStart)
{
    HCMark_s mark = HCArena_mark(mgr);
    BasicBlock_s *blocks = NULL;
    int nBlocks = HC_buildBasicBlocks(mgr, &blocks, lst);
    int nSlots  = scanMaxSpillLoc(lst) >> 2;
    unsigned int *freq;
    int b;
    int i;
    int prev;

    computeDominators(mgr, blocks, nBlocks);
    computeLoopNesting(mgr, blocks, nBlocks);

    freq = (unsigned int *)HCArena_allocBottom(mgr, 4u * (unsigned int)nSlots);
    memset(freq, 0, 4u * (unsigned int)nSlots);

    for (b = 0; b < nBlocks; ++b)
    {
        BasicBlock_s *bb = &blocks[b];
        int start = BasicBlock_getStart(bb);
        int end   = BasicBlock_getEnd(bb);
        int depth = 1 << (4 * bb->nesting);
        int pos;

        for (pos = start; pos <= end; ++pos)
        {
            HCInstr_s *instr = &lst[pos];
            if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
            {
                int loc = HC_getOpcodeImm(instr);
                if (loc >= spillStart)
                {
                    int ndx = (loc - spillStart) >> 2;
                    int n   = depth + (int)(freq[ndx] >> 16);
                    int capped;
                    if (ndx >= 65536 || n >= 65536 + depth)
                        HC_assertFail("ndx < 65536 && n < 65536+depth",
                                      "hcCompiler.c", 5760, NULL);
                    capped = HC_max(n, 0xFFFF);
                    freq[ndx] = (unsigned int)ndx | ((unsigned int)capped << 16);
                }
            }
        }
    }

    sort32(freq, nSlots);

    i = 0;
    do
    {
        HCInstr_s *instr = &lst[i];
        if (HC_isSpillLoad(instr) != 0 || HC_isSpillStore(instr) != 0)
        {
            int loc = HC_getOpcodeImm(instr);
            if (loc >= spillStart)
            {
                int ndx    = (loc - spillStart) >> 2;
                int newNdx = (int)(freq[ndx] & 0xFFFFu);
                HC_setOpcodeImm(instr, spillStart + 4 * newNdx);
            }
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    HCArena_release(mgr, &mark);
}

static void copyInstrs(HCInstr_s *dst, HCInstr_s *src)
{
    int i = 0;
    int prev;

    if (dst == NULL)
        HC_assertFail("dst", "hcCompiler.c", 5793, NULL);
    if (src == NULL)
        HC_assertFail("src", "hcCompiler.c", 5794, NULL);
    do
    {
        dst[i] = src[i];
        prev = i++;
    }
    while (HC_getOpcodeOper(&src[prev]) != HCOP_EOP);
}

/* --- hcPanicRegalloc.inl -------------------------------------------------- */

static unsigned int computePinMask(const int *uses, int nUses, int nHardRegs)
{
    unsigned int mask = 1u;
    int u;

    if (nHardRegs >= 32)
        HC_assertFail("nHardRegs < 32", "hcPanicRegalloc.inl", 34, NULL);
    for (u = 0; u < nUses; ++u)
    {
        if (uses[u] < nHardRegs)
            mask |= (1u << uses[u]);
    }
    return mask;
}

static HCInstr_s *panicRegisterAllocation(HCArena_s *mgr, HCBackend_s *backend,
                                          int spillStart, HCInstr_s *lst)
{
    int i = 0;
    int prev;
    int nHardRegs = backend->numHardRegs();
    HCInstr_s *out = (HCInstr_s *)HCArena_current(mgr);
    HCInstr_s *dst = out;
    int uses[4];
    int regUses[4];
    int mappedUses[4];

    do
    {
        int dstHard = -1;
        HCInstr_s *srcInstr = &lst[i];
        int def = HC_getDef(srcInstr);
        HCInstr_s rewritten = *srcInstr;
        int nUses   = HC_getUses(&rewritten, uses);
        unsigned int pinMask = computePinMask(uses, nUses, nHardRegs);
        int nRegUses;
        int u;

        if (def != -1 && def < nHardRegs)
            pinMask |= (1u << def);

        for (u = 0; u < nUses; ++u)
        {
            int r = uses[u];
            if (r < nHardRegs)
            {
                mappedUses[u] = r;
            }
            else
            {
                int hard = HC_bsf(~pinMask);
                pinMask |= (1u << hard);
                HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
                *dst++ = genSpillLoad(hard, spillStart + 4 * r);
                mappedUses[u] = hard;
            }
        }

        nRegUses = HC_getRegUses(regUses, &rewritten);
        for (u = 0; u < nRegUses; ++u)
        {
            int hardReg = -1;
            int k;
            for (k = 0; k < nUses; ++k)
            {
                if (regUses[u] == uses[k])
                {
                    hardReg = mappedUses[k];
                    break;
                }
            }
            if (hardReg == -1)
                HC_assertFail("hardReg != HC_NIL", "hcPanicRegalloc.inl", 110, NULL);
            if (def == regUses[u])
                dstHard = hardReg;
            HC_setUse(&rewritten, u, hardReg);
        }

        if (def != -1)
        {
            if (dstHard == -1)
                dstHard = HC_bsf(~pinMask);
            if (def < nHardRegs)
                dstHard = def;
            HC_setOpcodeDst(&rewritten, dstHard);
        }

        HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
        *dst++ = rewritten;

        if (def != -1)
        {
            HCArena_growCurrent(mgr, (unsigned int)sizeof(HCInstr_s));
            *dst++ = genSpillStore(dstHard, spillStart + 4 * def);
        }
        prev = i++;
    }
    while (HC_getOpcodeOper(&lst[prev]) != HCOP_EOP);

    return out;
}

/* --- compilation driver & public entrypoints ------------------------------ */

#if GERBERA_ARMV6_JIT
/* Parameter values remain reusable until a definition, overlapping store,
 * or control-flow boundary invalidates them. Other pointers may alias r0. */
static void cseInvariantR0Loads(HCInstr_s *lst)
{
    enum { CAPACITY = 32 };
    unsigned int offsets[CAPACITY];
    int registers[CAPACITY], count = 0, i;
    for (i = 0; HC_getOpcodeOper(&lst[i]) != HCOP_EOP; ++i)
    {
        HCInstr_s *instr = &lst[i];
        int op = HC_getOpcodeOper(instr), def = HC_getDef(instr), j;
        if (op == HCOP_LABEL || HC_isBranchOp(op) || def == 0)
        {
            count = 0;
            continue;
        }
        if (HC_isStoreOp(op))
        {
            if (!HC_isSpillStore(instr))
                count = 0;
            else
                for (j = 0; j < count; )
                {
                    unsigned int offset = instr->instr1;
                    if (offset - offsets[j] < 4u || offsets[j] - offset < 4u)
                    {
                        offsets[j] = offsets[--count];
                        registers[j] = registers[count];
                    }
                    else ++j;
                }
            continue;
        }
        for (j = 0; j < count; )
        {
            if (registers[j] == def)
            {
                offsets[j] = offsets[--count];
                registers[j] = registers[count];
            }
            else ++j;
        }
        if (HC_isSpillLoad(instr))
        {
            for (j = 0; j < count; ++j)
                if (offsets[j] == instr->instr1) break;
            if (j < count)
                *instr = HC_genOp2(HCOP_MOV, def, registers[j]);
            else if (count < CAPACITY)
            {
                offsets[count] = instr->instr1;
                registers[count++] = def;
            }
        }
    }
}

/* Reuse pure expressions within a straight-line region. Limit the search
 * window so compile time stays bounded for long generated programs. */
static void reuseALUValues(HCInstr_s *lst)
{
    int first = 0, j;
    for (j = 0; HC_getOpcodeOper(&lst[j]) != HCOP_EOP; ++j)
    {
        int op = HC_getOpcodeOper(&lst[j]), i;
        int dst = HC_getDef(&lst[j]);
        unsigned int key = lst[j].instr0 & ~(0x7FFu << 10);
        if (op == HCOP_LABEL || HC_isBranchOp(op)) first = j + 1;
        if (op > HCOP_XOR || op == HCOP_NOP || op == HCOP_MOV || op == HCOP_MOVN || dst < 0)
            continue;
        for (i = j - 1; i >= first && i >= j - 64; --i)
        {
            int uses[4], count, previous, k;
            if ((lst[i].instr0 & ~(0x7FFu << 10)) != key || lst[i].instr1 != lst[j].instr1)
                continue;
            previous = HC_getDef(&lst[i]);
            count = HC_getUses(&lst[i], uses);
            if (previous < 0 || previous == dst || HC_containsInt(uses, count, previous)) continue;
            for (k = i + 1; k < j; ++k)
            {
                int def = HC_getDef(&lst[k]);
                if (def >= 0 && (def == previous || HC_containsInt(uses, count, def))) break;
            }
            if (k == j)
            {
                lst[j] = HC_genOp2(HCOP_MOV, dst, previous);
                break;
            }
        }
    }
}

/* The pixel-pipe bytecode uses labels 2/3/4 for the row head, the empty-row
 * path, and the pixel loop. A gradient ordinarily starts at ref + dx*left
 * and advances by dx even when depth rejects a pixel. For a single-use
 * gradient, anchor it at the right edge instead and recover its value at
 * that use: (ref + dx*(left+count)) + dx*(i-count). All arithmetic is HC
 * low-word arithmetic, so this also preserves negative steps and wrap.
 * This trades a loop-carried, often spilled value for a multiply-add only
 * on the path needing it. The pixel-pipe selector limits
 * this transform to smooth nearest MODULATE with 16-bit color and depth. */
typedef struct SpanGradient_s
{
    int init, use, update;
    int adder, value;
} SpanGradient_s;

static int spanRegOp(HCInstr_s *ins, int op, int s0, int s1)
{
    return HC_getOpcodeOper(ins) == op && !HC_opcodeHasImm(ins) &&
           HC_getOpcodeSrc0Raw(ins) == s0 && HC_getOpcodeSrc1Raw(ins) == s1;
}

static int spanImmOp(HCInstr_s *ins, int op, int s0, unsigned int imm)
{
    return HC_getOpcodeOper(ins) == op && HC_opcodeHasImm(ins) &&
           HC_getOpcodeSrc0Raw(ins) == s0 && ins->instr1 == imm;
}

/* Skip a contiguous run of depth failures without advancing texture and
 * dither state per pixel. The passing path retains its ordinary shader loop. */
static int skipDepthRejects(HCArena_s *mgr, HCInstr_s *lst, RegInstrCount_s shape,
                            int pixel, int back, int join, int index,
                            SpanGradient_s *plan, int nPlan, int nHardRegs)
{
    int color = pixel + 4, z, storedZ, cmp, i, g, label = 0, dither = -1, depthStore;
    int reject, advance, shade, done, savedIndex, zStep, delta, temp, next = shape.numRegs;
    int n = 0, topMark;
    HCInstr_s *out;

    if (nPlan != 3 || join < color || plan[0].use != pixel + 1 ||
        !spanImmOp(&lst[pixel + 1], HCOP_LSR, plan[0].value, 14)) return 0;
    z = HC_getDef(&lst[pixel + 1]);
    storedZ = HC_getDef(&lst[pixel + 2]);
    cmp = HC_getOpcodeOper(&lst[pixel + 3]);
    if (HC_getOpcodeOper(&lst[pixel + 2]) != HCOP_LW2U ||
        HC_getOpcodeSrc1Raw(&lst[pixel + 2]) != index ||
        (cmp != HCOP_CBGTU && cmp != HCOP_CBGEU) ||
        !spanRegOp(&lst[pixel + 3], cmp, z, storedZ) ||
        (unsigned int)HC_getOpcodeDst(&lst[pixel + 3]) != lst[join].instr1) return 0;
    depthStore = spanRegOp(&lst[color], HCOP_SW2, HC_getOpcodeSrc0Raw(&lst[pixel + 2]), index) &&
                 HC_getOpcodeDst(&lst[color]) == z;
    for (i = join + 1; i < back - 1; ++i)
    {
        HCInstr_s *ins = &lst[i];
        if (HC_getOpcodeOper(ins) == HCOP_ROR && HC_opcodeHasImm(ins) && ins->instr1 == 8u &&
            HC_getOpcodeDst(ins) == HC_getOpcodeSrc0Raw(ins))
        {
            if (dither >= 0) return 0;
            dither = HC_getOpcodeDst(ins);
        }
    }
    if (back - join != nPlan + 2 + (dither >= 0)) return 0;
    for (i = 0; i < shape.numInstrs; ++i)
        if (HC_getOpcodeOper(&lst[i]) == HCOP_LABEL)
            label = HC_max(label, (int)lst[i].instr1);
    if (label + 4 >= (int)HC_MAX_LABEL_INDEX || next + 4 + nHardRegs >= HC_MAX_REG_INDEX) return 0;
    reject = ++label; advance = ++label; shade = ++label; done = ++label;
    savedIndex = next++; zStep = next++; delta = next++; temp = next++;
    topMark = HCArena_markTop(mgr);
    out = (HCInstr_s *)HCArena_allocTop(mgr, sizeof(*out) * (shape.numInstrs + 32));
    for (i = 0; i < shape.numInstrs - 1; ++i)
    {
        HCInstr_s ins = lst[i];
        if (i == color + depthStore) out[n++] = HC_genOp1Imm(HCOP_LABEL, shade);
        if (i == pixel + 3) HC_setOpcodeDst(&ins, reject);
        out[n++] = ins;
    }
    out[n++] = HC_genOp1(HCOP_BRA, done);
    out[n++] = HC_genOp1Imm(HCOP_LABEL, reject);
    out[n++] = HC_genOp2(HCOP_MOV, savedIndex, index);
    out[n++] = genSpillLoad(zStep, 232);
    out[n++] = HC_genOp1Imm(HCOP_LABEL, advance);
    out[n++] = HC_genOp3Imm(HCOP_ADD, index, index, 1);
    out[n++] = HC_genOp3Imm(HCOP_CBEQ, 3, index, 0);
    out[n++] = HC_genOp3(HCOP_ADD, plan[0].value, plan[0].value, zStep);
    out[n++] = lst[pixel + 1];
    out[n++] = lst[pixel + 2];
    out[n] = lst[pixel + 3];
    HC_setOpcodeDst(&out[n++], advance);
    if (depthStore) out[n++] = lst[color];
    out[n++] = HC_genOp3(HCOP_SUB, delta, index, savedIndex);
    for (g = 1; g < nPlan; ++g)
    {
        out[n++] = HC_genOp3(HCOP_MULS, temp, plan[g].adder, delta);
        out[n++] = HC_genOp3(HCOP_ADD, plan[g].value, plan[g].value, temp);
    }
    if (dither >= 0)
    {
        out[n++] = HC_genOp3Imm(HCOP_LSL, temp, delta, 3);
        out[n++] = HC_genOp3(HCOP_ROR, dither, dither, temp);
    }
    out[n++] = HC_genOp1(HCOP_BRA, shade);
    out[n++] = HC_genOp1Imm(HCOP_LABEL, done);
    out[n++] = lst[shape.numInstrs - 1];
    HCArena_growCurrent(mgr, sizeof(*out) * (n - shape.numInstrs));
    memcpy(lst, out, sizeof(*out) * n);
    HCArena_releaseTop(mgr, topMark);
    return 1;
}

static void rematerializeSpanGradients(HCArena_s *mgr, HCInstr_s *lst, int nHardRegs, int split)
{
    enum { ROW = 2, EXIT = 3, PIXEL = 4, FIRST_X_ADDER = 232, MAX_GRADIENTS = 8 };
    RegInstrCount_s shape = scanNumRegs(lst);
    SpanGradient_s plan[MAX_GRADIENTS];
    int row = -1, pixel = -1, back = -1, rightOp = -1, latestTarget = -1;
    int left, count, right, index, span, i, j, g, nPlan = 0;
    int next = shape.numRegs, n = 0, topMark;
    HCInstr_s *out;

    if (next + 2 * MAX_GRADIENTS + 8 + nHardRegs >= HC_MAX_REG_INDEX)
        return;
    for (i = 0; i < shape.numInstrs; ++i)
    {
        HCInstr_s *ins = &lst[i];
        if (HC_getOpcodeOper(ins) == HCOP_LABEL)
        {
            if (ins->instr1 == ROW) row = i;
            if (ins->instr1 == PIXEL) pixel = i;
        }
        if (HC_isBranchOp(HC_getOpcodeOper(ins)) && HC_getOpcodeDst(ins) == PIXEL)
        {
            if (back >= 0) return;
            back = i;
        }
    }
    if (row < 0 || pixel <= row + 7 || back <= pixel + 2 || back + 1 >= shape.numInstrs ||
        HC_getOpcodeOper(&lst[row + 1]) != HCOP_LD ||
        HC_getOpcodeOper(&lst[back + 1]) != HCOP_LABEL || lst[back + 1].instr1 != EXIT)
        return;

    span = HC_getOpcodeDst(&lst[row + 1]);
    if (!spanImmOp(&lst[row + 3], HCOP_LSR, span, 16) ||
        !spanImmOp(&lst[row + 4], HCOP_AND, span, 65535))
        return;
    count = HC_getOpcodeDst(&lst[row + 3]);
    left = HC_getOpcodeDst(&lst[row + 4]);
    if (!spanImmOp(&lst[pixel - 2], HCOP_SUBR, count, 0))
        return;
    index = HC_getOpcodeDst(&lst[pixel - 2]);
    if (!spanImmOp(&lst[pixel - 1], HCOP_CBGES, index, 0) || HC_getOpcodeDst(&lst[pixel - 1]) != EXIT ||
        !spanImmOp(&lst[back - 1], HCOP_ADD, index, 1) || HC_getDef(&lst[back - 1]) != index ||
        !spanImmOp(&lst[back], HCOP_CBNEQ, index, 0))
        return;

    for (i = row + 1; i < back; ++i)
    {
        HCInstr_s *ins = &lst[i];
        int op = HC_getOpcodeOper(ins), def = HC_getDef(ins);
        if ((def == index && i != pixel - 2 && i != back - 1) ||
            (def == span && i > row + 1 && i <= row + 4) ||
            (def == count && i != row + 3 && i < pixel) ||
            (def == left && i != row + 4 && i < pixel))
            return;
        if (i < pixel - 2 && spanRegOp(ins, HCOP_ADD, left, count))
            rightOp = i;
        if (i < pixel - 1 && (op == HCOP_LABEL || HC_isBranchOp(op)))
            return;  /* no entry or exit between the row head and its setup */
        if (i > pixel && HC_isBranchOp(op))
        {
            /* Only forward skips in the body, and none may bypass the
             * induction-variable updates (checked below for each gradient). */
            unsigned int target = (unsigned int)HC_getOpcodeDst(ins);
            for (j = i + 1; j < back - 1; ++j)
                if (HC_getOpcodeOper(&lst[j]) == HCOP_LABEL && lst[j].instr1 == target)
                    break;
            if (j == back - 1) return;
            latestTarget = HC_max(latestTarget, j);
        }
    }
    if (rightOp <= row + 4) return;
    right = HC_getDef(&lst[rightOp]);
    for (i = 0; i < rightOp; ++i)
    {
        int uses[4], nUses = HC_getUses(&lst[i], uses);
        if (HC_getDef(&lst[i]) == right || HC_containsInt(uses, nUses, right)) return;
    }
    /* The generated program has no outside entry into the pixel body. */
    for (i = pixel + 1; i < back; ++i)
        if (HC_getOpcodeOper(&lst[i]) == HCOP_LABEL)
            for (j = 0; j < shape.numInstrs; ++j)
                if ((j < pixel || j > back) && HC_isBranchOp(HC_getOpcodeOper(&lst[j])) &&
                    (unsigned int)HC_getOpcodeDst(&lst[j]) == lst[i].instr1)
                    return;

    for (g = split ? 0 : 2; g < (split ? MAX_GRADIENTS : 5); ++g)
    {
        int load, mul, value, adder, product, use = -1, update = -1;
        for (load = 0; load < row; ++load)
            if (HC_isSpillLoad(&lst[load]) && lst[load].instr1 == FIRST_X_ADDER + 4u * g)
                break;
        if (load == row) continue;
        adder = HC_getDef(&lst[load]);
        for (mul = row + 5; mul < (split ? pixel - 2 : rightOp); ++mul)
            if (spanRegOp(&lst[mul], HCOP_MULS, adder, left)) break;
        if (mul == (split ? pixel - 2 : rightOp)) continue;
        product = HC_getDef(&lst[mul]);
        value = HC_getDef(&lst[mul + 1]);
        if (!spanRegOp(&lst[mul + 1], HCOP_ADD, HC_getOpcodeSrc0Raw(&lst[mul + 1]), product) ||
            HC_getOpcodeSrc0Raw(&lst[mul + 1]) == product || HC_getOpcodeSrc0Raw(&lst[mul + 1]) == value ||
            value <= 0 || adder <= 0 || product <= 0)
            continue;

        for (i = 0; i < shape.numInstrs; ++i)
        {
            int uses[4], nUses = HC_getUses(&lst[i], uses), def = HC_getDef(&lst[i]);
            if ((def == adder && i != load) || (def == product && i != mul) ||
                (HC_containsInt(uses, nUses, product) && i != mul + 1)) break;
            if (i == mul + 1) continue;
            if (def == value)
            {
                if (update >= 0 || i <= pixel || i <= latestTarget || i >= back - 1 ||
                    !spanRegOp(&lst[i], HCOP_ADD, value, adder)) break;
                update = i;
            }
            else if (HC_containsInt(uses, nUses, value))
            {
                if (use >= 0 || i <= pixel || i >= back) break;
                use = i;
            }
        }
        if (i == shape.numInstrs && use >= 0 && update > use)
        {
            SpanGradient_s entry = {mul, use, update, adder, value};
            plan[nPlan++] = entry;
        }
    }
    if (split)
    {
        skipDepthRejects(mgr, lst, shape, pixel, back, latestTarget, index, plan, nPlan, nHardRegs);
        return;
    }
    if (nPlan == 0) return;

    topMark = HCArena_markTop(mgr);
    out = (HCInstr_s *)HCArena_allocTop(mgr, sizeof(*out) * (shape.numInstrs + nPlan));
    for (i = 0; i < shape.numInstrs; ++i)
    {
        HCInstr_s ins = lst[i];
        int skip = 0;
        if (i == row + 5) out[n++] = lst[rightOp];
        if (i == rightOp) continue;
        for (g = 0; g < nPlan; ++g)
        {
            const SpanGradient_s *p = &plan[g];
            if (i == p->init) HC_setOpcodeSrc1(&ins, right);
            if (i == p->update) skip = 1;
            if (i == p->use)
            {
                int uses[4], u, nUses = HC_getRegUses(uses, &ins), tmp = next++;
                out[n++] = HC_genOp3(HCOP_MULS, tmp, p->adder, index);
                out[n++] = HC_genOp3(HCOP_ADD, tmp, p->value, tmp);
                for (u = 0; u < nUses; ++u)
                    if (uses[u] == p->value) HC_setUse(&ins, u, tmp);
            }
        }
        if (!skip) out[n++] = ins;
    }
    HCArena_growCurrent(mgr, sizeof(*out) * nPlan);
    memcpy(lst, out, sizeof(*out) * n);
    HCArena_releaseTop(mgr, topMark);
}

#endif

int HC_setConstantParameter(HCInstr_s *instructions, int byteOffset, int value)
{
    HCInstr_s *ins;
    int replaced = 0;
    for (ins = instructions; HC_getOpcodeOper(ins) != HCOP_EOP; ++ins)
        if (HC_isSpillStore(ins) && HC_getOpcodeImm(ins) == byteOffset)
            return 0;
    for (ins = instructions; HC_getOpcodeOper(ins) != HCOP_EOP; ++ins)
        if (HC_isSpillLoad(ins) && HC_getOpcodeImm(ins) == byteOffset)
        {
            *ins = HC_genOp2Imm(HCOP_MOV, HC_getOpcodeDst(ins), value);
            ++replaced;
        }
    return replaced;
}

static int compileInternal(HCArena_s *mgr, void **output, HCInstr_s *emuDst,
                           HCInstr_s *instructions, HCBackend_s *backend,
                           unsigned int optimizationMask,
                           unsigned char *inputParamsUsed,
                           unsigned char *inputParamMutable,
                           int numStackParameters)
{
    int nHardRegs  = (backend != NULL) ? backend->numHardRegs() : 1;
    int spillStart = 4 * (numStackParameters + 1);
    HCInstr_s *lst = NULL;
    int botMark;
    int prevInstrs;
    int curInstrs;
    int backendOptions = backend != NULL && backend->optimizationMask != NULL
        ? backend->optimizationMask() : 0;
    int scheduleARM11 = (backendOptions & HC_BACKEND_ARM11_SCHEDULE) &&
                       (optimizationMask & HC_OPT_SCHEDULE);

    if (backend != NULL && backend->internalStackSpaceAfterInvars != NULL)
        spillStart += backend->internalStackSpaceAfterInvars();

    validateProgram(mgr, 1, instructions);
    botMark = swapToLowMem(mgr, &lst, instructions);

    do
    {
        prevInstrs = scanNumRegs(lst).numInstrs;
        if ((optimizationMask & HC_OPT_UNREACHABLE_CODE) != 0)
            removeUnreachableCode(mgr, lst);
        if ((optimizationMask & HC_OPT_CONST_PROP) != 0)
            constantPropagation(mgr, lst);
        if ((optimizationMask & HC_OPT_DEAD_CODE) != 0)
            deadCodeElimination(mgr, lst);
#if GERBERA_ARMV6_JIT
        if (backend == HCARMBackend_create() && (optimizationMask & HC_OPT_LOCAL_COPY_PROP))
            reuseALUValues(lst);
#endif
        if ((optimizationMask & HC_OPT_LOCAL_COPY_PROP) != 0)
            localCopyPropagation(mgr, lst);
        if ((optimizationMask & HC_OPT_BRANCHES) != 0)
        {
            swapConditionalBranches(lst);
            removeRedundantBranches(mgr, lst);
        }
        if ((optimizationMask & HC_OPT_PEEPHOLE) != 0)
        {
            peephole(lst);
#if GERBERA_ARMV6_JIT
            if (backend == HCARMBackend_create())
                cseInvariantR0Loads(lst);
#endif
            peepholeRepackStore(mgr, lst, 0);
        }
        if ((optimizationMask & HC_OPT_SHIFTS) != 0)
        {
            liveRangeAnalysis(mgr, lst, 1);
            shiftLogicalAndCombine(mgr, lst);
            shiftChainCombine(mgr, lst);
            validateProgram(mgr, 1, lst);
        }
        if ((optimizationMask & HC_OPT_LOCAL_COPY_PROP) != 0)
            localCopyPropagation(mgr, lst);
        if ((optimizationMask & HC_OPT_DEAD_CODE) != 0)
            deadCodeElimination(mgr, lst);
        curInstrs = scanNumRegs(lst).numInstrs;
    }
    while (curInstrs < prevInstrs);

    if ((optimizationMask & HC_OPT_INLINE_FILLS) != 0)
    {
        int topMark;
        peepholeFills(mgr, lst);
        topMark = swapToHiMem(mgr, &lst, lst);
        HCArena_releaseBottom(mgr, botMark);
        botMark = HCArena_markBottom(mgr);
        lst = inlineFills(mgr, lst);
        peephole(lst);
        validateProgram(mgr, 1, lst);
        HCArena_releaseTop(mgr, topMark);
    }

    if (inputParamsUsed != NULL)
        computeInputParamMask(inputParamsUsed, numStackParameters, lst);

    if (backend != NULL)
    {
        int topMark;
        HCBackendInfo_s beInfo;
        void *codePtr = NULL;
        int nBytes;

#if GERBERA_ARMV6_JIT
        if (backend == HCARMBackend_create() &&
            (optimizationMask & (HC_OPT_PEEPHOLE | HC_OPT_DEAD_CODE)) == (HC_OPT_PEEPHOLE | HC_OPT_DEAD_CODE) &&
            peepholeRepackStore(mgr, lst, 1))
            deadCodeElimination(mgr, lst);
        if ((optimizationMask & HC_OPT_SPAN_GRADIENTS) != 0 && backend == HCARMBackend_create())
        {
            rematerializeSpanGradients(mgr, lst, nHardRegs, 0);
            if ((optimizationMask & HC_OPT_DEPTH_REJECT_RUNS) != 0)
                rematerializeSpanGradients(mgr, lst, nHardRegs, 1);
        }
#endif
        offsetRegs(lst, nHardRegs);
        topMark = swapToHiMem(mgr, &lst, lst);
        HCArena_releaseBottom(mgr, botMark);

        if (backend->preRegallocFixer != NULL)
        {
            botMark = preRegallocFix(mgr, backend, &lst, nHardRegs);
            HCArena_releaseTop(mgr, topMark);
            topMark = swapToHiMem(mgr, &lst, lst);
            HCArena_releaseBottom(mgr, botMark);
        }

        botMark = fixImmediates(mgr, backend, &lst);
        HCArena_releaseTop(mgr, topMark);
        validateProgram(mgr, nHardRegs, lst);

        if ((optimizationMask & HC_OPT_REGALLOC) != 0)
        {
            int doSpillOpts = 1;
            lst = registerAllocation(mgr, lst, spillStart, nHardRegs, scheduleARM11);
            if (backend->optimizationMask != NULL)
                doSpillOpts = (backendOptions & HC_BACKEND_NO_SPILL_OPT) == 0;
            if (doSpillOpts != 0)
            {
                coalesceSpillLocations(mgr, spillStart, lst);
                optimizeLoopConstantLoads(mgr, inputParamMutable, spillStart, lst);
            }
        }
        else
        {
            lst = panicRegisterAllocation(mgr, backend, spillStart, lst);
        }

        removeRedundantSpills(mgr, lst);
        packSpillLocations(mgr, lst, spillStart);
        sortSpillSlots(mgr, lst, spillStart);
        offsetStackReferences(backend, numStackParameters, lst);
        botMark = postRegallocFix(mgr, backend, &lst, nHardRegs);
        (void)botMark;
        deadCodeElimination(mgr, lst);

        if (emuDst != NULL)
            copyInstrs(emuDst, lst);

#if GERBERA_ARMV6_JIT
        if (scheduleARM11)
        {
            renameScratchLoads(mgr, lst, nHardRegs);
            armListScheduler(mgr, lst);
            removeRedundantSpills(mgr, lst);
        }
        else
#endif
            loadScheduler(mgr, lst);
        remapRegisters(backend, lst);
        beInfo = computeBackEndInfo(mgr, lst, nHardRegs);
        nBytes = backend->codegen(mgr, &codePtr, lst, &beInfo, spillStart);
        *output = codePtr;
        return nBytes;
    }
    else
    {
        *output = lst;
        return (int)(sizeof(HCInstr_s) * (size_t)scanNumRegs(lst).numInstrs);
    }
}

static int compileWrap(void *args)
{
    CompParams_s *p = (CompParams_s *)args;
    return compileInternal(p->mgr, p->output, p->emuDst, p->instructions,
                           p->backend, p->optimizationMask,
                           p->inputParamsUsed, p->inputParamMutable,
                           p->numStackParameters);
}

int HC_compilePriv(
    void *arena,
    int arenaSize,
    HCSetjmpConfig_s *setjmpConfig,
    void **output,
    HCInstr_s *emuDst,
    HCInstr_s *instructions,
    HCBackend_s *backend,
    unsigned int optimizationMask,
    unsigned char *inputParamsUsed,
    unsigned char *inputParamMutable,
    int numStackParameters)
{
    HCArena_s mgr;
    CompParams_s params;
    int jmpbufSize = (int)setjmpConfig->jmpbufSize;

    if (arenaSize - jmpbufSize <= 0)
        return -1;

    HCArena_init(&mgr, (char *)arena + jmpbufSize, arenaSize - jmpbufSize);
    mgr.longjmp = setjmpConfig->longjmp;
    mgr.jmpbuf  = arena;

    params.mgr                = &mgr;
    params.output             = output;
    params.emuDst             = emuDst;
    params.instructions       = instructions;
    params.backend            = backend;
    params.optimizationMask   = optimizationMask;
    params.inputParamsUsed    = inputParamsUsed;
    params.inputParamMutable  = inputParamMutable;
    params.numStackParameters = numStackParameters;

    return setjmpConfig->setjmpWrap(mgr.jmpbuf, compileWrap, &params);
}

int HC_compile(
    void *arena,
    int arenaSize,
    HCSetjmpConfig_s *setjmpConfig,
    void **output,
    HCInstr_s *instructions,
    HCBackend_s *backend,
    unsigned int optimizationMask,
    unsigned char *inputParamsUsed,
    unsigned char *inputParamMutable,
    int numStackParameters)
{
    return HC_compilePriv(arena, arenaSize, setjmpConfig, output, NULL,
                          instructions, backend, optimizationMask,
                          inputParamsUsed, inputParamMutable,
                          numStackParameters);
}

int HC_extraStackSpace(void *backend)
{
    HCBackend_s *be = (HCBackend_s *)backend;
    if (be != NULL)
        return be->internalStackSpace() + 4;
    else
        return 0;
}
