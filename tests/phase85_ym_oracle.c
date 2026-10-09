#define md2gba_ym2612_pcm_init ref_init
#define md2gba_ym2612_pcm_write ref_write
#define md2gba_ym2612_pcm_status ref_status
#define md2gba_ym2612_pcm_render ref_render
#define md2gba_ym2612_pcm_mix_into ref_mix
#define md2gba_ym2612_pcm_sink ref_sink
#include "reference/ym84_reference.c"
#include <string.h>
static Md2GbaYm2612Pcm oracle;
void oracle_set(const void*s){ref_init(&oracle,0,16384);memcpy(&oracle,s,offsetof(Md2GbaYm2612Pcm,wave_cache));}
int oracle_equal(const void*s){return !memcmp(&oracle,s,offsetof(Md2GbaYm2612Pcm,wave_cache));}
void oracle_render(int8_t*dst,size_t n,unsigned add){for(size_t i=0;i<n;i++){int v=0;for(unsigned c=0;c<6;c++)if(oracle.pan_mask&(1u<<c))v+=(c==5&&oracle.dac_enable)?(int)oracle.dac_data-128:render_channel(&oracle.ch[c]);v>>=1;if(v>127)v=127;else if(v<-128)v=-128;if(add){v+=dst[i];if(v>127)v=127;else if(v<-128)v=-128;}dst[i]=(int8_t)v;}}
