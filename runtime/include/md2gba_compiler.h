#ifndef MD2GBA_COMPILER_H
#define MD2GBA_COMPILER_H

/* Compiler-only publication barrier.  MD2GBA runs on one ARM7 core and
   masks interrupts around shared foreground/IRQ state, so a CPU memory fence
   is unnecessary here.  This prevents Clang/GCC from moving ordinary staging
   RAM accesses across a ready flag or a DMA trigger as optimization policy
   evolves (including future LTO builds). */
#if defined(__clang__) || defined(__GNUC__)
#define MD2GBA_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define MD2GBA_COMPILER_BARRIER() ((void)0)
#endif

#endif
