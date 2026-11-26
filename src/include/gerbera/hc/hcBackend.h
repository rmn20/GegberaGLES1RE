/* JIT backend callbacks. Offsets below are for the 32-bit reference ABI.
 * Nokia's x86 and ARM builds both have 12 slots (CodeView TI=0x19D4;
 * ARM backendDecl at 0x36F6C). Slot 9 is disasm (procedure TI=0x19D1).
 * UIQ 1.4.8 omits disasm and uses an incompatible 11-slot layout.
 */
#ifndef GERBERA_HC_BACKEND_H
#define GERBERA_HC_BACKEND_H

#include "gerbera/hc/hcOpcode.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GERBERA_ARMV6_JIT
#if defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
#define GERBERA_ARMV6_JIT 1
#else
#define GERBERA_ARMV6_JIT 0
#endif
#endif

enum
{
    HC_BACKEND_NO_SPILL_OPT = 1,
    HC_BACKEND_ARM11_SCHEDULE = 2
};

typedef struct HCBackend_s
{
    int (*canCreateImmediate)(HCInstr_s *instr);                    /* +0  */
    int (*internalStackSpace)(void);                                /* +4  */
    int (*internalStackSpaceAfterInvars)(void);                     /* +8  */
    unsigned char *(*targetRegisterMapping)(void);                  /* +12 */
    int (*numHardRegs)(void);                                       /* +16 */
    int (*preRegallocFixer)(void *mgr, HCInstr_s **dest,
                            HCInstr_s *src, void *compHandle);      /* +20 */
    int (*codegen)(void *mgr, void **destination, HCInstr_s *instrs,
                   void *info, int spillStart);                     /* +24 */
    int (*postRegallocFixer)(void *mgr, HCInstr_s **dest,
                             HCInstr_s *src, void *compInfo);       /* +28 */
    void (*emulate)(void (*program)(void *), void *stackPtr);       /* +32 */
    void (*disasm)(void *program, int length, void (*output)(char *)); /* +36 */
    int (*isNative)(void);                                          /* +40 */
    int (*optimizationMask)(void);                                  /* +44 */
} HCBackend_s;

HCBackend_s *HCX86Backend_create(void);
HCBackend_s *HCARMBackend_create(void);

#ifdef __cplusplus
}
#endif

#endif /* GERBERA_HC_BACKEND_H */
