#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_aot68k.h"

static uint8_t rom[0x100] = {
    /* LEA $C000.W,A4 */       0x49,0xF8,0xC0,0x00,
    /* MOVE.L #$FFFFC000,D0 */ 0x20,0x3C,0xFF,0xFF,0xC0,0x00,
    /* CMPA.W D0,A4 */         0xB8,0xC0,
    /* NOP */                  0x4E,0x71
};

int main(void){
    Md2GbaBus b; Md2Gba68k c;
    md2gba_bus_init(&b,rom,sizeof(rom));
    md68k_reset_context(&c); c.pc=0;
    if(md68k_interp_step(&c,&b)!=MD68K_INTERP_OK){puts("FAIL lea step");return 2;}
    printf("after LEA A4=%08x\n",c.a[4]);
    if(md68k_interp_step(&c,&b)!=MD68K_INTERP_OK){puts("FAIL move step");return 3;}
    if(md68k_interp_step(&c,&b)!=MD68K_INTERP_OK){puts("FAIL cmpa step");return 4;}
    printf("after CMPA SR=%04x Z=%u\n",c.sr,(unsigned)!!(c.sr&MD68K_Z));
    if(c.a[4]!=0xFFFFC000u){printf("FAIL architectural LEA value: %08x expected FFFFC000\n",c.a[4]);return 1;}
    if(!(c.sr&MD68K_Z)){puts("FAIL CMPA.W must compare equal");return 1;}
    puts("PASS abs.w LEA sign-extension + CMPA.W architectural address-register semantics");
    return 0;
}
