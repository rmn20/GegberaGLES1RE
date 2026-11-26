/* hcOpcode.h - HC instruction encoding (hcOpcode.h / hcOpcode.c).
 *
 * An HCInstr is two 32-bit words:
 *   instr0: bits 0-7 = opcode, bit 8 = hasImm, bit 9 = (flags bit 1),
 *           bits 10-20 = dst register, bits 21-31 = src0 register
 *   instr1: src1 register (bits 0-10) or immediate value (full word)
 *
 * The opcode table HC_opcodeDecls[HCOP_MAX] encodes per-opcode metadata:
 *   bits 0-2: argument count
 *   bits 3-4: type (0=ALUOP, 1=MEMOP, 2=BRANCH)
 *   bit 5: commutative
 *   bits 6-7: dst flags (0=NORMAL, 1=AS_SRC, 2=AS_LABEL, 3=NOT_AVAILABLE)
 *
 * The first 55 enum names and values are pinned from CodeView LF_ENUMERATE
 * records in docs/libGLES_CM.dll (0x158e0b..0x15fc1b).
 */
#ifndef GERBERA_HC_OPCODE_H
#define GERBERA_HC_OPCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- instruction ---------------------------------------------------------- */
typedef struct HCInstr_s
{
    unsigned int instr0;
    unsigned int instr1;
} HCInstr_s;

/* --- opcode enum (exact CodeView LF_ENUMERATE order from libGLES_CM.dll) -- */
typedef enum HCOP_e
{
    HCOP_ADD   = 0,
    HCOP_AND   = 1,
    HCOP_ASD   = 2,
    HCOP_ASR   = 3,
    HCOP_BIC   = 4,
    HCOP_LSD   = 5,
    HCOP_LSL   = 6,
    HCOP_LSR   = 7,
    HCOP_MAXS  = 8,
    HCOP_MAXU  = 9,
    HCOP_MINS  = 10,
    HCOP_MINU  = 11,
    HCOP_MULS  = 12,
    HCOP_MULU  = 13,
    HCOP_MOV   = 14,
    HCOP_MOVN  = 15,
    HCOP_NOP   = 16,
    HCOP_OR    = 17,
    HCOP_ROL   = 18,
    HCOP_ROR   = 19,
    HCOP_SUB   = 20,
    HCOP_SUBR  = 21,
    HCOP_XOR   = 22,
    HCOP_LBS   = 23,
    HCOP_LBU   = 24,
    HCOP_LWS   = 25,
    HCOP_LWU   = 26,
    HCOP_LW2S  = 27,
    HCOP_LW2U  = 28,
    HCOP_LD    = 29,
    HCOP_LD4   = 30,
    HCOP_SB    = 31,
    HCOP_SW    = 32,
    HCOP_SW2   = 33,
    HCOP_SD    = 34,
    HCOP_SD4   = 35,
    HCOP_FILLB = 36,
    HCOP_FILLW = 37,
    HCOP_FILLD = 38,
    HCOP_ANSA  = 39,
    HCOP_BRA   = 40,
    HCOP_CBA   = 41,
    HCOP_CBEQ  = 42,
    HCOP_CBGES = 43,
    HCOP_CBGEU = 44,
    HCOP_CBGTS = 45,
    HCOP_CBGTU = 46,
    HCOP_CBLES = 47,
    HCOP_CBLEU = 48,
    HCOP_CBLTS = 49,
    HCOP_CBLTU = 50,
    HCOP_CBNA  = 51,
    HCOP_CBNEQ = 52,
    HCOP_EOP   = 53,
    HCOP_LABEL = 54,
    /* Internal ARM lowering; not bytecode instructions. */
    HCOP_UQADD8 = 55,
    HCOP_UQSUB8 = 56,
    HCOP_UXTB16 = 57,
    HCOP_UXTAB16_ROR8 = 58,
    HCOP_UXTB_ROR = 59,
    /* The packed blend pseudo-ops lower before allocation. */
    HCOP_PACKED_ALPHA_A = 60,
    HCOP_PACKED_ALPHA_B = 61,
    HCOP_UQADD8_RGB = 62,
    HCOP_UQSUB8_RGB = 63,
    HCOP_PACKED_ALPHA_RGB_A = 64,
    HCOP_PACKED_ALPHA_RGB_B = 65,
    HCOP_MAX   = 66
} HCOP_e;

/* opcode types (HC_opcodeDecls bits 3-4) */
typedef enum HCOP_TYPE_e
{
    HCOP_TYPE_ALUOP  = 0,
    HCOP_TYPE_MEMOP  = 1,
    HCOP_TYPE_BRANCH = 2,
    HCOP_TYPE_SIZE   = 2
} HCOP_TYPE_e;

/* dst flags (HC_opcodeDecls bits 6-7) */
typedef enum HCOP_DST_e
{
    HCOP_DST_NORMAL        = 0,
    HCOP_DST_AS_SRC        = 1,
    HCOP_DST_AS_LABEL      = 2,
    HCOP_DST_NOT_AVAILABLE = 3,
    HCOP_DST_SIZE          = 2
} HCOP_DST_e;

#define HCOP_OPER_SIZE   8
#define HCOP_DST_SIZE    11
#define HCREG_SIZE       11
#define HC_MAX_REG_INDEX 2048

/* --- opcode table --------------------------------------------------------- */
extern const unsigned char HC_opcodeDecls[HCOP_MAX];

/* --- instruction generation (hcOpcode.c) ---------------------------------- */
HCInstr_s HC_genOp0(int op);
HCInstr_s HC_genOp1(int op, int dst);
HCInstr_s HC_genOp1Imm(int op, int imm);
HCInstr_s HC_genOp2(int op, int dst, int src0);
HCInstr_s HC_genOp2Imm(int op, int dst, int imm);
HCInstr_s HC_genOp3(int op, int dst, int src0, int src1);
HCInstr_s HC_genOp3Imm(int op, int dst, int src0, int imm);
HCInstr_s HC_genOp3Raw(int op, int dst, int src0, int src1);
HCInstr_s HC_genOp3ImmRaw(int op, int dst, int src0, int imm);

/* --- opcode accessors (hcOpcode.h / hcOpcode.c) --------------------------- */
int HC_getOpcodeOper(HCInstr_s *instr);
int HC_getOpcodeArgCountOp(int op);
int HC_getOpcodeArgCount(HCInstr_s *instr);
int HC_getOpcodeDst(HCInstr_s *instr);
int HC_getOpcodeSrc0Raw(HCInstr_s *instr);
int HC_getOpcodeSrc0(HCInstr_s *instr);
int HC_getOpcodeSrc1Raw(HCInstr_s *instr);
int HC_getOpcodeSrc1(HCInstr_s *instr);
int HC_isStoreOp(int op);
int HC_getOpcodeTypeOp(int op);
int HC_getOpcodeFlags(HCInstr_s *instr);
int HC_opcodeHasImm(HCInstr_s *instr);
int HC_opcodeIsCommutative(HCInstr_s *instr);
int HC_getOpcodeImm(HCInstr_s *instr);
int HC_getOpcodeLabel(HCInstr_s *instr);
void HC_setOpcodeSrc1(HCInstr_s *instr, int v);
int HC_getOpcodeType(HCInstr_s *instr);
int HC_isLoadOp(int op);
int HC_isCondBranchOp(int op);
int HC_isAbsBranchOp(int op);
int HC_isBranch(HCInstr_s *instr);
int HC_isAbsBranch(HCInstr_s *instr);
void HC_setOpcodeImm(HCInstr_s *instr, int v);
int HC_opcodeDstFlagsOp(int op);
void HC_setOpcodeDst(HCInstr_s *instr, int v);
void HC_setOpcodeNoImm(HCInstr_s *instr);
void HC_setOpcodeSrc0(HCInstr_s *instr, int v);
int HC_isBranchOp(int op);
int HC_min(int a, int b);

int HC_getMemOpIndexSizeOp(int op);
int HC_getMemOpIndexSize(HCInstr_s *instr);
int HC_getDef(HCInstr_s *instr);
int HC_getRegUses(int *dst, HCInstr_s *instr);
int HC_getUses(HCInstr_s *instr, int *uses);
void HC_setUse(HCInstr_s *instr, int which, int newVal);

/* --- hcDefs.c helpers ----------------------------------------------------- */
int HC_bsf(unsigned int x);
void HC_assertFail(const char *expr, const char *file, int line, const char *msg);
#if defined(HG_NO_ASSERTS) || defined(NDEBUG)
#define HC_assertFail(expr, file, line, msg) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* GERBERA_HC_OPCODE_H */
