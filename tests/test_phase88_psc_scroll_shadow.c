#include <stdint.h>
#include <stdio.h>
#include "md2gba_bus.h"
#include "md2gba_gba_backend.h"
/* Host stubs for target-only publication hooks used by backend.o. */
void md2gba_gba_scroll_dma_set(const uint16_t stream[160][8],int enabled){(void)stream;(void)enabled;}
void md2gba_gba_scroll_latch_direct(const uint16_t scroll[8]){(void)scroll;}
static int fail(const char*s){fprintf(stderr,"FAIL %s\n",s);return 1;}
int main(void){
    static uint8_t rom[0x200]; static Md2GbaBus b; uint16_t out[8]; uint32_t e;
    md2gba_bus_init(&b,rom,sizeof(rom));
    md2gba_vdp_control16_direct(&b,0x8B00u); /* whole-plane H/V scroll */
    md2gba_vdp_control16_direct(&b,0x8D3Fu); /* H-scroll table at FC00 */
    b.vdp.target=MD2GBA_VDP_VRAM; b.vdp.address=0xFC00u;
    md2gba_vdp_data16_direct(&b,0x0010u); /* plane A +16 */
    md2gba_vdp_data16_direct(&b,0xFFF8u); /* plane B -8 */
    b.vdp.target=MD2GBA_VDP_VSRAM; b.vdp.address=0u;
    md2gba_vdp_data16_direct(&b,0x0005u); /* A +5 */
    md2gba_vdp_data16_direct(&b,0xFFF9u); /* B -7 */
    if(!b.vdp.present_scroll_valid)return fail("whole-plane shadow not valid");
    if(b.vdp.present_hscroll[0]!=16 || b.vdp.present_hscroll[1]!=(int16_t)-8)return fail("H shadow mismatch");
    if(b.vdp.present_vscroll[0]!=5 || b.vdp.present_vscroll[1]!=(int16_t)-7)return fail("V shadow mismatch");
    if(md2gba_gba_build_direct_scroll_viewport(&b.vdp,out,40u,32u))return fail("direct viewport rejected");
    if(out[0]!=(uint16_t)24 || out[1]!=(uint16_t)37 || out[2]!=(uint16_t)48 || out[3]!=(uint16_t)25)return fail("projected offsets mismatch");
    if(out[4]!=out[0]||out[5]!=out[1]||out[6]!=out[2]||out[7]!=out[3])return fail("priority-pair mirror mismatch");
    e=b.vdp.present_scroll_epoch;
    md2gba_vdp_control16_direct(&b,0x8B02u); /* per-line H scroll */
    if(b.vdp.present_scroll_valid)return fail("line-scroll should invalidate direct shadow");
    if(b.vdp.present_scroll_epoch==e)return fail("mode transition did not advance presentation epoch");
    puts("PASS P88 PSC-style write-time scroll shadow + live viewport projection");
    return 0;
}
