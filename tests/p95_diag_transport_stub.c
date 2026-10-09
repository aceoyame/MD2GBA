#include "md2gba_psc_m4a.h"
static Md2GbaPscM4a *active;
unsigned md2gba_psc_m4a_mix_slot(uint8_t c){(void)c;return 0;}
void md2gba_psc_m4a_init(Md2GbaPscM4a*s,int8_t*p){if(s){s->pcm_buffer=p;s->started=0;}active=s;}
void md2gba_psc_m4a_start(Md2GbaPscM4a*s){if(s)s->started=1;active=s;}
void md2gba_psc_m4a_stop(Md2GbaPscM4a*s){if(s)s->started=0;if(active==s)active=0;}
void md2gba_psc_m4a_vsync(void){}
unsigned md2gba_psc_m4a_service(Md2GbaPscM4a*s,Md2GbaPscM4aFillFn fill,void*user){(void)s;(void)fill;(void)user;return 0;}
Md2GbaPscM4a*md2gba_psc_m4a_active(void){return active;}
