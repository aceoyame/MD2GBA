#include <stddef.h>
#include <stdint.h>
void md2gba_ps4_m4a_reset(void){}
int md2gba_ps4_m4a_play(uint8_t id){(void)id;return 1;}
void md2gba_ps4_m4a_stop(void){}
uint8_t md2gba_ps4_m4a_music_id(void){return 0;}
int md2gba_ps4_m4a_sfx_play(uint8_t id){(void)id;return 1;}
void md2gba_ps4_m4a_sfx_stop_normal(void){}
void md2gba_ps4_m4a_sfx_stop_special(void){}
void md2gba_ps4_m4a_sfx_stop_all(void){}
void md2gba_ps4_m4a_mix_mono(void *user,int8_t *dst,size_t samples){(void)user;for(size_t i=0;i<samples;i++)dst[i]=0;}
