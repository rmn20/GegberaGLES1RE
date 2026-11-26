/* ARMv5T integer division (ARM RTABI).
 *
 * Implements __aeabi_uidiv, __aeabi_uidivmod, __aeabi_idiv, __aeabi_idivmod
 * CLZ selects the first quotient bit; each unrolled step consumes one bit.
 */
    .syntax unified
    .arch armv5t
    .arm

    .macro UDIV_STEP shift
    cmp     r1, r3, lsl #\shift
    subcs   r1, r1, r3, lsl #\shift
    adc     r0, r0, r0
    .endm

    .section .text.__aeabi_uidivmod, "ax", %progbits
    .align 2
    .global __aeabi_uidiv
    .hidden __aeabi_uidiv
    .type __aeabi_uidiv, %function
    .global __aeabi_uidivmod
    .hidden __aeabi_uidivmod
    .type __aeabi_uidivmod, %function
__aeabi_uidiv:
__aeabi_uidivmod:
    cmp     r1, #0
    beq     .L_udiv_zero
    cmp     r0, r1
    blo     .L_udiv_lt

    clz     r2, r1                  /* clz(den)                             */
    clz     r3, r0                  /* clz(num)                             */
    sub     r2, r2, r3              /* shift = clz(den) - clz(num) in 0..31 */
    rsb     r2, r2, #31             /* skip = 31 - shift                    */
    add     r2, r2, r2, lsl #1      /* r2 = 3 * skip                        */

    mov     r3, r1                  /* r3 = den                             */
    mov     r1, r0                  /* r1 = rem = num                       */
    mov     r0, #0                  /* r0 = quot = 0                        */

    add     pc, pc, r2, lsl #2      /* jump to A+8 + 12*skip                */
    mov     r0, r0                  /* A+4 pad                              */

    UDIV_STEP 31
    UDIV_STEP 30
    UDIV_STEP 29
    UDIV_STEP 28
    UDIV_STEP 27
    UDIV_STEP 26
    UDIV_STEP 25
    UDIV_STEP 24
    UDIV_STEP 23
    UDIV_STEP 22
    UDIV_STEP 21
    UDIV_STEP 20
    UDIV_STEP 19
    UDIV_STEP 18
    UDIV_STEP 17
    UDIV_STEP 16
    UDIV_STEP 15
    UDIV_STEP 14
    UDIV_STEP 13
    UDIV_STEP 12
    UDIV_STEP 11
    UDIV_STEP 10
    UDIV_STEP 9
    UDIV_STEP 8
    UDIV_STEP 7
    UDIV_STEP 6
    UDIV_STEP 5
    UDIV_STEP 4
    UDIV_STEP 3
    UDIV_STEP 2
    UDIV_STEP 1
    UDIV_STEP 0
    bx      lr

.L_udiv_lt:
    mov     r1, r0
    mov     r0, #0
    bx      lr

.L_udiv_zero:
    mov     r0, #0
    mov     r1, #0
    bx      lr
    .size __aeabi_uidivmod, . - __aeabi_uidivmod

    .section .text.__aeabi_idivmod, "ax", %progbits
    .align 2
    .global __aeabi_idiv
    .hidden __aeabi_idiv
    .type __aeabi_idiv, %function
__aeabi_idiv:
    orrs    ip, r0, r1
    bpl     __aeabi_uidiv
    push    {r4, lr}
    eor     r4, r0, r1
    cmp     r0, #0
    rsbmi   r0, r0, #0
    cmp     r1, #0
    rsbmi   r1, r1, #0
    bl      __aeabi_uidiv
    cmp     r4, #0
    rsbmi   r0, r0, #0
    pop     {r4, pc}
    .size __aeabi_idiv, . - __aeabi_idiv

    .global __aeabi_idivmod
    .hidden __aeabi_idivmod
    .type __aeabi_idivmod, %function
__aeabi_idivmod:
    orrs    ip, r0, r1
    bpl     __aeabi_uidivmod
    push    {r4, r5, r6, lr}
    eor     r4, r0, r1              /* bit 31 of r4 = sign of quotient      */
    movs    r5, r0                  /* bit 31 of r5 = sign of remainder     */
    rsbmi   r0, r0, #0
    cmp     r1, #0
    rsbmi   r1, r1, #0
    bl      __aeabi_uidivmod
    cmp     r4, #0
    rsbmi   r0, r0, #0
    cmp     r5, #0
    rsbmi   r1, r1, #0
    pop     {r4, r5, r6, pc}
    .size __aeabi_idivmod, . - __aeabi_idivmod
