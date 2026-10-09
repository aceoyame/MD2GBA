#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
static Md2GbaBus busa,busb; static uint8_t *rom; static unsigned seed=0x87a55a31u; static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static unsigned fused0(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x005364u;goto L_005364;}
    c->pc=0x00535Au;goto L_00535A;
  }
L_00535A:; L_005364:; *budget=left;return steps;}
static unsigned fused1(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x0160BAu;goto L_0160BA;}
    c->pc=0x0160ACu;goto L_0160AC;
  }
L_0160AC:; L_0160BA:; *budget=left;return steps;}
static unsigned fused2(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x016B3Au;goto L_016B3A;}
    c->pc=0x016B2Cu;goto L_016B2C;
  }
L_016B2C:; L_016B3A:; *budget=left;return steps;}
static unsigned fused3(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x01BD38u;goto L_01BD38;}
    c->pc=0x01BD2Au;goto L_01BD2A;
  }
L_01BD2A:; L_01BD38:; *budget=left;return steps;}
static unsigned fused4(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x01C958u;goto L_01C958;}
    c->pc=0x01C94Au;goto L_01C94A;
  }
L_01C94A:; L_01C958:; *budget=left;return steps;}
static unsigned fused5(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[2],value=0u;uint32_t saddr=c->a[1];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){value=md2gba_read16(b,saddr);saddr+=2u;md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(28);}
    c->a[2]=dbase;c->a[1]=saddr;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x01CB4Au;goto L_01CB4A;}
    c->pc=0x01CB40u;goto L_01CB40;
  }
L_01CB40:; L_01CB4A:; *budget=left;return steps;}
static unsigned fused6(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x01DC42u;goto L_01DC42;}
    c->pc=0x01DC34u;goto L_01DC34;
  }
L_01DC34:; L_01DC42:; *budget=left;return steps;}
static unsigned fused7(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x020DB0u;goto L_020DB0;}
    c->pc=0x020DA2u;goto L_020DA2;
  }
L_020DA2:; L_020DB0:; *budget=left;return steps;}
static unsigned fused8(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x0216B0u;goto L_0216B0;}
    c->pc=0x0216A2u;goto L_0216A2;
  }
L_0216A2:; L_0216B0:; *budget=left;return steps;}
static unsigned fused9(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x023936u;goto L_023936;}
    c->pc=0x023928u;goto L_023928;
  }
L_023928:; L_023936:; *budget=left;return steps;}
static unsigned fused10(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x0243E6u;goto L_0243E6;}
    c->pc=0x0243D8u;goto L_0243D8;
  }
L_0243D8:; L_0243E6:; *budget=left;return steps;}
static unsigned fused11(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x024BEAu;goto L_024BEA;}
    c->pc=0x024BDCu;goto L_024BDC;
  }
L_024BDC:; L_024BEA:; *budget=left;return steps;}
static unsigned fused12(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x02AFEAu;goto L_02AFEA;}
    c->pc=0x02AFDCu;goto L_02AFDC;
  }
L_02AFDC:; L_02AFEA:; *budget=left;return steps;}
static unsigned fused13(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[3],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x02C078u;goto L_02C078;}
    c->pc=0x02C06Au;goto L_02C06A;
  }
L_02C06A:; L_02C078:; *budget=left;return steps;}
static unsigned fused14(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[2],value=c->d[4]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(4);}
    c->a[2]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x02EA0Au;goto L_02EA0A;}
    c->pc=0x02EA00u;goto L_02EA00;
  }
L_02EA00:; L_02EA0A:; *budget=left;return steps;}
static unsigned fused15(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[2],value=c->d[4]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(4);}
    c->a[2]=dbase;
    c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x02F590u;goto L_02F590;}
    c->pc=0x02F586u;goto L_02F586;
  }
L_02F586:; L_02F590:; *budget=left;return steps;}
static unsigned fused16(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(4);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x03055Au;goto L_03055A;}
    c->pc=0x030550u;goto L_030550;
  }
L_030550:; L_03055A:; *budget=left;return steps;}
static unsigned fused17(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[0],value=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),value);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x036CB4u;goto L_036CB4;}
    c->pc=0x036CA6u;goto L_036CA6;
  }
L_036CA6:; L_036CB4:; *budget=left;return steps;}
static unsigned fused18(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[1],value=0u;uint32_t saddr=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){value=md2gba_read16(b,saddr);saddr+=2u;md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(28);}
    c->a[1]=dbase;c->a[0]=saddr;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x038C5Au;goto L_038C5A;}
    c->pc=0x038C50u;goto L_038C50;
  }
L_038C50:; L_038C5A:; *budget=left;return steps;}
static unsigned fused19(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[6]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x0451E2u;goto L_0451E2;}
    c->pc=0x0451D8u;goto L_0451D8;
  }
L_0451D8:; L_0451E2:; *budget=left;return steps;}
static unsigned fused20(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[6]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x0451F6u;goto L_0451F6;}
    c->pc=0x0451ECu;goto L_0451EC;
  }
L_0451EC:; L_0451F6:; *budget=left;return steps;}
static unsigned fused21(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x052EE4u;goto L_052EE4;}
    c->pc=0x052EDAu;goto L_052EDA;
  }
L_052EDA:; L_052EE4:; *budget=left;return steps;}
static unsigned fused22(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x054C0Cu;goto L_054C0C;}
    c->pc=0x054C02u;goto L_054C02;
  }
L_054C02:; L_054C0C:; *budget=left;return steps;}
static unsigned fused23(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x054C30u;goto L_054C30;}
    c->pc=0x054C26u;goto L_054C26;
  }
L_054C26:; L_054C30:; *budget=left;return steps;}
static unsigned fused24(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[0],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x054C46u;goto L_054C46;}
    c->pc=0x054C3Cu;goto L_054C3C;
  }
L_054C3C:; L_054C46:; *budget=left;return steps;}
static unsigned fused25(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x06461Cu;goto L_06461C;}
    c->pc=0x064610u;goto L_064610;
  }
L_064610:; L_06461C:; *budget=left;return steps;}
static unsigned fused26(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x064644u;goto L_064644;}
    c->pc=0x064638u;goto L_064638;
  }
L_064638:; L_064644:; *budget=left;return steps;}
static unsigned fused27(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x064664u;goto L_064664;}
    c->pc=0x064658u;goto L_064658;
  }
L_064658:; L_064664:; *budget=left;return steps;}
static unsigned fused28(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x064680u;goto L_064680;}
    c->pc=0x064674u;goto L_064674;
  }
L_064674:; L_064680:; *budget=left;return steps;}
static unsigned fused29(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x06492Cu;goto L_06492C;}
    c->pc=0x064920u;goto L_064920;
  }
L_064920:; L_06492C:; *budget=left;return steps;}
static unsigned fused30(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(36),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x06494Cu;goto L_06494C;}
    c->pc=0x064940u;goto L_064940;
  }
L_064940:; L_06494C:; *budget=left;return steps;}
static unsigned fused31(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));
    uint32_t dbase=c->a[1],value=0u;uint32_t saddr=c->a[2];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){value=md2gba_read16(b,saddr);saddr+=2u;md2gba_write16(b,dbase+(44),value);dbase=dbase+(uint32_t)(64);}
    c->a[1]=dbase;c->a[2]=saddr;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*28u;
    if(n==remaining){c->pc=0x06D566u;goto L_06D566;}
    c->pc=0x06D55Au;goto L_06D55A;
  }
L_06D55A:; L_06D566:; *budget=left;return steps;}
static unsigned fused32(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[1]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[0]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[1]=(c->d[1]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&0x80000000u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x06FDA4u;goto L_06FDA4;}
    c->pc=0x06FD98u;goto L_06FD98;
  }
L_06FD98:; L_06FDA4:; *budget=left;return steps;}
static unsigned fused33(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(52),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x070848u;goto L_070848;}
    c->pc=0x07083Cu;goto L_07083C;
  }
L_07083C:; L_070848:; *budget=left;return steps;}
static unsigned fused34(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[1]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(52),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x07095Eu;goto L_07095E;}
    c->pc=0x070952u;goto L_070952;
  }
L_070952:; L_07095E:; *budget=left;return steps;}
static unsigned fused35(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride MOVE/LEA/DBF fusion. */
  if(left>24u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=24u):((budget>>5)+(budget>>7)));
    uint32_t dbase=c->a[4],value=c->d[0]&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),value);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;
    c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    value&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(value==0u?4u:0u)|(value&32768u?8u:0u));
    steps+=n*3u;left-=n*24u;
    if(n==remaining){c->pc=0x074438u;goto L_074438;}
    c->pc=0x07442Eu;goto L_07442E;
  }
L_07442E:; L_074438:; *budget=left;return steps;}
static unsigned fused36(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[2];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(16);}
    c->a[2]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x00BC48u;goto L_00BC48;}
    c->pc=0x00BC3Eu;goto L_00BC3E;
  }
L_00BC3E:; L_00BC48:; *budget=left;return steps;}
static unsigned fused37(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[3];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(28),0u);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x00CFE6u;goto L_00CFE6;}
    c->pc=0x00CFDAu;goto L_00CFDA;
  }
L_00CFDA:; L_00CFE6:; *budget=left;return steps;}
static unsigned fused38(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x013048u;goto L_013048;}
    c->pc=0x01303Eu;goto L_01303E;
  }
L_01303E:; L_013048:; *budget=left;return steps;}
static unsigned fused39(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x01305Au;goto L_01305A;}
    c->pc=0x013050u;goto L_013050;
  }
L_013050:; L_01305A:; *budget=left;return steps;}
static unsigned fused40(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x019840u;goto L_019840;}
    c->pc=0x019836u;goto L_019836;
  }
L_019836:; L_019840:; *budget=left;return steps;}
static unsigned fused41(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x019852u;goto L_019852;}
    c->pc=0x019848u;goto L_019848;
  }
L_019848:; L_019852:; *budget=left;return steps;}
static unsigned fused42(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x01A2B0u;goto L_01A2B0;}
    c->pc=0x01A2A6u;goto L_01A2A6;
  }
L_01A2A6:; L_01A2B0:; *budget=left;return steps;}
static unsigned fused43(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x01A2C2u;goto L_01A2C2;}
    c->pc=0x01A2B8u;goto L_01A2B8;
  }
L_01A2B8:; L_01A2C2:; *budget=left;return steps;}
static unsigned fused44(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[3];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(28),0u);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x01CE1Cu;goto L_01CE1C;}
    c->pc=0x01CE10u;goto L_01CE10;
  }
L_01CE10:; L_01CE1C:; *budget=left;return steps;}
static unsigned fused45(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x022482u;goto L_022482;}
    c->pc=0x022478u;goto L_022478;
  }
L_022478:; L_022482:; *budget=left;return steps;}
static unsigned fused46(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x023CBAu;goto L_023CBA;}
    c->pc=0x023CB0u;goto L_023CB0;
  }
L_023CB0:; L_023CBA:; *budget=left;return steps;}
static unsigned fused47(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x023CCCu;goto L_023CCC;}
    c->pc=0x023CC2u;goto L_023CC2;
  }
L_023CC2:; L_023CCC:; *budget=left;return steps;}
static unsigned fused48(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[2];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(28),0u);dbase=dbase+(uint32_t)(64);}
    c->a[2]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x028BC4u;goto L_028BC4;}
    c->pc=0x028BB8u;goto L_028BB8;
  }
L_028BB8:; L_028BC4:; *budget=left;return steps;}
static unsigned fused49(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(16);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x02DB7Au;goto L_02DB7A;}
    c->pc=0x02DB70u;goto L_02DB70;
  }
L_02DB70:; L_02DB7A:; *budget=left;return steps;}
static unsigned fused50(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x02EA30u;goto L_02EA30;}
    c->pc=0x02EA26u;goto L_02EA26;
  }
L_02EA26:; L_02EA30:; *budget=left;return steps;}
static unsigned fused51(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[3];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(28),0u);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x02F332u;goto L_02F332;}
    c->pc=0x02F326u;goto L_02F326;
  }
L_02F326:; L_02F332:; *budget=left;return steps;}
static unsigned fused52(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(32);}
    c->a[0]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x02F5B6u;goto L_02F5B6;}
    c->pc=0x02F5ACu;goto L_02F5AC;
  }
L_02F5AC:; L_02F5B6:; *budget=left;return steps;}
static unsigned fused53(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[1];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(16);}
    c->a[1]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x033576u;goto L_033576;}
    c->pc=0x03356Cu;goto L_03356C;
  }
L_03356C:; L_033576:; *budget=left;return steps;}
static unsigned fused54(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x052110u;goto L_052110;}
    c->pc=0x052106u;goto L_052106;
  }
L_052106:; L_052110:; *budget=left;return steps;}
static unsigned fused55(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x052492u;goto L_052492;}
    c->pc=0x052488u;goto L_052488;
  }
L_052488:; L_052492:; *budget=left;return steps;}
static unsigned fused56(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x0524A2u;goto L_0524A2;}
    c->pc=0x052498u;goto L_052498;
  }
L_052498:; L_0524A2:; *budget=left;return steps;}
static unsigned fused57(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x052A94u;goto L_052A94;}
    c->pc=0x052A8Au;goto L_052A8A;
  }
L_052A8A:; L_052A94:; *budget=left;return steps;}
static unsigned fused58(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: generic fixed-stride CLR/LEA/DBF fusion. */
  if(left>30u){
    unsigned remaining=(c->d[0]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=30u):(budget>>5));uint32_t dbase=c->a[0];
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(0),0u);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[0]=(c->d[0]&0xffff0000u)|((remaining-1u-n)&65535u);
    c->sr=(uint16_t)((c->sr&~15u)|4u);steps+=n*3u;left-=n*30u;
    if(n==remaining){c->pc=0x06ED86u;goto L_06ED86;}
    c->pc=0x06ED7Cu;goto L_06ED7C;
  }
L_06ED7C:; L_06ED86:; *budget=left;return steps;}
static unsigned fused59(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x015FFCu;goto L_015FFC;}
    c->pc=0x015FE8u;goto L_015FE8;
  }
L_015FE8:; L_015FFC:; *budget=left;return steps;}
static unsigned fused60(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x016168u;goto L_016168;}
    c->pc=0x016154u;goto L_016154;
  }
L_016154:; L_016168:; *budget=left;return steps;}
static unsigned fused61(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x016A62u;goto L_016A62;}
    c->pc=0x016A4Eu;goto L_016A4E;
  }
L_016A4E:; L_016A62:; *budget=left;return steps;}
static unsigned fused62(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x016BECu;goto L_016BEC;}
    c->pc=0x016BD8u;goto L_016BD8;
  }
L_016BD8:; L_016BEC:; *budget=left;return steps;}
static unsigned fused63(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x0181B6u;goto L_0181B6;}
    c->pc=0x0181A2u;goto L_0181A2;
  }
L_0181A2:; L_0181B6:; *budget=left;return steps;}
static unsigned fused64(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x0182BCu;goto L_0182BC;}
    c->pc=0x0182A8u;goto L_0182A8;
  }
L_0182A8:; L_0182BC:; *budget=left;return steps;}
static unsigned fused65(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=58u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x018EFCu;goto L_018EFC;}
    c->pc=0x018EE8u;goto L_018EE8;
  }
L_018EE8:; L_018EFC:; *budget=left;return steps;}
static unsigned fused66(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x019B94u;goto L_019B94;}
    c->pc=0x019B80u;goto L_019B80;
  }
L_019B80:; L_019B94:; *budget=left;return steps;}
static unsigned fused67(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=38u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x01BC34u;goto L_01BC34;}
    c->pc=0x01BC20u;goto L_01BC20;
  }
L_01BC20:; L_01BC34:; *budget=left;return steps;}
static unsigned fused68(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=12u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x020D5Au;goto L_020D5A;}
    c->pc=0x020D46u;goto L_020D46;
  }
L_020D46:; L_020D5A:; *budget=left;return steps;}
static unsigned fused69(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[6]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[3],v0=5u&65535u,v1=38u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[3]=dbase;c->d[6]=(c->d[6]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x02125Cu;goto L_02125C;}
    c->pc=0x021248u;goto L_021248;
  }
L_021248:; L_02125C:; *budget=left;return steps;}
static unsigned fused70(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>36u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<64u)?(budget>=36u):((budget>>6)+(budget>>7)));uint32_t dbase=c->a[0],v0=5u&65535u,v1=15u&65535u;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write16(b,dbase+(2),v0);md2gba_write16(b,dbase+(28),v1);dbase=dbase+(uint32_t)(64);}
    c->a[0]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=65535u;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&32768u?8u:0u));steps+=n*4u;left-=n*36u;
    if(n==remaining){c->pc=0x036C78u;goto L_036C78;}
    c->pc=0x036C64u;goto L_036C64;
  }
L_036C64:; L_036C78:; *budget=left;return steps;}
static unsigned fused71(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));uint32_t dbase=c->a[4],v0=c->d[0]&0xffffffffu,v1=c->d[0]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),v0);md2gba_write32(b,dbase+(36),v1);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&0x80000000u?8u:0u));steps+=n*4u;left-=n*28u;
    if(n==remaining){c->pc=0x06CE78u;goto L_06CE78;}
    c->pc=0x06CE68u;goto L_06CE68;
  }
L_06CE68:; L_06CE78:; *budget=left;return steps;}
static unsigned fused72(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));uint32_t dbase=c->a[4],v0=c->d[0]&0xffffffffu,v1=c->d[0]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),v0);md2gba_write32(b,dbase+(36),v1);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&0x80000000u?8u:0u));steps+=n*4u;left-=n*28u;
    if(n==remaining){c->pc=0x06CFC0u;goto L_06CFC0;}
    c->pc=0x06CFB0u;goto L_06CFB0;
  }
L_06CFB0:; L_06CFC0:; *budget=left;return steps;}
static unsigned fused73(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));uint32_t dbase=c->a[4],v0=c->d[0]&0xffffffffu,v1=c->d[0]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),v0);md2gba_write32(b,dbase+(36),v1);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&0x80000000u?8u:0u));steps+=n*4u;left-=n*28u;
    if(n==remaining){c->pc=0x06D108u;goto L_06D108;}
    c->pc=0x06D0F8u;goto L_06D0F8;
  }
L_06D0F8:; L_06D108:; *budget=left;return steps;}
static unsigned fused74(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));uint32_t dbase=c->a[4],v0=c->d[0]&0xffffffffu,v1=c->d[0]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),v0);md2gba_write32(b,dbase+(36),v1);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&0x80000000u?8u:0u));steps+=n*4u;left-=n*28u;
    if(n==remaining){c->pc=0x06D250u;goto L_06D250;}
    c->pc=0x06D240u;goto L_06D240;
  }
L_06D240:; L_06D250:; *budget=left;return steps;}
static unsigned fused75(Md2Gba68k*c,Md2GbaBus*b,unsigned*budget){unsigned steps=0,left=*budget;
  /* P86/P87: ordered two-field record update fusion. */
  if(left>28u){
    unsigned remaining=(c->d[7]&65535u)+1u,budget=left-1u,n=((budget<32u)?(budget>=28u):((budget>>5)+(budget>>8)));uint32_t dbase=c->a[4],v0=c->d[0]&0xffffffffu,v1=c->d[1]&0xffffffffu;
    if(n>remaining)n=remaining;
    for(unsigned i=0u;i<n;i++){md2gba_write32(b,dbase+(32),v0);md2gba_write32(b,dbase+(36),v1);dbase=dbase+(uint32_t)(64);}
    c->a[4]=dbase;c->d[7]=(c->d[7]&0xffff0000u)|((remaining-1u-n)&65535u);
    v1&=0xffffffffu;c->sr=(uint16_t)((c->sr&~15u)|(v1==0u?4u:0u)|(v1&0x80000000u?8u:0u));steps+=n*4u;left-=n*28u;
    if(n==remaining){c->pc=0x07637Eu;goto L_07637E;}
    c->pc=0x07636Eu;goto L_07636E;
  }
L_07636E:; L_07637E:; *budget=left;return steps;}
static unsigned (*funcs[])(Md2Gba68k*,Md2GbaBus*,unsigned*)={fused0,fused1,fused2,fused3,fused4,fused5,fused6,fused7,fused8,fused9,fused10,fused11,fused12,fused13,fused14,fused15,fused16,fused17,fused18,fused19,fused20,fused21,fused22,fused23,fused24,fused25,fused26,fused27,fused28,fused29,fused30,fused31,fused32,fused33,fused34,fused35,fused36,fused37,fused38,fused39,fused40,fused41,fused42,fused43,fused44,fused45,fused46,fused47,fused48,fused49,fused50,fused51,fused52,fused53,fused54,fused55,fused56,fused57,fused58,fused59,fused60,fused61,fused62,fused63,fused64,fused65,fused66,fused67,fused68,fused69,fused70,fused71,fused72,fused73,fused74,fused75};
static const uint32_t starts[]={0x535au,0x160acu,0x16b2cu,0x1bd2au,0x1c94au,0x1cb40u,0x1dc34u,0x20da2u,0x216a2u,0x23928u,0x243d8u,0x24bdcu,0x2afdcu,0x2c06au,0x2ea00u,0x2f586u,0x30550u,0x36ca6u,0x38c50u,0x451d8u,0x451ecu,0x52edau,0x54c02u,0x54c26u,0x54c3cu,0x64610u,0x64638u,0x64658u,0x64674u,0x64920u,0x64940u,0x6d55au,0x6fd98u,0x7083cu,0x70952u,0x7442eu,0xbc3eu,0xcfdau,0x1303eu,0x13050u,0x19836u,0x19848u,0x1a2a6u,0x1a2b8u,0x1ce10u,0x22478u,0x23cb0u,0x23cc2u,0x28bb8u,0x2db70u,0x2ea26u,0x2f326u,0x2f5acu,0x3356cu,0x52106u,0x52488u,0x52498u,0x52a8au,0x6ed7cu,0x15fe8u,0x16154u,0x16a4eu,0x16bd8u,0x181a2u,0x182a8u,0x18ee8u,0x19b80u,0x1bc20u,0x20d46u,0x21248u,0x36c64u,0x6ce68u,0x6cfb0u,0x6d0f8u,0x6d240u,0x7636eu};
static const uint8_t lens[]={3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4};
static const uint8_t dstreg[]={0,3,3,3,3,2,3,3,3,3,3,3,3,3,2,2,0,0,1,0,0,4,0,0,0,4,4,4,4,4,4,1,4,4,4,4,2,3,0,0,0,0,0,0,3,0,0,0,2,0,0,3,0,1,0,0,0,0,0,3,3,3,3,3,3,3,3,3,3,3,0,4,4,4,4,4};
static const uint8_t srcreg[]={255,255,255,255,255,1,255,255,255,255,255,255,255,255,255,255,255,255,0,255,255,255,255,255,255,255,255,255,255,255,255,2,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
static const uint8_t countreg[]={7,6,6,6,6,7,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,1,0,0,7,6,0,7,7,7,7,7,7,6,7,7,7,7,7,6,6,6,6,0,0,0,0,0,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7};

static void init_bus(Md2GbaBus*b){md2gba_bus_init(b,rom,0x300000u);for(unsigned i=0;i<65536;i++)b->wram[i]=(uint8_t)rnd();for(unsigned i=0;i<65536;i++)b->vdp.vram[i]=(uint8_t)rnd();}
int main(void){
 rom=(uint8_t*)malloc(0x300000u);FILE*f=fopen("inputs/ps4built.bin","rb");if(!rom||!f||fread(rom,1,0x300000u,f)!=0x300000u)return 3;fclose(f);
 unsigned cases=0;
 for(unsigned k=0;k<sizeof(starts)/sizeof(starts[0]);k++)for(unsigned t=0;t<40;t++){
   Md2Gba68k a={0},r;init_bus(&busa);for(unsigned j=0;j<8;j++){a.d[j]=rnd();a.a[j]=rnd();}
   unsigned dr=dstreg[k],sr=srcreg[k],cr=countreg[k];
   /* Exercise ordinary WRAM, wrap-near-WRAM, and VDP-ish addresses while keeping
      the structure itself title-independent. */
   static const uint32_t da[]={0xff1000u,0xff7f20u,0xffffc0u,0xc00000u};
   if(dr<8)a.a[dr]=(rnd()&0xFF000000u)|da[t&3u];
   if(sr<8)a.a[sr]=(rnd()&0xFF000000u)|((t&1u)?0xff4000u:0x000400u);
   a.d[cr]=(a.d[cr]&0xffff0000u)|(rnd()%33u);a.pc=starts[k];a.sr=(uint16_t)rnd();
   r=a;busb=busa;unsigned left=1u+rnd()%1600u,orig=left,cy=0u,steps=funcs[k](&a,&busa,&left);
   for(unsigned j=0;j<steps;j++){uint32_t pc=r.pc;if(md68k_interp_step(&r,&busb)!=1){printf("interp fail k=%u pc=%x\n",k,pc);return 4;}cy+=md68k_estimate_cycles(&busb,pc,r.pc);}
   if(memcmp(&a,&r,sizeof(a))||memcmp(&busa,&busb,sizeof(busa))||cy!=orig-left){
     printf("FAIL k=%u t=%u steps=%u cyc=%u/%u pc=%x/%x sr=%x/%x cpu=%d bus=%d\n",k,t,steps,cy,orig-left,a.pc,r.pc,a.sr,r.sr,memcmp(&a,&r,sizeof(a)),memcmp(&busa,&busb,sizeof(busa)));
     for(unsigned z=0;z<8;z++)if(a.d[z]!=r.d[z]||a.a[z]!=r.a[z])printf(" r%u d=%x/%x a=%x/%x\n",z,a.d[z],r.d[z],a.a[z],r.a[z]);
     for(unsigned z=0;z<65536;z++)if(busa.wram[z]!=busb.wram[z]){printf(" wram diff %x %x/%x\n",z,busa.wram[z],busb.wram[z]);break;}
     return 1;}cases++;
 }
 printf("PASS P87 restored strided fusions: %u differential cases across %u structural sites\n",cases,(unsigned)(sizeof(starts)/sizeof(starts[0])));return 0;
}
