.syntax unified
.cpu arm7tdmi
.arm
.section .header,"ax",%progbits
.global md2gba_entry_branch
md2gba_entry_branch:
    b md2gba_start

.section .text.start,"ax",%progbits
.global md2gba_start
.type md2gba_start,%function
md2gba_start:
    /* No stack is needed until relocation is complete. */
    ldr r0, =__iwram_load__
    ldr r1, =__iwram_start__
    ldr r2, =__iwram_end__
1:  cmp r1, r2
    bhs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r0, =__data_load__
    ldr r1, =__data_start__
    ldr r2, =__data_end__
3:  cmp r1, r2
    bhs 4f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 3b
4:
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    mov r3, #0
5:  cmp r1, r2
    bhs 6f
    str r3, [r1], #4
    b 5b
6:
    ldr sp, =0x03007F00
    ldr r0, =md2gba_ps4_whole_live_main
    bx r0
7:  b 7b
.size md2gba_start, .-md2gba_start
