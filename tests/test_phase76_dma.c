#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"

static int fail(const char*s){puts(s);return 1;}
static unsigned bitcount32(unsigned x){unsigned n=0;while(x){n+=x&1u;x>>=1;}return n;}
static unsigned count_pages(const Md2GbaVdp*v){unsigned i,n=0;for(i=0;i<8u;i++)n+=bitcount32(v->vram_dirty_pages[i]);return n;}
static unsigned count_tiles(const Md2GbaVdp*v){unsigned i,n=0;for(i=0;i<64u;i++)n+=bitcount32(v->vram_dirty_tiles[i]);return n;}

int main(void){
    static unsigned char rom[0x400];
    static Md2GbaBus b;
    unsigned e;
    md2gba_bus_init(&b,rom,sizeof(rom));
    md2gba_write16(&b,0xFF1000u,0x1122u);md2gba_write16(&b,0xFF1002u,0x3344u);
    md2gba_write16(&b,0xFF1004u,0x5566u);md2gba_write16(&b,0xFF1006u,0x7788u);
    md2gba_vdp_clear_dirty(&b.vdp); e=b.vdp.vram_epoch;
    md2gba_write16(&b,0xC00004u,0x8110u); /* DMA enable */
    md2gba_write16(&b,0xC00004u,0x8F02u); /* increment 2 */
    md2gba_write32(&b,0xC00004u,0x94009304u); /* four words */
    md2gba_write16(&b,0xC00004u,0x9500u);md2gba_write16(&b,0xC00004u,0x9688u);md2gba_write16(&b,0xC00004u,0x977Fu); /* FF1000/2 */
    md2gba_write32(&b,0xC00004u,0x60000080u); /* DMA -> VRAM $2000 */
    if(memcmp(&b.vdp.vram[0x2000],"\x11\x22\x33\x44\x55\x66\x77\x88",8))return fail("bulk DMA data mismatch");
    if(b.vdp.vram_epoch!=e+1u)return fail("bulk DMA should advance VRAM epoch once");
    if(count_pages(&b.vdp)!=1u || count_tiles(&b.vdp)!=1u)return fail("bulk DMA dirtied outside exact page/tile");
    if(!md2gba_vdp_vram_range_dirty(&b.vdp,0x2000u,8u))return fail("bulk DMA exact range not dirty");
    if(md2gba_vdp_vram_range_dirty(&b.vdp,0x2100u,8u))return fail("bulk DMA adjacent page spuriously dirty");

    /* Standard 3-button MD pad seam: TH high = B/C, TH low = A/Start. */
    md2gba_bus_set_pad(&b,0u,MD2GBA_PAD_START|MD2GBA_PAD_A|MD2GBA_PAD_RIGHT);
    md2gba_write8(&b,0xA10003u,0x40u);
    if((md2gba_read8(&b,0xA10003u)&0x08u)!=0u)return fail("RIGHT not visible in TH-high phase");
    md2gba_write8(&b,0xA10003u,0x00u);
    if((md2gba_read8(&b,0xA10003u)&0x30u)!=0u)return fail("A/START not visible in TH-low phase");

    puts("PASS P76 bulk DMA exact dirtiness + 3-button pad protocol");
    return 0;
}
