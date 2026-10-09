#ifndef MD2GBA_SWIZZLE_H
#define MD2GBA_SWIZZLE_H
#include <stdint.h>
#include <stddef.h>

/*
 * The reference commercial compatibility runtime provides strong evidence for a reversed
 * 64-KiB Genesis-WRAM representation.  If the virtual 68K byte address is
 * $FFxxxx, its byte lands at EWRAM_BASE + (0xFFFF - xxxx).
 *
 * This makes a 68K postincrement pointer become a native decrementing pointer
 * and allows little-endian ARM halfword/word accesses to cheaply preserve the
 * 68K's big-endian byte ordering.
 */
#define MD2GBA_SWIZZLED_WRAM_SIZE 0x10000u

static inline uint16_t md2gba_wram_low16(uint32_t genesis_addr) {
    return (uint16_t)genesis_addr;
}
static inline size_t md2gba_swizzled_byte_index(uint32_t genesis_addr) {
    return (size_t)(0xFFFFu - md2gba_wram_low16(genesis_addr));
}
/* Lowest native byte index touched by an aligned 68K word/long access. */
static inline size_t md2gba_swizzled_word_index(uint32_t genesis_addr) {
    return (size_t)(0xFFFEu - md2gba_wram_low16(genesis_addr));
}
static inline size_t md2gba_swizzled_long_index(uint32_t genesis_addr) {
    return (size_t)(0xFFFCu - md2gba_wram_low16(genesis_addr));
}

static inline uint8_t md2gba_swizzled_read8(const uint8_t *ram,uint32_t a) {
    return ram[md2gba_swizzled_byte_index(a)];
}
static inline void md2gba_swizzled_write8(uint8_t *ram,uint32_t a,uint8_t v) {
    ram[md2gba_swizzled_byte_index(a)]=v;
}
static inline uint16_t md2gba_swizzled_read16(const uint8_t *ram,uint32_t a) {
    size_t i=md2gba_swizzled_word_index(a);
    return (uint16_t)ram[i] | ((uint16_t)ram[i+1]<<8);
}
static inline void md2gba_swizzled_write16(uint8_t *ram,uint32_t a,uint16_t v) {
    size_t i=md2gba_swizzled_word_index(a); ram[i]=(uint8_t)v; ram[i+1]=(uint8_t)(v>>8);
}
static inline uint32_t md2gba_swizzled_read32(const uint8_t *ram,uint32_t a) {
    size_t i=md2gba_swizzled_long_index(a);
    return (uint32_t)ram[i] | ((uint32_t)ram[i+1]<<8) | ((uint32_t)ram[i+2]<<16) | ((uint32_t)ram[i+3]<<24);
}
static inline void md2gba_swizzled_write32(uint8_t *ram,uint32_t a,uint32_t v) {
    size_t i=md2gba_swizzled_long_index(a);
    ram[i]=(uint8_t)v; ram[i+1]=(uint8_t)(v>>8); ram[i+2]=(uint8_t)(v>>16); ram[i+3]=(uint8_t)(v>>24);
}
#endif
