.syntax unified
.cpu arm7tdmi
.arm
.text
.align 2

/* Freestanding ARM7 helpers needed by the source-derived native sequence
   player. They avoid pulling a hosted runtime; divisions are control-rate
   sequence operations, never the 13.379-kHz inner sample loop. */
.global __aeabi_lmul
.type __aeabi_lmul,%function
__aeabi_lmul:
    push {r4}
    mov r4, r0
    umull r0, r12, r0, r2
    mla r12, r1, r2, r12
    mla r1, r4, r3, r12
    pop {r4}
    bx lr
.size __aeabi_lmul,.-__aeabi_lmul

.global __aeabi_llsl
.type __aeabi_llsl,%function
__aeabi_llsl:
    cmp r2,#0
    bxeq lr
    cmp r2,#32
    bhs 1f
    rsb r3,r2,#32
    mov r1,r1,lsl r2
    orr r1,r1,r0,lsr r3
    mov r0,r0,lsl r2
    bx lr
1:  cmp r2,#64
    movhs r0,#0
    movhs r1,#0
    bxhs lr
    sub r2,r2,#32
    mov r1,r0,lsl r2
    mov r0,#0
    bx lr
.size __aeabi_llsl,.-__aeabi_llsl

.global __aeabi_llsr
.type __aeabi_llsr,%function
__aeabi_llsr:
    cmp r2,#0
    bxeq lr
    cmp r2,#32
    bhs 1f
    rsb r3,r2,#32
    mov r0,r0,lsr r2
    orr r0,r0,r1,lsl r3
    mov r1,r1,lsr r2
    bx lr
1:  cmp r2,#64
    movhs r0,#0
    movhs r1,#0
    bxhs lr
    sub r2,r2,#32
    mov r0,r1,lsr r2
    mov r1,#0
    bx lr
.size __aeabi_llsr,.-__aeabi_llsr

.global __aeabi_uldivmod
.type __aeabi_uldivmod,%function
__aeabi_uldivmod:
    /* in: n=r1:r0, d=r3:r2. out: q=r1:r0, rem=r3:r2 */
    push {r4-r10,lr}
    orrs r12,r2,r3
    bne 1f
    mov r2,r0
    mov r3,r1
    mov r0,#0
    mov r1,#0
    pop {r4-r10,pc}
1:
    mov r9,r2
    mov r10,r3
    mov r4,#0              /* quotient lo */
    mov r5,#0              /* quotient hi */
    mov r6,#0              /* remainder lo */
    mov r7,#0              /* remainder hi */
    mov r8,#64
2:
    mov r3,r1,lsr #31      /* next numerator bit */
    movs r0,r0,lsl #1
    adc r1,r1,r1
    movs r6,r6,lsl #1
    adc r7,r7,r7
    orr r6,r6,r3
    movs r4,r4,lsl #1
    adc r5,r5,r5
    cmp r7,r10
    blo 3f
    bhi 4f
    cmp r6,r9
    blo 3f
4:
    subs r6,r6,r9
    sbc r7,r7,r10
    orr r4,r4,#1
3:
    subs r8,r8,#1
    bne 2b
    mov r0,r4
    mov r1,r5
    mov r2,r6
    mov r3,r7
    pop {r4-r10,pc}
.size __aeabi_uldivmod,.-__aeabi_uldivmod

.global __aeabi_uidivmod
.type __aeabi_uidivmod,%function
__aeabi_uidivmod:
    /* in r0=n,r1=d; out r0=q,r1=rem */
    push {lr}
    mov r2,r1
    mov r1,#0
    mov r3,#0
    bl __aeabi_uldivmod
    mov r1,r2
    pop {pc}
.size __aeabi_uidivmod,.-__aeabi_uidivmod
