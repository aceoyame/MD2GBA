#include <cstdint>
#include <cstdio>
#include "ps4gba/chip_audio.h"
int main(){
    constexpr std::uint64_t expected_hash=0xa648da82c6e16521ULL;
    std::uint64_t hash=1469598103934665603ULL;
    unsigned failures=0,min_peak=255,max_peak=0;
    ps4gba::chip_audio::set_fm_backend(ps4gba::chip_audio::fm_backend::multisample);
    for(unsigned id=0x81;id<=0xB4;++id){
        ps4gba::chip_audio::stop();
        if(!ps4gba::chip_audio::play(static_cast<std::uint8_t>(id),224,true)){++failures;continue;}
        std::int8_t buf[224]; unsigned peak=0; std::uint64_t energy=0;
        for(unsigned f=0;f<600;++f){
            ps4gba::chip_audio::mix_psc_mono(buf,224);
            for(unsigned i=0;i<224;++i){
                const auto u=static_cast<std::uint8_t>(buf[i]); hash^=u; hash*=1099511628211ULL;
                const int v=buf[i]; const unsigned a=static_cast<unsigned>(v<0?-v:v);
                if(a>peak)peak=a; energy+=a;
            }
        }
        if(!peak||!energy)++failures;
        if(peak<min_peak)min_peak=peak;if(peak>max_peak)max_peak=peak;
    }
    if(hash!=expected_hash){std::printf("P134T/P104 catalog hash mismatch: %016llx != %016llx\\n",(unsigned long long)hash,(unsigned long long)expected_hash);++failures;}
    std::printf("P134T PSC104 catalog: failures=%u peak=%u..%u fnv64=%016llx\\n",failures,min_peak,max_peak,(unsigned long long)hash);
    return failures?1:0;
}
