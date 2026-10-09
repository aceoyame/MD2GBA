.syntax unified
.cpu arm7tdmi
.arm
.section .text.md2gba_irq_entry,"ax",%progbits
.align 2
.global md2gba_gba_pcm_irq_handler
.type md2gba_gba_pcm_irq_handler,%function
.extern md2gba_gba_pcm_irq_body
md2gba_gba_pcm_irq_handler:
    /* BIOS reaches the user vector in IRQ mode. Preserve the exact interrupted
       SPSR/LR, run the C body on the normal System stack, then return to the
       BIOS IRQ epilogue. */
    mrs r0, spsr
    stmfd sp!, {r0, lr}

    mrs r1, cpsr
    bic r1, r1, #0x1f
    orr r1, r1, #0x1f
    msr cpsr_c, r1

    stmfd sp!, {r0, lr}
    bl md2gba_gba_pcm_irq_body
    ldmfd sp!, {r0, lr}

    mrs r1, cpsr
    bic r1, r1, #0x1f
    orr r1, r1, #0x92
    msr cpsr_c, r1
    ldmfd sp!, {r0, lr}
    msr spsr_cxsf, r0
    bx lr
.size md2gba_gba_pcm_irq_handler, .-md2gba_gba_pcm_irq_handler
