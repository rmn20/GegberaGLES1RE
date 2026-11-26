/* hcCompiler.h - HC IR optimizer, register allocator, and JIT driver
 * (hcCompiler.h / hcCompiler.c / hcPanicRegalloc.inl).
 *
 * Layout and entrypoints recovered from docs/libGLES_CM.dll.c:46514-51794,
 * CodeView NB11 sstGlobalTypes (TI=0x1737, 0x1AAB..0x1B70), and ARM EKA2
 * docs/libgles_cm.dll.epoc.c (compileWrap sub_2C298, HC_compile sub_2C76C).
 */
#ifndef GERBERA_HC_COMPILER_H
#define GERBERA_HC_COMPILER_H

#include "gerbera/hc/hcBackend.h"
#include "gerbera/hc/hcBackendInfo.h"
#include "gerbera/hc/hcBasicBlock.h"
#include "gerbera/hc/hcMemory.h"
#include "gerbera/hc/hcOpcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Setjmp/longjmp out-of-scratch-memory escape configuration (TI=0x1737). */
typedef struct HCSetjmpConfig_s
{
    int          (*setjmpWrap)(void *jmpbuf, int (*func)(void *), void *args);
    void         (*longjmp)(void *jmpbuf, int value);
    unsigned int   jmpbufSize;
} HCSetjmpConfig_s;

/* Optimization bitmask flags dispatched in compileInternal (0x45CB20). */
#define HC_OPT_REGALLOC         0x001u
#define HC_OPT_CONST_PROP       0x002u
#define HC_OPT_LOCAL_COPY_PROP  0x004u
#define HC_OPT_UNREACHABLE_CODE 0x008u
#define HC_OPT_DEAD_CODE        0x010u
#define HC_OPT_BRANCHES         0x020u
#define HC_OPT_PEEPHOLE         0x040u
#define HC_OPT_SHIFTS           0x080u
#define HC_OPT_SCHEDULE         0x100u
#define HC_OPT_INLINE_FILLS     0x200u
#define HC_OPT_SPAN_GRADIENTS    0x400u
#define HC_OPT_DEPTH_REJECT_RUNS   0x800u
#define HC_OPT_ALL              0x3FFu

int HC_containsInt(const int *arr, int length, int v);
int HC_isSpillStore(HCInstr_s *instr);
int HC_isSpillLoad(HCInstr_s *instr);
/* Replace reads of an invariant parameter with its compile-time value.
 * Returns zero if the program writes that parameter. */
int HC_setConstantParameter(HCInstr_s *instructions, int byteOffset, int value);
int HC_buildBasicBlocks(HCArena_s *mgr, BasicBlock_s **result, HCInstr_s *lst);
HCBackendInfo_s computeBackEndInfo(HCArena_s *mgr, HCInstr_s *lst, int nHardRegs);

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
    int numStackParameters);

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
    int numStackParameters);

int HC_extraStackSpace(void *backend);

#ifdef __cplusplus
}
#endif

#endif /* GERBERA_HC_COMPILER_H */
