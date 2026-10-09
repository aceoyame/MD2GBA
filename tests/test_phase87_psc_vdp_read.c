#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"

static uint32_t vdp_cmd(uint16_t addr, unsigned code) {
    return ((uint32_t)(addr & 0x3fffu) << 16) |
           ((uint32_t)(code & 3u) << 30) |
           ((uint32_t)(addr >> 14) & 3u) |
           ((uint32_t)(code & 0x3cu) << 2);
}
static void issue(Md2GbaBus *b,uint16_t addr,unsigned code){
    uint32_t c=vdp_cmd(addr,code);
    md2gba_vdp_control16_direct(b,(uint16_t)(c>>16));
    md2gba_vdp_control16_direct(b,(uint16_t)c);
}
int main(void){
    static uint8_t rom[0x200]; Md2GbaBus b; memset(rom,0,sizeof rom); md2gba_bus_init(&b,rom,sizeof rom);
    b.vdp.regs[15]=2;
    b.vdp.vram[0x1234]=0x56; b.vdp.vram[0x1235]=0x78;
    issue(&b,0x1234,0x00); assert(b.vdp.target==MD2GBA_VDP_VRAM); assert(md2gba_read16(&b,0xC00000)==0x5678); assert(b.vdp.address==0x1236);
    b.vdp.cram[0x1a]=0x0abc; issue(&b,0x34,0x08); assert(b.vdp.target==MD2GBA_VDP_CRAM); assert(md2gba_read16(&b,0xC00000)==0x0abc); assert(b.vdp.address==0x36);
    b.vdp.vsram[3]=0x07de; issue(&b,6,0x04); assert(b.vdp.target==MD2GBA_VDP_VSRAM); assert(md2gba_read16(&b,0xC00000)==0x07de); assert(b.vdp.address==8);
    /* PSC wraps the VSRAM backing selection through the 0x50-byte visible window. */
    b.vdp.vsram[1]=0x1357; issue(&b,0x52,0x04); assert(md2gba_read16(&b,0xC00000)==0x1357);
    puts("PASS P87 PSC-derived VDP data-port reads"); return 0;
}
