#include <stdio.h>
#include <stdint.h>
#include "md2gba_psc_m4a.h"
int main(void){
  const uint8_t counter[]={7,6,5,4,3,2,1,7};
  const unsigned want[]={1,2,3,4,5,6,0,1};
  for(unsigned i=0;i<8;i++){
    unsigned got=md2gba_psc_m4a_mix_slot(counter[i]);
    printf("c=%u slot=%u\n",counter[i],got);
    if(got!=want[i]) return 1;
  }
  return 0;
}
