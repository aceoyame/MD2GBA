#include <stdint.h>
#include <stdio.h>
#include "md2gba_ym2612_pcm.h"
static void wr(Md2GbaYm2612Pcm*y,unsigned ch,uint8_t reg,uint8_t v){uint8_t port=(uint8_t)(ch>=3u);unsigned c=ch%3u;md2gba_ym2612_pcm_write(y,port,(uint8_t)(reg+c),v);}
static void key(Md2GbaYm2612Pcm*y,unsigned ch,int on){static const uint8_t code[6]={0,1,2,4,5,6};md2gba_ym2612_pcm_write(y,0,0x28,(uint8_t)((on?0xF0:0)|code[ch]));}
int main(void){Md2GbaYm2612Pcm y;int8_t b[257];unsigned ch,op,r;uint32_t h=2166136261u;
 if(sizeof(y)!=680u){printf("FAIL YM state size %u\n",(unsigned)sizeof(y));return 1;}
 md2gba_ym2612_pcm_init(&y,MD2GBA_YM2612_NTSC_CLOCK,16384u);
 for(ch=0;ch<6;ch++){wr(&y,ch,0xB0,(uint8_t)(((ch+2)&7)|(((ch%4)&7)<<3)));wr(&y,ch,0xB4,0xC0);wr(&y,ch,0xA0,(uint8_t)(0x40+ch*17));wr(&y,ch,0xA4,(uint8_t)(0x22+(ch<<3)));
  for(op=0;op<4;op++){static const uint8_t oo[4]={0,4,8,12};uint8_t o=oo[op];wr(&y,ch,(uint8_t)(0x30+o),(uint8_t)(1+((ch+op)%7)));wr(&y,ch,(uint8_t)(0x40+o),(uint8_t)(4+ch*3+op*7));wr(&y,ch,(uint8_t)(0x50+o),(uint8_t)(0x1f-(op&3)));wr(&y,ch,(uint8_t)(0x60+o),(uint8_t)(0x10+ch+op));wr(&y,ch,(uint8_t)(0x70+o),(uint8_t)(0x08+op));wr(&y,ch,(uint8_t)(0x80+o),(uint8_t)(0x20+(op<<4)|5));}key(&y,ch,1);}
 for(r=0;r<80;r++){if((r%7)==0){ch=(r/7)%6;wr(&y,ch,0x40,(uint8_t)((r*3)&0x7f));}if((r%11)==0){ch=(r/11)%6;wr(&y,ch,0xA0,(uint8_t)(0x30+r));}if(r==43)key(&y,2,0);if(r==51)key(&y,2,1);md2gba_ym2612_pcm_render(&y,b,sizeof(b));for(unsigned i=0;i<sizeof(b);i++){h^=(uint8_t)b[i];h*=16777619u;}}
 if(h!=0xd40446a5u){printf("FAIL YM PCM hash %08X\n",h);return 1;}if(y.register_writes!=196u||y.rendered_samples!=20560u){printf("FAIL counters %u %u\n",(unsigned)y.register_writes,(unsigned)y.rendered_samples);return 1;}
 puts("PASS P85 YM preserves P84/P83 PCM vector d40446a5; state=680 bytes");return 0;}
