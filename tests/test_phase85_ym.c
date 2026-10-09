#include "md2gba_ym2612_pcm.h"
#include <stdio.h>
#include <string.h>
void oracle_set(const void*);int oracle_equal(const void*);void oracle_render(int8_t*,size_t,unsigned);
static unsigned seed=0x71581u;static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){Md2GbaYm2612Pcm y;int8_t a[513],b[513];unsigned gains[]={0,1,18,63,127,234,255};
 for(unsigned t=0;t<4096;t++){
  md2gba_ym2612_pcm_init(&y,0,16384);y.pan_mask=(uint8_t)(1+rnd()%63);y.dac_enable=t%9==0;y.dac_data=rnd();
  for(unsigned c=0;c<6;c++){Md2GbaYmChannel*ch=&y.ch[c];ch->algorithm=(t+c)&7;ch->feedback=(t>>3)&7;ch->fb_prev[0]=(int)(rnd()%255)-127;ch->fb_prev[1]=(int)(rnd()%255)-127;
   for(unsigned o=0;o<4;o++){Md2GbaYmOperator*op=&ch->op[o];op->phase=rnd();op->phase_inc=rnd()>>4;op->env=rnd()&1023;op->gain=gains[rnd()%7];op->key_on=1;op->env_state=MD2GBA_YM_ENV_SUSTAIN;
    if(t&1){op->env_state=(rnd()>>16)%5;op->d2r=(rnd()>>16)&31;}if(t%5==0){op->env_state=MD2GBA_YM_ENV_OFF;op->key_on=1;}if(t%3==0 && c&1){op->key_on=0;op->env_state=MD2GBA_YM_ENV_OFF;}
    op->rs_ar=rnd();op->am_d1r=rnd();op->sl_rr=rnd();op->tl=rnd()&127;}
   if(t%4==0){ch->feedback=0;ch->op[2].phase_inc=ch->op[0].phase_inc;ch->op[3].phase_inc=ch->op[1].phase_inc;}if(t%8==0){ch->op[2].phase=ch->op[0].phase;ch->op[3].phase=ch->op[1].phase;}}
  oracle_set(&y);size_t n=1+rnd()%513;unsigned add=t&1;for(size_t i=0;i<n;i++)a[i]=b[i]=(int8_t)rnd();oracle_render(b,n,add);
  if(add)md2gba_ym2612_pcm_mix_into(&y,a,n);else md2gba_ym2612_pcm_render(&y,a,n);
  if(memcmp(a,b,n)||!oracle_equal(&y)||y.rendered_samples!=n){printf("FAIL YM %u state=%d count=%u/%zu\n",t,oracle_equal(&y),y.rendered_samples,n);return 1;}}
 printf("PASS 4096 YM vectors: all algorithms, feedback, envelope/key states, DAC, gain 255, clipping and chunk boundaries; state=%zu bytes\n",sizeof(y));return 0;}
