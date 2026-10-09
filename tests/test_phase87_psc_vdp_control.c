#include <stdio.h>
#include "md2gba_bus.h"
static int fail(const char*s){puts(s);return 1;}
int main(void){
    static unsigned char rom[0x100]; static Md2GbaBus b;
    md2gba_bus_init(&b,rom,sizeof(rom));

    /* Fresh $8xxx is a VDP register write. */
    md2gba_vdp_control16_direct(&b,0x8F02u);
    if(b.vdp.regs[15]!=2u || b.vdp.register_writes!=1u || b.vdp.pending_control)
        return fail("fresh register write decode");

    /* PSC PSII/PSIII both resolve a pending two-word command first.  A second
       command word that happens to look like $8xxx must NOT become a register write. */
    md2gba_vdp_control16_direct(&b,0x4000u);
    if(!b.vdp.pending_control)return fail("first command word not pending");
    md2gba_vdp_control16_direct(&b,0x8000u);
    if(b.vdp.pending_control)return fail("second command word left pending");
    if(b.vdp.register_writes!=1u)return fail("pending $8xxx misdecoded as register write");
    if(b.vdp.control_long!=0x40008000u)return fail("two-word command assembly mismatch");
    if(b.vdp.target!=MD2GBA_VDP_VRAM || (b.vdp.code&0x0Fu)!=1u)
        return fail("pending command target/code mismatch");

    /* Only 100xxxxx xxxxxxxx is the VDP register-write form.  $Axxx is a
       legitimate first command word pattern and must not be consumed as a register. */
    md2gba_vdp_control16_direct(&b,0xA000u);
    if(!b.vdp.pending_control)return fail("fresh $Axxx incorrectly decoded as register write");
    if(b.vdp.register_writes!=1u)return fail("fresh $Axxx incremented register writes");
    md2gba_vdp_control16_direct(&b,0x0000u);
    if(b.vdp.pending_control)return fail("$Axxx command did not complete");

    puts("PASS P87 PSC-derived VDP control-port sequencing");
    return 0;
}
