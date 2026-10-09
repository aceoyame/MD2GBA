#include <cstdint>
#include <cstdio>
#include "ps4gba/chip_audio.h"
int main(){
    constexpr std::uint64_t expected=0x056868d8f2e6eddeULL;
    std::uint64_t h=1469598103934665603ULL;
    std::uint64_t count=0;
    ps4gba::chip_audio::set_fm_backend(ps4gba::chip_audio::fm_backend::multisample);
    for(unsigned id=0x81;id<=0xB4;++id){
        ps4gba::chip_audio::stop();
        if(!ps4gba::chip_audio::play((unsigned char)id,224,true)) return 2;
        std::int8_t b[224];
        for(unsigned f=0;f<600;++f){
            ps4gba::chip_audio::mix_psc_mono(b,224);
            for(unsigned i=0;i<224;++i){ h^=(unsigned char)b[i]; h*=1099511628211ULL; ++count; }
        }
    }
    std::printf("P98 PCM FNV64=%016llx bytes=%llu\n",(unsigned long long)h,(unsigned long long)count);
    return h==expected && count==6988800ULL ? 0 : 1;
}
