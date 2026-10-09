#include <cstdint>
#include <cstdio>
#include "ps4gba/chip_audio.h"

int main(){
    int failures=0; unsigned min_peak=255,max_peak=0; unsigned long long total_energy=0;
    ps4gba::chip_audio::set_fm_backend(ps4gba::chip_audio::fm_backend::multisample);
    for(unsigned id=0x81; id<=0xB4; ++id){
        ps4gba::chip_audio::stop();
        if(!ps4gba::chip_audio::play((unsigned char)id,224,true)){
            std::printf("FAIL play %02X\n",id); failures++; continue;
        }
        int8_t buf[224]; unsigned peak=0; unsigned long long energy=0;
        for(unsigned f=0;f<600;f++){
            ps4gba::chip_audio::mix_psc_mono(buf,224);
            for(unsigned i=0;i<224;i++){
                int v=buf[i]; unsigned a=(unsigned)(v<0?-v:v);
                if(a>peak)peak=a; energy+=a;
            }
        }
        if(peak==0 || energy==0){std::printf("FAIL silent %02X\n",id); failures++;}
        if(peak<min_peak)min_peak=peak; if(peak>max_peak)max_peak=peak; total_energy+=energy;
    }
    std::printf("P93 catalog: failures=%d peak_range=%u..%u total_energy=%llu\n",failures,min_peak,max_peak,total_energy);
    return failures?1:0;
}
