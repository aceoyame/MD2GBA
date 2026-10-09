OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(md2gba_entry_branch)

MEMORY {
    ROM (rx)   : ORIGIN = 0x08000000, LENGTH = 32M
    EWRAM (rw) : ORIGIN = 0x02000000, LENGTH = 256K
    IWRAM (rwx): ORIGIN = 0x03000000, LENGTH = 28K
}

SECTIONS {
    .header 0x08000000 : { KEEP(*(.header)) } > ROM

    .text 0x080000C0 : {
        *(.text.start)
        /* P76: the frontend dispatcher runs only at scheduler/render boundaries,
           not per translated instruction. Keep it in prefetched Game Pak ROM
           and reserve scarce IWRAM for the CPU/bus/renderer and FIFO IRQ. */
        *000_main.o(.text*)
        /* P88 / PSC-style residency: software composition is a cold fallback,
           not a zero-wait-RAM resident engine. Keep only bus/timing/cycle/IRQ
           plus explicitly tagged hot sections in IWRAM. */
        *(EXCLUDE_FILE (*000_main.o *001_bus.o *004_timing.o *005_cycles.o *007_vblank_irq.o) .text*)
        *(.rom.cold*)
        *(.rodata*)
        . = ALIGN(4);
    } > ROM

    .iwram : {
        . = ALIGN(4);
        __iwram_start__ = .;
        /* generic Sonic experiment: bus stays ROM; explicit .iwram.* kernels still move */
        *004_timing.o(.text*)
        *005_cycles.o(.text*)
        *007_vblank_irq.o(.text*)
        *(.iwram)
        *(.iwram.*)
        . = ALIGN(4);
        __iwram_end__ = .;
    } > IWRAM AT> ROM
    __iwram_load__ = LOADADDR(.iwram);

    .data : {
        . = ALIGN(4);
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > EWRAM AT> ROM
    __data_load__ = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(4); __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > EWRAM
    /DISCARD/ : { *(.comment*) *(.ARM.attributes*) *(.eh_frame*) *(.ARM.exidx*) *(.ARM.extab*) }
}
