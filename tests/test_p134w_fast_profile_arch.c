#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "md2gba_bus.h"
#include "md2gba_interp68k.h"
#include "md2gba_timing.h"
#define MD2GBA_AOT_GENERATED_FAST 1
#include "profiles/ps4/m4a/generated/rawcache_p116/md2gba_profile_predecode_kos.c"
#undef md2gba_profile_predecode_kos
#undef md2gba_profile_predecode_kos_reset
/* Rename the Nemesis exported functions so both generated C files can coexist. */
#define md2gba_profile_raw_nem_reset nem_reset_test
#define md2gba_profile_raw_nem nem_one_test
#define md2gba_profile_raw_nem_vblank_batch nem_batch_test
#include "profiles/ps4/m4a/generated/rawnem_p123/md2gba_profile_raw_nem.c"

int main(void){
    static uint8_t rom[4]={0}; Md2GbaBus b; Md2Gba68k c; uint32_t cost=0,steps=0; unsigned i;
    md2gba_bus_init(&b,rom,sizeof rom);
    memset(&c,0,sizeof c);
    const KosRec *k=&kos_0[0];
    c.a[0]=0x5A000000u|(k->pre_a0&0xFFFFFFu); c.a[1]=0xA5000000u|(k->pre_a1&0xFFFFFFu); c.sr=k->pre_sr;
    for(i=0;i<7;i++) c.d[i]=k->pre_d[i];
    md2gba_profile_predecode_kos_reset();
    if(!md2gba_profile_predecode_kos(&c,&b,(uint32_t)k->cycles+1u,&cost,&steps)){puts("FAIL kos refused");return 1;}
    if((c.a[0]&0xFF000000u)!=0x5A000000u || (c.a[1]&0xFF000000u)!=0xA5000000u){printf("FAIL kos upper a0=%08x a1=%08x\n",c.a[0],c.a[1]);return 2;}
    if((c.a[0]&0xFFFFFFu)!=(k->post_a0&0xFFFFFFu)||(c.a[1]&0xFFFFFFu)!=(k->post_a1&0xFFFFFFu)){puts("FAIL kos low24");return 3;}

    memset(&c,0,sizeof c); md2gba_bus_init(&b,rom,sizeof rom); const NemRec *n=&nem_0[0];
    c.a[0]=0x5A000000u|(n->pre_a0&0xFFFFFFu); c.a[5]=0xA5000000u|(n->pre_a5&0xFFFFFFu); c.sr=n->pre_sr;
    c.a[1]=0x00FFE200u;c.a[2]=0x00FFFB0Eu;c.a[3]=nem_assets[0].a3;c.a[4]=0x00C00000u;c.a[6]=0x00C00004u;
    for(i=0;i<8;i++) c.d[i]=n->pre_d[i];
    nem_reset_test(); cost=steps=0;
    if(!nem_one_test(&c,&b,0x7fffffffu,&cost,&steps)){puts("FAIL nem refused");return 4;}
    if((c.a[0]&0xFF000000u)!=0x5A000000u || (c.a[5]&0xFF000000u)!=0xA5000000u){printf("FAIL nem upper a0=%08x a5=%08x\n",c.a[0],c.a[5]);return 5;}
    if((c.a[0]&0xFFFFFFu)!=(n->post_a0&0xFFFFFFu)||(c.a[5]&0xFFFFFFu)!=(n->post_a5&0xFFFFFFu)){puts("FAIL nem low24");return 6;}
    puts("PASS P134W fast-profile architectural pointer upper-byte preservation"); return 0;
}
