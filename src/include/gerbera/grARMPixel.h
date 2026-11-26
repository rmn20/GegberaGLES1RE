#ifndef GERBERA_ARM_PIXEL_H
#define GERBERA_ARM_PIXEL_H

#include "gerbera/grState.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the emitted byte count, or zero when the signature needs HC. */
int grCompileARMPixelPipeline(void *output, int capacity, const _grSignature *signature,
                              unsigned int textureLayout, unsigned int *gradientMask);

/* Extended compiler that also emits direct ARMv6 loops for fog and alpha test. */
int grCompileARMPixelPipelineEx(void *output, int capacity, const _grSignature *signature,
                                unsigned int textureLayout, unsigned int *gradientMask,
                                int allowExtended);

#ifdef __cplusplus
}
#endif

#endif
