#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../md2gba_projected_retry.h"

int main(void){
    Md2GbaProjectedRetry s;uint8_t reg[17]={0};memset(&s,0,sizeof(s));
    if(!md2gba_projected_retry_ready(&s,reg,1u,1u))return 1;
    md2gba_projected_retry_result(&s,-6,reg,1u,1u);
    if(md2gba_projected_retry_ready(&s,reg,7u,1000u))return 2;
    reg[7]++;if(md2gba_projected_retry_ready(&s,reg,7u,1000u))return 3;
    reg[12]++;if(!md2gba_projected_retry_ready(&s,reg,7u,1000u))return 4;
    md2gba_projected_retry_result(&s,-2,reg,7u,1000u);
    if(s.next_guest_vblank!=1004u||md2gba_projected_retry_ready(&s,reg,7u,2000u)||
       md2gba_projected_retry_ready(&s,reg,8u,1003u)||!md2gba_projected_retry_ready(&s,reg,8u,1004u))return 5;
    md2gba_projected_retry_result(&s,-3,reg,8u,1004u);
    if(s.next_guest_vblank!=1012u||md2gba_projected_retry_ready(&s,reg,9u,1011u)||
       !md2gba_projected_retry_ready(&s,reg,9u,1012u))return 6;
    reg[2]++;if(!md2gba_projected_retry_ready(&s,reg,9u,1012u))return 7;
    md2gba_projected_retry_result(&s,0,reg,9u,1012u);
    if(s.blocked||s.failures||!md2gba_projected_retry_ready(&s,reg,9u,1012u))return 8;
    /* The unsigned VBlank counter may wrap after a very long running game. */
    md2gba_projected_retry_result(&s,-5,reg,9u,0xfffffffdu);
    if(md2gba_projected_retry_ready(&s,reg,10u,0u)||!md2gba_projected_retry_ready(&s,reg,10u,1u))return 9;
    puts("PASS capacity recovery is source-driven and bounded; geometry recovery is register-driven");
    return 0;
}
