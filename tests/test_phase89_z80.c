#include <stdio.h>
#include <string.h>
#include "md2gba_z80.h"
#include "md2gba_audio.h"
static unsigned ymw,psgw; static unsigned lastp,lastr,lastv;
static void yw(void*u,uint8_t p,uint8_t r,uint8_t v){(void)u;ymw++;lastp=p;lastr=r;lastv=v;}
static unsigned ys(void*u){(void)u;return 0;}
static void pw(void*u,uint8_t v){(void)u;(void)v;psgw++;}
int main(void){
  Md2GbaBus b; Md2GbaZ80 z; unsigned char rom[0x10000]; Md2GbaAudioSink s;
  memset(rom,0,sizeof rom); md2gba_bus_init(&b,rom,sizeof rom);
  memset(&s,0,sizeof s);s.ym2612_write=yw;s.ym2612_status=(uint8_t(*)(void*))ys;s.psg_write=pw;md2gba_bus_set_audio_sink(&b,&s);
  /* LD A,$2A; LD ($4000),A; LD A,$91; LD ($4001),A; LD A,$9F; LD ($7F11),A; HALT */
  { const unsigned char p[]={0x3E,0x2A,0x32,0x00,0x40,0x3E,0x91,0x32,0x01,0x40,0x3E,0x9F,0x32,0x11,0x7F,0x76}; memcpy(b.z80_ram,p,sizeof p); }
  b.z80_reset=0x100u;b.z80_bus_request=0;md2gba_z80_init(&z);md2gba_z80_run(&z,&b,200u);
  if(z.unsupported||ymw!=1u||psgw!=1u||lastp!=0u||lastr!=0x2Au||lastv!=0x91u){printf("FAIL io u=%u ym=%u psg=%u p=%u r=%02x v=%02x\n",(unsigned)z.unsupported,ymw,psgw,lastp,lastr,lastv);return 1;}
  /* Serial bank writes are generic Genesis behavior. */
  b.z80_ram[0]=0x3E;b.z80_ram[1]=1;b.z80_ram[2]=0x32;b.z80_ram[3]=0;b.z80_ram[4]=0x60;b.z80_ram[5]=0x76;z.pc=0;z.halted=0;z.bank=0;md2gba_z80_run(&z,&b,100u);
  if(z.bank!=0x100u){printf("FAIL bank %03x\n",z.bank);return 2;}
  puts("PASS phase89 z80 Genesis I/O");return 0;
}
