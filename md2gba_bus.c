#include "md2gba_bus.h"
#if defined(MD2GBA_GBA_IWRAM)
#define MD2GBA_COLD __attribute__((section(".rom.cold")))
#define MD2GBA_HOT __attribute__((section(".iwram.text"),noinline))
#else
#define MD2GBA_COLD
#define MD2GBA_HOT __attribute__((noinline))
#endif

static inline uint32_t md_addr(uint32_t a) { return a & 0x00FFFFFFu; }
static uint8_t rom8(Md2GbaBus *b, uint32_t a) { return a < b->rom_size ? b->rom[a] : 0xFF; }
static uint32_t be32p(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int sram_index(Md2GbaBus*b,uint32_t a,uint32_t*idx){
 if(!b->sram_present || !(b->sram_control&1u) || a<b->sram_start || a>b->sram_end)return 0;
 if(b->sram_lane_stride==2u && ((a^b->sram_start)&1u))return 0;
 *idx=b->sram_lane_stride==2u?((a-b->sram_start)>>1):(a-b->sram_start);
 return *idx<b->sram_size;
}

uint16_t md2gba_vdp_status(const Md2GbaBus *b) {
    /* Bit 3 is vertical blank; bit 1 is DMA busy on the Mega Drive VDP. */
    return (uint16_t)((b->vblank_active ? 0x0008u : 0u) | (b->hblank_active ? 0x0004u : 0u) | (b->vdp.dma_busy ? 0x0002u : 0u));
}


/* P86 generic batch helpers.  These deliberately refuse any span that could
   wrap the 24-bit bus, touch cartridge SRAM overlay, or reach hardware.  The
   caller then executes the original ordered bus helpers.  For accepted spans,
   guest bytes already have identical physical order in ROM/WRAM, so a raw
   native copy avoids repeated address decode and endian swaps. */
static inline int p86_span24(uint32_t address,unsigned count,unsigned width,unsigned inc,uint32_t *start,uint32_t *end){
    uint32_t a=address&0x00FFFFFFu;uint64_t span;
    if(!count || !width)return 0;
    span=(uint64_t)(count-1u)*(uint64_t)inc+(uint64_t)width;
    if(!span || span-1u>0x00FFFFFFu-a)return 0;
    *start=a;*end=a+(uint32_t)span-1u;return 1;
}

#if defined(MD2GBA_PSC_REVERSED_WRAM)
/* OPT-B: resolve a validated span to descending native storage once. Native
   width loads preserve guest endian order. Unsupported alignment stays on the
   original path; every admitted element is read before its destination write,
   retaining forward-guest overlap propagation (not memcpy/memmove semantics). */
static const uint8_t *stream_reversed_ptr(const Md2GbaBus*b,uint32_t a,uint32_t end,unsigned width){
    const uint8_t*p;
    if(a>=0xFF0000u && end<=0xFFFFFFu && md2gba_wram_unshadowed(b,a,end))
        p=b->wram+(0x10000u-width-(a&0xFFFFu));
    else if(end<b->rom_size && b->rom_reversed && md2gba_cart_unshadowed(b,a,end))
        p=b->rom_reversed+(b->rom_size-width-a);
    else return 0;
    return ((uintptr_t)p&(width-1u))?0:p;
}
#endif

MD2GBA_COLD int md2gba_bus_copy_stream_fast(Md2GbaBus*b,uint32_t src,uint32_t dst,unsigned count,unsigned width,unsigned src_inc,unsigned dst_inc,uint32_t *last_value){
    uint32_t sa,se,da,de,last=0u;const uint8_t *sp;uint8_t *dp;unsigned i;
    if(!last_value || (width!=1u&&width!=2u&&width!=4u))return 0;
    if(!p86_span24(src,count,width,src_inc,&sa,&se) || !p86_span24(dst,count,width,dst_inc,&da,&de))return 0;
    if(da<0xFF0000u || de>0xFFFFFFu || !md2gba_wram_unshadowed(b,da,de))return 0;
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if(width>1u && ((sa|da|src_inc|dst_inc)&1u))return 0;
    if(!((sa|da|src_inc|dst_inc)&(width-1u))){
        sp=stream_reversed_ptr(b,sa,se,width);
        if(sp){
            dp=b->wram+(0x10000u-width-(da&0xFFFFu));
            if(width==1u){for(i=0;i<count;i++){last=sp[-(int32_t)(i*src_inc)];dp[-(int32_t)(i*dst_inc)]=(uint8_t)last;}}
            else if(width==2u){for(i=0;i<count;i++){last=*(const uint16_t*)(const void*)(sp-i*src_inc);*(uint16_t*)(void*)(dp-i*dst_inc)=(uint16_t)last;}}
            else {for(i=0;i<count;i++){last=*(const uint32_t*)(const void*)(sp-i*src_inc);*(uint32_t*)(void*)(dp-i*dst_inc)=last;}}
            *last_value=last;return 1;
        }
    }
    /* Descending WRAM destroys forward-host contiguity but not the fusion.
       Keep the entire admitted guest loop here and use the inlined PSC-style
       wide accessors so read->write ordering and overlap semantics remain exact. */
    if(!((sa>=0xFF0000u&&se<=0xFFFFFFu&&md2gba_wram_unshadowed(b,sa,se)) ||
         (se<b->rom_size&&md2gba_cart_unshadowed(b,sa,se))))return 0;
    for(i=0;i<count;i++){uint32_t ss=(src+i*src_inc)&0xFFFFFFu,dd=(dst+i*dst_inc)&0xFFFFFFu;
        if(width==1u)last=md2gba_read8_fast(b,ss);
        else if(width==2u)last=md2gba_read16(b,ss);
        else last=md2gba_read32(b,ss);
        if(width==1u)md2gba_wram_write8_offset(b,dd&0xFFFFu,(uint8_t)last);
        else if(width==2u)md2gba_wram_write16_offset(b,dd&0xFFFFu,(uint16_t)last);
        else md2gba_wram_write32_offset(b,dd&0xFFFFu,last);
    }
    *last_value=last;return 1;
#endif
    dp=b->wram+(da&0xFFFFu);
    if(sa>=0xFF0000u && se<=0xFFFFFFu && md2gba_wram_unshadowed(b,sa,se))sp=b->wram+(sa&0xFFFFu);
    else if(se<b->rom_size && md2gba_cart_unshadowed(b,sa,se))sp=b->rom+sa;
    else return 0;
    if(width==1u){
        uint8_t raw=0u;for(i=0;i<count;i++){raw=sp[i*src_inc];dp[i*dst_inc]=raw;}last=raw;
    }else if(width==2u && !((sa|da|src_inc|dst_inc)&1u)){
        uint16_t raw=0u;for(i=0;i<count;i++){raw=*(const uint16_t*)(const void*)(sp+i*src_inc);*(uint16_t*)(void*)(dp+i*dst_inc)=raw;}last=md2gba_bswap16(raw);
    }else if(width==4u && !((sa|da|src_inc|dst_inc)&3u)){
        uint32_t raw=0u;for(i=0;i<count;i++){raw=*(const uint32_t*)(const void*)(sp+i*src_inc);*(uint32_t*)(void*)(dp+i*dst_inc)=raw;}last=md2gba_bswap32(raw);
    }else if(width==4u && !((sa|da|src_inc|dst_inc)&1u)){
        uint16_t r0=0u,r1=0u;for(i=0;i<count;i++){const uint8_t*sq=sp+i*src_inc;uint8_t*dq=dp+i*dst_inc;r0=*(const uint16_t*)(const void*)sq;r1=*(const uint16_t*)(const void*)(sq+2u);*(uint16_t*)(void*)dq=r0;*(uint16_t*)(void*)(dq+2u)=r1;}last=((uint32_t)md2gba_bswap16(r0)<<16)|md2gba_bswap16(r1);
    }else if(width==2u){
        uint8_t hi=0u,lo=0u;for(i=0;i<count;i++){const uint8_t*sq=sp+i*src_inc;uint8_t*dq=dp+i*dst_inc;hi=sq[0];lo=sq[1];dq[0]=hi;dq[1]=lo;}last=((uint32_t)hi<<8)|lo;
    }else{
        uint8_t q0=0u,q1=0u,q2=0u,q3=0u;for(i=0;i<count;i++){const uint8_t*sq=sp+i*src_inc;uint8_t*dq=dp+i*dst_inc;q0=sq[0];q1=sq[1];q2=sq[2];q3=sq[3];dq[0]=q0;dq[1]=q1;dq[2]=q2;dq[3]=q3;}last=((uint32_t)q0<<24)|((uint32_t)q1<<16)|((uint32_t)q2<<8)|q3;
    }
    *last_value=last;return 1;
}

MD2GBA_COLD int md2gba_bus_compare_stream_fast(Md2GbaBus*b,uint32_t src,uint32_t dst,unsigned count,unsigned width,unsigned src_inc,unsigned dst_inc,uint32_t *src_value,uint32_t *dst_value,unsigned *done){
    uint32_t sa,se,da,de,sv=0u,dv=0u;const uint8_t *sp,*dp;unsigned i=0u;
    if(!src_value||!dst_value||!done||(width!=1u&&width!=2u&&width!=4u))return 0;
    if(!p86_span24(src,count,width,src_inc,&sa,&se)||!p86_span24(dst,count,width,dst_inc,&da,&de))return 0;
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if(width>1u && ((sa|da|src_inc|dst_inc)&1u))return 0;
    if(!((sa|da|src_inc|dst_inc)&(width-1u))){
        sp=stream_reversed_ptr(b,sa,se,width);dp=stream_reversed_ptr(b,da,de,width);
        if(sp&&dp){
            if(width==1u){for(i=0u;i<count;i++){sv=sp[-(int32_t)(i*src_inc)];dv=dp[-(int32_t)(i*dst_inc)];if(sv!=dv){i++;break;}}}
            else if(width==2u){for(i=0u;i<count;i++){sv=*(const uint16_t*)(const void*)(sp-i*src_inc);dv=*(const uint16_t*)(const void*)(dp-i*dst_inc);if(sv!=dv){i++;break;}}}
            else {for(i=0u;i<count;i++){sv=*(const uint32_t*)(const void*)(sp-i*src_inc);dv=*(const uint32_t*)(const void*)(dp-i*dst_inc);if(sv!=dv){i++;break;}}}
            *src_value=sv;*dst_value=dv;*done=i;return 1;
        }
    }
    if(!((sa>=0xFF0000u&&se<=0xFFFFFFu&&md2gba_wram_unshadowed(b,sa,se)) || (se<b->rom_size&&md2gba_cart_unshadowed(b,sa,se))))return 0;
    if(!((da>=0xFF0000u&&de<=0xFFFFFFu&&md2gba_wram_unshadowed(b,da,de)) || (de<b->rom_size&&md2gba_cart_unshadowed(b,da,de))))return 0;
    for(i=0u;i<count;i++){uint32_t ss=(src+i*src_inc)&0xFFFFFFu,dd=(dst+i*dst_inc)&0xFFFFFFu;
        if(width==1u){sv=md2gba_read8_fast(b,ss);dv=md2gba_read8_fast(b,dd);}
        else if(width==2u){sv=md2gba_read16(b,ss);dv=md2gba_read16(b,dd);}
        else {sv=md2gba_read32(b,ss);dv=md2gba_read32(b,dd);}
        if(sv!=dv){i++;break;}
    }
    *src_value=sv;*dst_value=dv;*done=i;return 1;
#endif
    if(sa>=0xFF0000u&&se<=0xFFFFFFu&&md2gba_wram_unshadowed(b,sa,se))sp=b->wram+(sa&0xFFFFu);
    else if(se<b->rom_size&&md2gba_cart_unshadowed(b,sa,se))sp=b->rom+sa;else return 0;
    if(da>=0xFF0000u&&de<=0xFFFFFFu&&md2gba_wram_unshadowed(b,da,de))dp=b->wram+(da&0xFFFFu);
    else if(de<b->rom_size&&md2gba_cart_unshadowed(b,da,de))dp=b->rom+da;else return 0;
    if(width==1u){for(i=0u;i<count;i++){sv=sp[i*src_inc];dv=dp[i*dst_inc];if(sv!=dv){i++;break;}}}
    else if(width==2u&&!((sa|da|src_inc|dst_inc)&1u)){uint16_t sr=0u,dr=0u;for(i=0u;i<count;i++){sr=*(const uint16_t*)(const void*)(sp+i*src_inc);dr=*(const uint16_t*)(const void*)(dp+i*dst_inc);if(sr!=dr){i++;break;}}sv=md2gba_bswap16(sr);dv=md2gba_bswap16(dr);}
    else if(width==4u&&!((sa|da|src_inc|dst_inc)&3u)){uint32_t sr=0u,dr=0u;for(i=0u;i<count;i++){sr=*(const uint32_t*)(const void*)(sp+i*src_inc);dr=*(const uint32_t*)(const void*)(dp+i*dst_inc);if(sr!=dr){i++;break;}}sv=md2gba_bswap32(sr);dv=md2gba_bswap32(dr);}
    else if(width==4u&&!((sa|da|src_inc|dst_inc)&1u)){uint16_t s0=0u,s1=0u,d0=0u,d1=0u;for(i=0u;i<count;i++){const uint8_t*sq=sp+i*src_inc;const uint8_t*dq=dp+i*dst_inc;s0=*(const uint16_t*)(const void*)sq;s1=*(const uint16_t*)(const void*)(sq+2u);d0=*(const uint16_t*)(const void*)dq;d1=*(const uint16_t*)(const void*)(dq+2u);if(s0!=d0||s1!=d1){i++;break;}}sv=((uint32_t)md2gba_bswap16(s0)<<16)|md2gba_bswap16(s1);dv=((uint32_t)md2gba_bswap16(d0)<<16)|md2gba_bswap16(d1);}
    else if(width==2u){uint8_t s0=0u,s1=0u,d0=0u,d1=0u;for(i=0u;i<count;i++){const uint8_t*sq=sp+i*src_inc;const uint8_t*dq=dp+i*dst_inc;s0=sq[0];s1=sq[1];d0=dq[0];d1=dq[1];if(s0!=d0||s1!=d1){i++;break;}}sv=((uint32_t)s0<<8)|s1;dv=((uint32_t)d0<<8)|d1;}
    else{uint8_t s0=0u,s1=0u,s2=0u,s3=0u,d0=0u,d1=0u,d2=0u,d3=0u;for(i=0u;i<count;i++){const uint8_t*sq=sp+i*src_inc;const uint8_t*dq=dp+i*dst_inc;s0=sq[0];s1=sq[1];s2=sq[2];s3=sq[3];d0=dq[0];d1=dq[1];d2=dq[2];d3=dq[3];if(s0!=d0||s1!=d1||s2!=d2||s3!=d3){i++;break;}}sv=((uint32_t)s0<<24)|((uint32_t)s1<<16)|((uint32_t)s2<<8)|s3;dv=((uint32_t)d0<<24)|((uint32_t)d1<<16)|((uint32_t)d2<<8)|d3;}
    *src_value=sv;*dst_value=dv;*done=i;return 1;
}

MD2GBA_COLD int md2gba_bus_fill_stream_fast(Md2GbaBus*b,uint32_t dst,unsigned count,unsigned width,unsigned dst_inc,uint32_t value){
    uint32_t da,de;uint8_t *dp;unsigned i;
    if((width!=1u&&width!=2u&&width!=4u) || !p86_span24(dst,count,width,dst_inc,&da,&de))return 0;
    if(da<0xFF0000u || de>0xFFFFFFu || !md2gba_wram_unshadowed(b,da,de))return 0;
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if(width>1u && ((da|dst_inc)&1u))return 0;
    if(!((da|dst_inc)&(width-1u))){
        dp=b->wram+(0x10000u-width-(da&0xFFFFu));
        if(width==1u){for(i=0;i<count;i++)dp[-(int32_t)(i*dst_inc)]=(uint8_t)value;}
        else if(width==2u){for(i=0;i<count;i++)*(uint16_t*)(void*)(dp-i*dst_inc)=(uint16_t)value;}
        else {for(i=0;i<count;i++)*(uint32_t*)(void*)(dp-i*dst_inc)=value;}
        return 1;
    }
    for(i=0;i<count;i++){unsigned o=(unsigned)((dst+i*dst_inc)&0xFFFFu);
        if(width==1u)md2gba_wram_write8_offset(b,o,(uint8_t)value);
        else if(width==2u)md2gba_wram_write16_offset(b,o,(uint16_t)value);
        else md2gba_wram_write32_offset(b,o,value);
    }
    return 1;
#endif
    dp=b->wram+(da&0xFFFFu);
    if(width==1u){uint8_t q=(uint8_t)value;for(i=0;i<count;i++)dp[i*dst_inc]=q;}
    else if(width==2u && !((da|dst_inc)&1u)){uint16_t q=md2gba_bswap16((uint16_t)value);for(i=0;i<count;i++)*(uint16_t*)(void*)(dp+i*dst_inc)=q;}
    else if(width==4u && !((da|dst_inc)&3u)){uint32_t q=md2gba_bswap32(value);for(i=0;i<count;i++)*(uint32_t*)(void*)(dp+i*dst_inc)=q;}
    else if(width==4u && !((da|dst_inc)&1u)){uint16_t q0=md2gba_bswap16((uint16_t)(value>>16)),q1=md2gba_bswap16((uint16_t)value);for(i=0;i<count;i++){uint8_t*dq=dp+i*dst_inc;*(uint16_t*)(void*)dq=q0;*(uint16_t*)(void*)(dq+2u)=q1;}}
    else if(width==2u){uint8_t hi=(uint8_t)(value>>8),lo=(uint8_t)value;for(i=0;i<count;i++){uint8_t*dq=dp+i*dst_inc;dq[0]=hi;dq[1]=lo;}}
    else{uint8_t q0=(uint8_t)(value>>24),q1=(uint8_t)(value>>16),q2=(uint8_t)(value>>8),q3=(uint8_t)value;for(i=0;i<count;i++){uint8_t*dq=dp+i*dst_inc;dq[0]=q0;dq[1]=q1;dq[2]=q2;dq[3]=q3;}}
    return 1;
}
void md2gba_bus_set_vblank(Md2GbaBus *b, int active) { b->vblank_active = active ? 1u : 0u; }
MD2GBA_COLD void md2gba_bus_set_audio_sink(Md2GbaBus *b, const Md2GbaAudioSink *sink) { md2gba_audio_set_sink(&b->audio,sink); }
MD2GBA_COLD void md2gba_bus_set_z80_release_sync(Md2GbaBus *b, Md2GbaZ80SyncFn fn, void *user) { if(!b)return; b->z80_release_sync=fn; b->z80_release_user=user; }
MD2GBA_COLD void md2gba_bus_set_pad(Md2GbaBus *b, unsigned port, uint16_t held) { if(port<2u)b->pad_held[port]=held; }
MD2GBA_COLD void md2gba_bus_set_pad_poll(Md2GbaBus *b, Md2GbaPadPollFn fn, void *user) { b->pad_poll=fn; b->pad_poll_user=user; }
uint16_t md2gba_bus_get_pad(const Md2GbaBus *b, unsigned port) { return port<2u?b->pad_held[port]:0u; }

static uint8_t pad_read(Md2GbaBus *b,unsigned port){
    uint16_t h; if(b->pad_poll){b->pad_held[port]=b->pad_poll(b->pad_poll_user,port);}
    h=b->pad_held[port]; uint8_t latch=port?b->pad2_data:b->pad1_data;
    unsigned th=!!(latch&0x40u); uint8_t r=0x80u | (th?0x40u:0u);
    /* Standard 3-button Mega Drive pad. Inputs are active-low. */
    if(!(h&MD2GBA_PAD_UP)) r|=0x01u;
    if(!(h&MD2GBA_PAD_DOWN)) r|=0x02u;
    if(th){
        if(!(h&MD2GBA_PAD_LEFT)) r|=0x04u;
        if(!(h&MD2GBA_PAD_RIGHT)) r|=0x08u;
        if(!(h&MD2GBA_PAD_B)) r|=0x10u;
        if(!(h&MD2GBA_PAD_C)) r|=0x20u;
    }else{
        /* bits 2/3 are forced low while TH is low. */
        if(!(h&MD2GBA_PAD_A)) r|=0x10u;
        if(!(h&MD2GBA_PAD_START)) r|=0x20u;
    }
    return r;
}

void md2gba_vdp_data16_direct_vdp(Md2GbaVdp *v, uint16_t value);
static void vdp_bitset_range(uint32_t*bits,unsigned first,unsigned last){
    unsigned fw=first>>5,lw=last>>5;
    uint32_t fm=0xFFFFFFFFu<<(first&31u),lm=0xFFFFFFFFu>>(31u-(last&31u));
    if(fw==lw){bits[fw]|=fm&lm;return;}
    bits[fw]|=fm;
    while(++fw<lw)bits[fw]=0xFFFFFFFFu;
    bits[lw]|=lm;
}
static int vdp_bitset_range_any(const uint32_t*bits,unsigned first,unsigned last){
    unsigned fw=first>>5,lw=last>>5;
    uint32_t fm=0xFFFFFFFFu<<(first&31u),lm=0xFFFFFFFFu>>(31u-(last&31u));
    if(fw==lw)return !!(bits[fw]&(fm&lm));
    if(bits[fw]&fm)return 1;
    while(++fw<lw)if(bits[fw])return 1;
    return !!(bits[lw]&lm);
}
static void vdp_mark_vram(Md2GbaVdp*v,uint16_t a){unsigned pg=a>>8,t=a>>5;v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);v->vram_epoch++;}
static void vdp_mark_vram_span_noepoch(Md2GbaVdp*v,uint16_t start,unsigned len){
    unsigned done=0u;
    while(done<len){
        uint16_t a=(uint16_t)(start+done);
        unsigned room=0x10000u-(unsigned)a;
        unsigned chunk=(len-done<room)?(len-done):room;
        unsigned last=(unsigned)a+chunk-1u;
        vdp_bitset_range(v->vram_dirty_pages,((unsigned)a)>>8,last>>8);
        vdp_bitset_range(v->vram_dirty_tiles,((unsigned)a)>>5,last>>5);
        done+=chunk;
    }
}
static void vdp_mark_vram_span(Md2GbaVdp*v,uint16_t start,unsigned len){if(!len)return;vdp_mark_vram_span_noepoch(v,start,len);v->vram_epoch++;}

/* P88 / PSC-style GBA-facing scroll shadow.  PSC keeps translated video state
   alongside Genesis-visible VDP state instead of rediscovering it at every
   presentation pass.  Keep only the cheap whole-plane form here; line/cell
   modes still use the existing staged HBlank stream. */
static void vdp_refresh_present_scroll(Md2GbaVdp*v){
    unsigned mode=v->regs[11]&3u,base;
    int valid=((mode&2u)==0u)&&((v->regs[11]&4u)==0u);
    int16_t ha=0,hb=0,va=0,vb=0;
    if(valid){
        base=((unsigned)v->regs[13]&0x3fu)<<10;
        ha=(int16_t)(((uint16_t)v->vram[(uint16_t)base]<<8)|v->vram[(uint16_t)(base+1u)]);
        hb=(int16_t)(((uint16_t)v->vram[(uint16_t)(base+2u)]<<8)|v->vram[(uint16_t)(base+3u)]);
        va=(int16_t)v->vsram[0];vb=(int16_t)v->vsram[1];
    }
    if(v->present_scroll_valid!=(uint8_t)valid || v->present_scroll_mode!=(uint8_t)mode ||
       (valid && (v->present_hscroll[0]!=ha||v->present_hscroll[1]!=hb||
                  v->present_vscroll[0]!=va||v->present_vscroll[1]!=vb))){
        v->present_scroll_valid=(uint8_t)valid;v->present_scroll_mode=(uint8_t)mode;
        v->present_hscroll[0]=ha;v->present_hscroll[1]=hb;
        v->present_vscroll[0]=va;v->present_vscroll[1]=vb;
        v->present_scroll_epoch++;
    }
}

/* P124: VRAM changes can affect the cached whole-plane H-scroll shadow only
   when they touch the four live table bytes.  Avoid rereading/comparing that
   table after unrelated pattern/name-table uploads.  Register 11/13 writes and
   VSRAM writes still refresh through their canonical paths. */
MD2GBA_HOT static int vdp_vram_span_touches_present_scroll(const Md2GbaVdp*v,uint16_t start,unsigned len){
    uint32_t base,end;
    if(!len)return 0;
    if(((v->regs[11]&3u)&2u)!=0u || (v->regs[11]&4u)!=0u)return 0;
    base=((uint32_t)v->regs[13]&0x3Fu)<<10;
    if(len>=0x10000u)return 1;
    end=(uint32_t)start+(uint32_t)len;
    if(end<=0x10000u)return (uint32_t)start<base+4u && end>base;
    return ((uint32_t)start<base+4u) || ((end-0x10000u)>base);
}

/* P76: common Genesis memory->VRAM DMA is overwhelmingly sequential.  The
   old generic path called md2gba_read16()+vdp_data16() for every word, which
   made first-use graphics/text loads scale with thousands of C bus calls and
   incremented renderer epochs for every changed byte.  Preserve the exact
   byte image but copy the ordinary ROM/WRAM + autoincrement=2 case in bulk. */
static int vdp_dma_source_ptr(Md2GbaBus*b,uint32_t src,unsigned bytes,const uint8_t**out){
    src&=0xFFFFFFu;
    if(bytes==0u)return 0;
    if(src+bytes<=b->rom_size && md2gba_cart_unshadowed(b,src,src+bytes-1u)){*out=b->rom+src;return 1;}
    #if !defined(MD2GBA_PSC_REVERSED_WRAM)
    if(src>=0xFF0000u && src+bytes<=0x1000000u && md2gba_wram_unshadowed(b,src,src+bytes-1u)){*out=b->wram+(src&0xFFFFu);return 1;}
#endif
    return 0;
}
#if defined(MD2GBA_PSC_REVERSED_WRAM)
/* OPT-B: exact canonical memory-DMA semantics for descending WRAM storage.
   Resolve the source once and perform ordered native halfword reads. This is
   deliberately separate from the older ROM-DMA bulk path, whose epoch/trace
   accounting differs from the canonical word path being replaced here. */
MD2GBA_HOT static int vdp_dma_reversed_wram_vram(Md2GbaBus*b,uint32_t src,unsigned words){
    Md2GbaVdp*v=&b->vdp;uint16_t start=v->address;unsigned i=0u,changed_count=0u;int scroll_span;
#if !defined(MD2GBA_RELEASE)
    unsigned trace_n;
#endif
    const uint8_t*sp;
    if(!words||words>0x8000u||src<0xFF0000u||src>0xFFFFFEu||(src&1u)||
       words*2u>0x1000000u-src || words*2u>0x10000u-(unsigned)start ||
       (start&1u)||v->dma_fill_pending||v->target!=MD2GBA_VDP_VRAM ||
       (v->regs[15]?v->regs[15]:2u)!=2u ||
       !md2gba_wram_unshadowed(b,src,src+words*2u-1u))return 0;
    sp=b->wram+(0xFFFEu-(src&0xFFFFu));
    /* P137: classify scroll-shadow overlap once for the complete synchronous
       transfer. Most reversed-WRAM DMA targets pattern/name/SAT data and can
       skip the repeated range arithmetic in every changed-word iteration. */
    scroll_span=vdp_vram_span_touches_present_scroll(v,start,words*2u);
#if !defined(MD2GBA_RELEASE)
    trace_n=v->trace_count<32u?32u-v->trace_count:0u;
    if(trace_n>words)trace_n=words;
    for(i=0;i<trace_n;i++){
        unsigned ti=v->trace_count++;
        v->trace_type[ti]=2u;v->trace_value[ti]=*(const uint16_t*)(const void*)(sp-i*2u);
        v->trace_addr[ti]=(uint16_t)(start+i*2u);v->trace_code[ti]=v->code;
        v->trace_target[ti]=(uint8_t)v->target;v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;
    }
    i=0u;
#endif
    /* P134Z: keep the common unchanged compare register-sized.  The P134V
       eight-word temporary made Clang spill the converted halfwords heavily on
       ARM7TDMI.  Two guest words fit naturally in registers: compare first,
       then perform the canonical per-changed-word dirty/epoch bookkeeping. */
    while(i+2u<=words){
        uint16_t a=(uint16_t)(start+i*2u);
        uint16_t n0=md2gba_bswap16(*(const uint16_t*)(const void*)(sp-i*2u));
        uint16_t n1=md2gba_bswap16(*(const uint16_t*)(const void*)(sp-(i+1u)*2u));
        uint16_t*dp=(uint16_t*)(void*)(v->vram+a);
        if(dp[0]!=n0){unsigned pg=a>>8,t=a>>5;dp[0]=n0;changed_count++;v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);if(scroll_span&&vdp_vram_span_touches_present_scroll(v,a,2u))vdp_refresh_present_scroll(v);}
        if(dp[1]!=n1){uint16_t a1=(uint16_t)(a+2u);unsigned pg=a1>>8,t=a1>>5;dp[1]=n1;changed_count++;v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);if(scroll_span&&vdp_vram_span_touches_present_scroll(v,a1,2u))vdp_refresh_present_scroll(v);}
        i+=2u;
    }
    for(;i<words;i++){
        uint16_t a=(uint16_t)(start+i*2u),value=*(const uint16_t*)(const void*)(sp-i*2u);
        uint16_t nv=md2gba_bswap16(value),*dp=(uint16_t*)(void*)(v->vram+a);
        if(*dp!=nv){
            unsigned pg=a>>8,t=a>>5;*dp=nv;changed_count++;
            v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);
            v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);
            if(scroll_span&&vdp_vram_span_touches_present_scroll(v,a,2u))vdp_refresh_present_scroll(v);
        }
    }
    v->vram_epoch+=changed_count;
    v->data_word=*(const uint16_t*)(const void*)(sp-(words-1u)*2u);
#if !defined(MD2GBA_RELEASE)
    v->data_writes+=words;
#endif
    v->address=(uint16_t)(start+words*2u);
    return 1;
}
#endif

/* P129 recovery: compare the common unchanged prefix in eight-word (16-byte)
   groups before entering P128's copy/change-detection loop.  Dense transfers
   pay one bounded 16-byte comparison; repeated DMA of already-resident assets
   can skip most or all destination writes.  Return values are always 16-byte
   aligned so the existing aligned 32-bit loop remains unchanged. */
static unsigned vdp_dma_same_prefix16(const uint8_t*dst,const uint8_t*src,unsigned bytes){
    typedef uint32_t AliasWord __attribute__((may_alias));
    unsigned i=0u;
    if((((uintptr_t)dst|(uintptr_t)src)&3u)!=0u)return 0u;
    while(i+16u<=bytes){
        const AliasWord*d=(const AliasWord*)(const void*)(dst+i);
        const AliasWord*s=(const AliasWord*)(const void*)(src+i);
        if((d[0]^s[0])|(d[1]^s[1])|(d[2]^s[2])|(d[3]^s[3]))break;
        i+=16u;
    }
    return i;
}

static int vdp_dma_bulk_vram(Md2GbaBus*b,uint32_t src,unsigned words){
    Md2GbaVdp*v=&b->vdp; const uint8_t*p; unsigned bytes=words*2u,i=0; uint16_t dst=v->address; uint32_t diff=0u;
#if defined(MD2GBA_PSC_REVERSED_WRAM)
    if(src>=0xFF0000u && vdp_dma_reversed_wram_vram(b,src,words))return 1;
#endif
    if((v->regs[15]?v->regs[15]:2u)!=2u || !vdp_dma_source_ptr(b,src,bytes,&p))return 0;
    if((unsigned)dst+bytes<=0x10000u && ((((uintptr_t)(v->vram+dst))|(uintptr_t)p)&3u)==0u){
        i=vdp_dma_same_prefix16(v->vram+dst,p,bytes);
        for(;i+16u<=bytes;i+=16u){
            uint32_t*n=(uint32_t*)(void*)(v->vram+dst+i); const uint32_t*q=(const uint32_t*)(const void*)(p+i);
            uint32_t q0=q[0],q1=q[1],q2=q[2],q3=q[3];
            diff|=n[0]^q0;diff|=n[1]^q1;diff|=n[2]^q2;diff|=n[3]^q3;
            n[0]=q0;n[1]=q1;n[2]=q2;n[3]=q3;
        }
        for(;i+4u<=bytes;i+=4u){uint32_t*n=(uint32_t*)(void*)(v->vram+dst+i),q=*(const uint32_t*)(const void*)(p+i);diff|=*n^q;*n=q;}
        for(;i<bytes;i++){uint8_t q=p[i];diff|=(uint32_t)(v->vram[dst+i]^q);v->vram[dst+i]=q;}
    }else{
        for(i=0;i<bytes;i++){uint16_t a=(uint16_t)(dst+i);uint8_t q=p[i];diff|=(uint32_t)(v->vram[a]^q);v->vram[a]=q;}
    }
    if(diff){vdp_mark_vram_span(v,dst,bytes);if(vdp_vram_span_touches_present_scroll(v,dst,bytes))vdp_refresh_present_scroll(v);}
    v->address=(uint16_t)(dst+bytes); return 1;
}
void md2gba_vdp_clear_dirty(Md2GbaVdp*v){unsigned i;for(i=0;i<8u;i++)v->vram_dirty_pages[i]=0;for(i=0;i<64u;i++)v->vram_dirty_tiles[i]=0;v->cram_dirty_mask[0]=v->cram_dirty_mask[1]=0;v->vsram_dirty_mask[0]=v->vsram_dirty_mask[1]=0;v->regs_dirty_mask=0;}
int md2gba_vdp_vram_range_dirty(const Md2GbaVdp*v,uint16_t start,unsigned len){unsigned done=0u;if(!len)return 0;while(done<len){uint16_t a=(uint16_t)(start+done);unsigned room=0x10000u-(unsigned)a,chunk=(len-done<room)?(len-done):room,last=(unsigned)a+chunk-1u;/* P105: renderer ranges (name tables, SAT, H-scroll) need only the existing 32-byte VRAM block precision. The older 256-byte page query caused unrelated pattern writes on the same page to spuriously rebuild sprites/scroll. */if(vdp_bitset_range_any(v->vram_dirty_tiles,((unsigned)a)>>5,last>>5))return 1;done+=chunk;}return 0;}

static void vdp_set_command(Md2GbaBus *b, uint32_t cmd) {
    Md2GbaVdp *v=&b->vdp;
    /* Genesis VDP command decoding: address bits 0-13 are in bits 16-29,
       address bits 14-15 are in command bits 0-1. Code bits are split. */
    v->control_long = cmd;
    v->address = (uint16_t)(((cmd >> 16) & 0x3FFFu) | ((cmd & 3u) << 14));
    v->code = (uint8_t)(((cmd >> 30) & 3u) | ((cmd >> 2) & 0x3Cu));
    /* The VDP code selects both transfer direction and memory space.  PSC's
       shared PSII/PSIII compatibility helper preserves the same targets for
       reads and writes: code 0/1 = VRAM, 8/3 = CRAM, 4/5 = VSRAM.  Keeping
       one target enum lets the data-port implementation share address and
       autoincrement semantics while the code bit pattern still distinguishes
       read commands from writes/DMA. */
    switch(v->code & 0x0F) {
        case 0x00: case 0x01: v->target = MD2GBA_VDP_VRAM; break;
        case 0x03: case 0x08: v->target = MD2GBA_VDP_CRAM; break;
        case 0x04: case 0x05: v->target = MD2GBA_VDP_VSRAM; break;
        default: v->target = MD2GBA_VDP_NONE; break;
    }

    /* Generic 68K->VDP DMA. Register 23 bit 7 distinguishes memory DMA
       from VDP fill/copy modes. The command's DMA bit is code bit 5 and
       VDP register 1 bit 4 enables DMA globally. */
    if((v->code & 0x20u) && (v->regs[1] & 0x10u)) {
        if(!(v->regs[23] & 0x80u) && v->target != MD2GBA_VDP_NONE) {
            uint32_t words=(uint32_t)v->regs[19] | ((uint32_t)v->regs[20]<<8);
            uint32_t srcw=(uint32_t)v->regs[21] | ((uint32_t)v->regs[22]<<8) | (((uint32_t)v->regs[23]&0x7Fu)<<16);
            uint32_t n=words ? words : 0x10000u, i;
#if !defined(MD2GBA_RELEASE)
            if(!v->dma_diag_valid) {
                v->dma_diag_valid=1u; v->dma_diag_target=(uint8_t)v->target; v->dma_diag_code=v->code;
                v->dma_diag_words_reg=(uint16_t)words; v->dma_diag_dst=v->address;
                v->dma_diag_src_word=srcw; v->dma_diag_src_byte=(srcw<<1)&0xFFFFFFu; v->dma_diag_cmd=cmd;
                for(i=0;i<8u;i++) v->dma_diag_source_words[i]=md2gba_read16(b,((srcw+i)<<1)&0xFFFFFFu);
            }
#endif
            v->dma_busy=1;
            if(v->target==MD2GBA_VDP_VRAM && vdp_dma_bulk_vram(b,(srcw<<1)&0xFFFFFFu,n)) {
                /* Fast path already advanced the VDP destination address and
                   applied exact dirty-range bookkeeping once. */
            } else {
                for(i=0;i<n;i++) md2gba_vdp_data16_direct_vdp(v,md2gba_read16(b,((srcw+i)<<1)&0xFFFFFFu));
            }
            srcw=(srcw+n)&0x7FFFFFu;
            v->regs[19]=v->regs[20]=0;
            v->regs[21]=(uint8_t)srcw; v->regs[22]=(uint8_t)(srcw>>8);
            v->regs[23]=(uint8_t)((v->regs[23]&0x80u)|((srcw>>16)&0x7Fu));
            v->dma_busy=0;
#if !defined(MD2GBA_RELEASE)
            v->dma_transfers++; v->dma_words+=n;
#endif
        } else {
            uint8_t mode=(uint8_t)((v->regs[23]>>6)&3u);
            uint16_t words=(uint16_t)((uint16_t)v->regs[19] | ((uint16_t)v->regs[20]<<8));
#if !defined(MD2GBA_RELEASE)
            if(!v->special_dma_diag_valid) {
                v->special_dma_diag_valid=1u; v->special_dma_diag_mode=mode;
                v->special_dma_diag_target=(uint8_t)v->target; v->special_dma_diag_code=v->code;
                v->special_dma_diag_words_reg=words; v->special_dma_diag_dst=v->address; v->special_dma_diag_cmd=cmd;
                v->special_dma_diag_src_word=(uint32_t)v->regs[21] | ((uint32_t)v->regs[22]<<8) | (((uint32_t)v->regs[23]&0x7Fu)<<16);
                v->special_dma_regs[0]=v->regs[19]; v->special_dma_regs[1]=v->regs[20];
                v->special_dma_regs[2]=v->regs[21]; v->special_dma_regs[3]=v->regs[22]; v->special_dma_regs[4]=v->regs[23];
            }
#endif
            if(mode==2u && v->target==MD2GBA_VDP_VRAM) {
                /* VRAM fill starts only after the CPU writes the fill word to the data port.
                   Keep the programmed length/destination intact until that write arrives. */
                v->dma_fill_pending=1u; v->dma_fill_words=words; v->dma_fill_dst=v->address;
#if !defined(MD2GBA_RELEASE)
                v->dma_fill_armed_count++;
#endif
            } else if(mode==3u && v->target==MD2GBA_VDP_VRAM) {
                /* VRAM copy is internal to the VDP. Length is zero=>65536 transfers;
                   source is the 16-bit byte address in regs 21/22. */
                uint32_t n=words ? words : 0x10000u, i;
                uint16_t src=(uint16_t)((uint16_t)v->regs[21] | ((uint16_t)v->regs[22]<<8));
                uint16_t inc=v->regs[15] ? v->regs[15] : 2u;
                uint16_t dst0=v->address; int changed=0;
                v->dma_busy=1u;
                for(i=0;i<n;i++) { uint8_t x=v->vram[src++]; if(v->vram[v->address]!=x){v->vram[v->address]=x;changed=1;} v->address=(uint16_t)(v->address+inc); }
                if(changed){uint32_t extent=(uint32_t)inc*(n?n-1u:0u)+1u;if(extent>0x10000u)extent=0x10000u;vdp_mark_vram_span(v,dst0,(unsigned)extent);if(vdp_vram_span_touches_present_scroll(v,dst0,(unsigned)extent))vdp_refresh_present_scroll(v);}
                v->regs[19]=v->regs[20]=0; v->regs[21]=(uint8_t)src; v->regs[22]=(uint8_t)(src>>8);
                v->dma_busy=0u;
#if !defined(MD2GBA_RELEASE)
                v->dma_transfers++; v->dma_words+=n;
#endif
            } else {
#if !defined(MD2GBA_RELEASE)
                v->unsupported_dma_modes++;
#endif
            }
        }
    }
}

void md2gba_vdp_control16_direct(Md2GbaBus *b, uint16_t value) {
    Md2GbaVdp *v=&b->vdp;
    v->control_word = value;
#if !defined(MD2GBA_RELEASE)
    v->control_writes++;
#endif
    /* A second command word has precedence over the $8xxx register-write
       encoding.  This matches the Genesis control-port state machine and the
       duplicated PSII/PSIII compatibility helpers in Phantasy Star Collection:
       once the first half of a two-word command is pending, the next word is
       command payload regardless of its top bits.  Testing $8xxx first loses
       legitimate commands whose second half happens to occupy that pattern. */
    if(v->pending_control) {
        uint32_t cmd = ((uint32_t)v->first_control_word << 16) | value;
        v->pending_control = 0;
        vdp_set_command(b, cmd);
    } else if((value & 0xE000u) == 0x8000u) {
        unsigned reg = (value >> 8) & 0x1Fu;
        if(reg < sizeof(v->regs)) {uint8_t nv=(uint8_t)value;if(v->regs[reg]!=nv){v->regs[reg]=nv;v->regs_dirty_mask|=1u<<reg;v->regs_epoch++;if(reg==11u||reg==13u)vdp_refresh_present_scroll(v);}}
#if !defined(MD2GBA_RELEASE)
        v->register_writes++;
#endif
    } else {
        v->first_control_word = value;
        v->pending_control = 1;
    }
    /* P51: register initialization is intentionally omitted. Record command-port
       traffic after state transition so the second half shows the decoded target. */
#if !defined(MD2GBA_RELEASE)
    if(v->trace_count < 32u) { unsigned ti=v->trace_count++; v->trace_type[ti]=1u; v->trace_value[ti]=value; v->trace_addr[ti]=v->address; v->trace_code[ti]=v->code; v->trace_target[ti]=(uint8_t)v->target; v->trace_pending[ti]=v->pending_control; v->trace_pc[ti]=0xFFFFFFFFu; }
#endif
}

static inline uint16_t vdp_cram_to_gba(uint16_t c) {
    unsigned r=(c>>1)&7u,g=(c>>5)&7u,b=(c>>9)&7u;
    r=(r<<2)|(r>>1); g=(g<<2)|(g>>1); b=(b<<2)|(b>>1);
    return (uint16_t)(r|(g<<5)|(b<<10));
}

/* P108: exact bulk form of repeated 16-bit VDP-data writes.  The AOT caller
   has already proved that complete MOVE.W/DBF iterations fit before the next
   scheduler deadline.  This helper only removes host-side address decode and
   per-write function-call overhead; guest write order and virtual VDP state
   remain identical to md2gba_vdp_data16_direct_vdp(). */
MD2GBA_HOT int md2gba_vdp_data16_repeat_fast(Md2GbaBus *b,unsigned count,uint16_t value){
    Md2GbaVdp *v;uint16_t a,nv;unsigned i,changed_count=0u;
    if(!b||!count)return 0;
    v=&b->vdp;
    /* Keep all special/observable VDP modes canonical.  The common transition
       clear is sequential even-address VRAM with auto-increment 2. */
    if(v->dma_fill_pending || v->target!=MD2GBA_VDP_VRAM)return 0;
    if((v->regs[15]?v->regs[15]:2u)!=2u || (v->address&1u))return 0;
    if((unsigned)v->address + count*2u > 0x10000u)return 0;

    v->data_word=value;
    /* Canonical direct writes expose the first 32 data-port transactions in diagnostic builds. */
#if !defined(MD2GBA_RELEASE)
    for(i=0u;i<count && v->trace_count<32u;i++){
        unsigned ti=v->trace_count++;
        v->trace_type[ti]=2u;v->trace_value[ti]=value;
        v->trace_addr[ti]=(uint16_t)(v->address+i*2u);
        v->trace_code[ti]=v->code;v->trace_target[ti]=(uint8_t)v->target;
        v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;
    }
#endif
#if !defined(MD2GBA_RELEASE)
    v->data_writes+=count;
#endif
    a=v->address;nv=(uint16_t)((value>>8)|(value<<8));
    for(i=0u;i<count;i++,a=(uint16_t)(a+2u)){
        uint16_t *dp=(uint16_t*)(void*)(v->vram+a);
        if(*dp!=nv){
            unsigned pg=((unsigned)a)>>8,t=((unsigned)a)>>5;
            *dp=nv;
            v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);
            v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);
            changed_count++;
            if(vdp_vram_span_touches_present_scroll(v,a,2u))vdp_refresh_present_scroll(v);
        }
    }
    v->vram_epoch+=changed_count;
    v->address=a;
    return 1;
}

/* P122: bulk longword VDP publisher for immutable loader streams.
   P121 already proved that these output writes are not observed until the next
   guest VBlank.  Keep all special VDP states canonical and accept only the
   ordinary sequential, aligned VRAM stream.  Dirty masks, epochs, trace slots,
   data_word/data_writes and final address remain byte-for-byte equivalent to
   two md2gba_vdp_data16_direct_vdp() calls per longword. */
MD2GBA_HOT int md2gba_vdp_data32_stream_fast_ready(const Md2GbaBus *b,unsigned count){
    const Md2GbaVdp *v;uint32_t bytes,end,hs;
    if(!b||!count)return 0;v=&b->vdp;
    if(v->dma_fill_pending||v->target!=MD2GBA_VDP_VRAM)return 0;
    if((v->regs[15]?v->regs[15]:2u)!=2u||(v->address&1u))return 0;
    bytes=(uint32_t)count*4u;end=(uint32_t)v->address+bytes;
    if(end>0x10000u)return 0;
    /* Canonical writes refresh the PSC presentation scroll after each changed
       word.  A tile-data stream never needs that work; if this range overlaps
       the live H-scroll words, stay canonical so present_scroll_epoch remains
       exact even for intermediate halfword changes. */
    if(((v->regs[11]&3u)&2u)==0u && (v->regs[11]&4u)==0u){
        hs=((uint32_t)v->regs[13]&0x3Fu)<<10;
        if((uint32_t)v->address<hs+4u && end>hs)return 0;
    }
    return 1;
}

/* P123: generated Nemesis batches are always naturally longword-aligned.
   Keep this predicate separate from the P122 helper so older generic users
   retain the even-address fast path. */
MD2GBA_HOT int md2gba_vdp_data32_stream_aligned_ready(const Md2GbaBus *b,unsigned count){
    const Md2GbaVdp *v;uint32_t bytes,end,hs;
    if(!b||!count)return 0;v=&b->vdp;
    if(v->dma_fill_pending||v->target!=MD2GBA_VDP_VRAM)return 0;
    if((v->regs[15]?v->regs[15]:2u)!=2u||(v->address&3u))return 0;
    bytes=(uint32_t)count*4u;end=(uint32_t)v->address+bytes;
    if(end>0x10000u)return 0;
    if(((v->regs[11]&3u)&2u)==0u && (v->regs[11]&4u)==0u){
        hs=((uint32_t)v->regs[13]&0x3Fu)<<10;
        if((uint32_t)v->address<hs+4u && end>hs)return 0;
    }
    return 1;
}
MD2GBA_HOT int md2gba_vdp_data32_stream_fast(Md2GbaBus *b,const uint32_t *values,unsigned count){
    Md2GbaVdp *v;uint16_t a;unsigned i,changed=0u;
#if !defined(MD2GBA_RELEASE)
    unsigned trace_left;
#endif
    if(!values||!md2gba_vdp_data32_stream_fast_ready(b,count))return 0;
    v=&b->vdp;a=v->address;
    /* Preserve exactly the first 32 diagnostic data-port transactions outside release. */
#if !defined(MD2GBA_RELEASE)
    trace_left=v->trace_count<32u?32u-v->trace_count:0u;
    if(trace_left){
        unsigned ti=v->trace_count,wi=0u;
        while(wi<count && trace_left){
            uint32_t q=values[wi++];uint16_t w0=(uint16_t)(q>>16),w1=(uint16_t)q;
            v->trace_type[ti]=2u;v->trace_value[ti]=w0;v->trace_addr[ti]=(uint16_t)(a+(ti-v->trace_count)*2u);
            v->trace_code[ti]=v->code;v->trace_target[ti]=(uint8_t)v->target;v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;ti++;trace_left--;
            if(trace_left){v->trace_type[ti]=2u;v->trace_value[ti]=w1;v->trace_addr[ti]=(uint16_t)(a+(ti-v->trace_count)*2u);v->trace_code[ti]=v->code;v->trace_target[ti]=(uint8_t)v->target;v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;ti++;trace_left--;}
        }
        v->trace_count=(uint8_t)ti;
    }
#endif
#if !defined(MD2GBA_RELEASE)
    v->data_writes+=(uint32_t)count*2u;
#endif
    for(i=0u;i<count;i++,a=(uint16_t)(a+4u)){
        uint32_t q=values[i];uint16_t n0=(uint16_t)(((uint16_t)(q>>16)>>8)|((uint16_t)(q>>16)<<8));
        uint16_t n1=(uint16_t)(((uint16_t)q>>8)|((uint16_t)q<<8));
        uint16_t *dp=(uint16_t*)(void*)(v->vram+a);
        if(dp[0]!=n0){unsigned aa=(unsigned)a,pg=aa>>8,t=aa>>5;dp[0]=n0;v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);changed++;}
        if(dp[1]!=n1){unsigned aa=(unsigned)a+2u,pg=aa>>8,t=aa>>5;dp[1]=n1;v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);changed++;}
    }
    v->vram_epoch+=changed;
    v->address=a;v->data_word=(uint16_t)values[count-1u];
    return 1;
}

/* P123: no-recheck publisher for a stream whose values are already bswap32'd.
   The caller must have just accepted md2gba_vdp_data32_stream_aligned_ready().
   A single aligned native longword comparison/store replaces two halfword
   stores. Dirty page/tile ORs are coalesced to once per changed tile/page while
   vram_epoch still counts changed 16-bit writes exactly. */
MD2GBA_HOT void md2gba_vdp_data32_stream_preswapped_unchecked(Md2GbaBus *b,const uint32_t *values,unsigned count){
    Md2GbaVdp *v=&b->vdp;uint16_t a=v->address;unsigned i,changed=0u;
    unsigned last_tile=0xFFFFFFFFu,last_page=0xFFFFFFFFu;
#if !defined(MD2GBA_RELEASE)
    {unsigned trace_left=v->trace_count<32u?32u-v->trace_count:0u;
    if(trace_left){
        unsigned ti=v->trace_count,base=ti,wi=0u;
        while(wi<count && trace_left){
            uint32_t q=values[wi++];
            uint16_t w0=md2gba_bswap16((uint16_t)q),w1=md2gba_bswap16((uint16_t)(q>>16));
            v->trace_type[ti]=2u;v->trace_value[ti]=w0;v->trace_addr[ti]=(uint16_t)(a+(ti-base)*2u);
            v->trace_code[ti]=v->code;v->trace_target[ti]=(uint8_t)v->target;v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;ti++;trace_left--;
            if(trace_left){v->trace_type[ti]=2u;v->trace_value[ti]=w1;v->trace_addr[ti]=(uint16_t)(a+(ti-base)*2u);v->trace_code[ti]=v->code;v->trace_target[ti]=(uint8_t)v->target;v->trace_pending[ti]=v->pending_control;v->trace_pc[ti]=0xFFFFFFFFu;ti++;trace_left--;}
        }
        v->trace_count=(uint8_t)ti;
    }}
#endif
#if !defined(MD2GBA_RELEASE)
    v->data_writes+=(uint32_t)count*2u;
#endif
    for(i=0u;i<count;i++,a=(uint16_t)(a+4u)){
        uint32_t *dp=(uint32_t*)(void*)(v->vram+a),old=*dp,nv=values[i];
        if(old!=nv){
            unsigned aa=(unsigned)a,pg=aa>>8,t=aa>>5;
            changed+=(unsigned)((uint16_t)old!=(uint16_t)nv);
            changed+=(unsigned)((uint16_t)(old>>16)!=(uint16_t)(nv>>16));
            *dp=nv;
            if(t!=last_tile){v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);last_tile=t;}
            if(pg!=last_page){v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);last_page=pg;}
        }
    }
    v->vram_epoch+=changed;
    v->address=a;
    {uint16_t q=(uint16_t)(values[count-1u]>>16);v->data_word=md2gba_bswap16(q);}
}

MD2GBA_HOT void md2gba_vdp_data16_direct_vdp(Md2GbaVdp *v, uint16_t value) {
    v->data_word = value;
#if !defined(MD2GBA_RELEASE)
    v->data_writes++;
#endif
    if(v->dma_fill_pending) {
        /* Mega Drive VRAM fill: the trigger word is written at the current address,
           then its low byte is repeated for the programmed transfer count using
           register 15 auto-increment. This model is sufficient for the standard
           zero-fill sequence used during PSIV startup and preserves byte-lane behavior. */
        uint32_t n=v->dma_fill_words ? v->dma_fill_words : 0x10000u, i;
        uint16_t inc=v->regs[15] ? v->regs[15] : 2u;
        uint8_t fill=(uint8_t)value;
        uint16_t start=v->address;
        int changed=0;
        v->dma_fill_pending=0u; v->dma_busy=1u;
#if !defined(MD2GBA_RELEASE)
        v->dma_fill_trigger_count++; v->dma_fill_trigger_value=value; v->dma_fill_trigger_addr=start;
        for(i=0;i<8u;i++) v->dma_fill_before[i]=v->vram[(uint16_t)(start+i)];
#endif
        /* Keep the byte-accurate fill but avoid dirty bookkeeping on every byte. */
        if(v->vram[v->address]!=(uint8_t)(value>>8)){v->vram[v->address]=(uint8_t)(value>>8);changed=1;}
        v->address=(uint16_t)(v->address+inc);
        for(i=0;i<n;i++) { if(v->vram[v->address]!=fill){v->vram[v->address]=fill;changed=1;} v->address=(uint16_t)(v->address+inc); }
#if !defined(MD2GBA_RELEASE)
        for(i=0;i<8u;i++) v->dma_fill_after[i]=v->vram[(uint16_t)(start+i)];
#endif
        /* Mark only the address span this fill could have touched.  This is
           deliberately conservative for increments >1 (gaps inside the span
           are marked too) but avoids P75's unconditional whole-VRAM dirtying,
           which turned small text/asset clears into global renderer rebuilds. */
        if(changed){uint32_t extent=(uint32_t)inc*n+1u;if(extent>0x10000u)extent=0x10000u;vdp_mark_vram_span(v,start,(unsigned)extent);if(vdp_vram_span_touches_present_scroll(v,start,(unsigned)extent))vdp_refresh_present_scroll(v);}
        v->regs[19]=v->regs[20]=0; v->dma_busy=0u;
#if !defined(MD2GBA_RELEASE)
        v->dma_transfers++; v->dma_words+=n;
#endif
        return;
    }
#if !defined(MD2GBA_RELEASE)
    if(v->trace_count < 32u) { unsigned ti=v->trace_count++; v->trace_type[ti]=2u; v->trace_value[ti]=value; v->trace_addr[ti]=v->address; v->trace_code[ti]=v->code; v->trace_target[ti]=(uint8_t)v->target; v->trace_pending[ti]=v->pending_control; v->trace_pc[ti]=0xFFFFFFFFu; }
#endif
    switch(v->target) {
        case MD2GBA_VDP_VRAM: {
            uint16_t a0=v->address,a1=(uint16_t)(v->address+1u); int changed=0;
            if(!(a0&1u) && a0!=0xFFFFu){
                uint16_t nv=(uint16_t)((value>>8)|(value<<8)); uint16_t*dp=(uint16_t*)(void*)(v->vram+a0);
                if(*dp!=nv){
                    unsigned pg=((unsigned)a0)>>8,t=((unsigned)a0)>>5;
                    *dp=nv;
                    v->vram_dirty_pages[pg>>5]|=1u<<(pg&31u);
                    v->vram_dirty_tiles[t>>5]|=1u<<(t&31u);
                    changed=1;
                }
            }else{
                uint8_t hi=(uint8_t)(value>>8),lo=(uint8_t)value;
                if(v->vram[a0]!=hi){v->vram[a0]=hi;vdp_mark_vram_span_noepoch(v,a0,1u);changed=1;}
                if(v->vram[a1]!=lo){v->vram[a1]=lo;vdp_mark_vram_span_noepoch(v,a1,1u);changed=1;}
            }
            if(changed){v->vram_epoch++;if(vdp_vram_span_touches_present_scroll(v,a0,2u))vdp_refresh_present_scroll(v);} break;
        }
        case MD2GBA_VDP_CRAM: {
            unsigned ci=(v->address>>1)&0x3Fu;
            if(v->cram[ci]!=value){v->cram[ci]=value;v->cram_gba[ci]=vdp_cram_to_gba(value);v->cram_dirty_mask[ci>>5]|=1u<<(ci&31u);v->cram_epoch++;}
            break;
        }
        case MD2GBA_VDP_VSRAM: {
            unsigned vi=(v->address>>1)&0x3Fu;
            if(v->vsram[vi]!=value){v->vsram[vi]=value;v->vsram_dirty_mask[vi>>5]|=1u<<(vi&31u);v->vsram_epoch++;vdp_refresh_present_scroll(v);}
            break;
        }
        default: break;
    }
    v->address = (uint16_t)(v->address + (v->regs[15] ? v->regs[15] : 2));
}

static void md2gba_zero(void *ptr, size_t n) {
    uint8_t *p=(uint8_t*)ptr; while(n--) *p++=0;
}

MD2GBA_COLD void md2gba_bus_init(Md2GbaBus *b, const uint8_t *rom, size_t rom_size) {
    md2gba_zero(b, sizeof(*b));
    b->rom=rom; b->rom_reversed=0; b->rom_size=rom_size;
    md2gba_audio_init(&b->audio);
    if(rom && rom_size>=0x1BCu && rom[0x1B0]=='R' && rom[0x1B1]=='A') {
        uint32_t st=be32p(rom+0x1B4)&0x00FFFFFFu, en=be32p(rom+0x1B8)&0x00FFFFFFu;
        if(en>=st) {
            uint32_t stride=((st^en)&1u)?1u:2u;
            uint32_t n=stride==2u?((en-st)/2u+1u):(en-st+1u);
            if(n<=sizeof(b->sram)) { b->sram_present=1;b->sram_start=st;b->sram_end=en;b->sram_size=n;b->sram_lane_stride=(uint8_t)stride; }
        }
    }
    b->pad1_data=b->pad2_data=0x7F;
    /* Export + NTSC. This is the generic configuration expected by US/EU
       cartridge code unless a frontend explicitly selects another machine. */
    b->hw_version=0x80u;
    b->vdp.regs[15]=2;
    vdp_refresh_present_scroll(&b->vdp);
}
void md2gba_bus_set_reversed_rom(Md2GbaBus *b,const uint8_t *reversed_rom){if(b)b->rom_reversed=reversed_rom;}

void md2gba_vdp_data16_direct(Md2GbaBus*b,uint16_t value){md2gba_vdp_data16_direct_vdp(&b->vdp,value);}

static uint16_t md2gba_vdp_data16_read_direct_vdp(Md2GbaVdp *v) {
    uint16_t value=0xFFFFu;
    uint16_t a=v->address;
    /* PSC's common VDP read helper uses the decoded command state, reads the
       selected backing store, then advances by register 15.  Recreate that
       behavior against MD2GBA's logical big-endian backing representation. */
    switch(v->code & 0x0Fu) {
        case 0x00: { /* VRAM read */
            uint16_t a1=(uint16_t)(a+1u);
            value=(uint16_t)(((uint16_t)v->vram[a]<<8)|v->vram[a1]);
            break;
        }
        case 0x08: { /* CRAM read; 64 words / 0x80 bytes */
            unsigned ci=((unsigned)a&0x7Fu)>>1;
            value=v->cram[ci];
            break;
        }
        case 0x04: { /* VSRAM read; hardware-visible 0x50-byte window */
            unsigned vi=(((unsigned)a%0x50u)>>1);
            value=v->vsram[vi];
            break;
        }
        default:
            break;
    }
    v->address=(uint16_t)(a+(v->regs[15]?v->regs[15]:2u));
    return value;
}

uint8_t md2gba_read8(Md2GbaBus *b, uint32_t address) {
    uint32_t a=md_addr(address),si;
    if(a >= 0xFF0000u) return md2gba_wram_read8_offset(b,a & 0xFFFFu);
    if(sram_index(b,a,&si)) return b->sram[si];
    if(a < b->rom_size) return rom8(b,a);
    if(a >= 0xA00000u && a < 0xA02000u) return b->z80_ram[a - 0xA00000u];
    if(a >= 0xA04000u && a <= 0xA04003u) {
        /* Address-port reads expose backend YM status; data-port reads stay
           open/zero. A missing sink therefore preserves the old ready=0 model. */
        return (a&1u) ? 0u : md2gba_audio_ym_status(&b->audio);
    }
    /* The 68000-visible I/O chip is byte-oriented on odd addresses. Even
       companion bytes read as zero in the generic compatibility model. */
    if(a >= 0xA10000u && a <= 0xA1001Fu && !(a & 1u)) return 0;
    switch(a) {
        case 0xA10001: return b->hw_version;
        case 0xA10003: return pad_read(b,0u);
        case 0xA10005: return pad_read(b,1u);
        case 0xA10009: return b->pad1_control;
        case 0xA1000B: return b->pad2_control;
        case 0xA1000D: return b->expansion_control;
        case 0xA11100: return 0; /* 68K owns Z80 bus in bring-up model */
        case 0xA11200: return 0;
        case 0xC00004: return (uint8_t)(md2gba_vdp_status(b)>>8);
        case 0xC00005: return (uint8_t)md2gba_vdp_status(b);
        default: b->unsupported_reads++; return 0xFF;
    }
}
uint16_t md2gba_read16_slow(Md2GbaBus *b, uint32_t address) {
    uint32_t a=md_addr(address);
    if(a==0xC00000u) return md2gba_vdp_data16_read_direct_vdp(&b->vdp);
    if(a==0xC00004u) return md2gba_vdp_status(b);
    if(a==0xA11100u) return b->z80_bus_request;
    if(a==0xA11200u) return b->z80_reset;
    return ((uint16_t)md2gba_read8(b,a)<<8) | md2gba_read8(b,a+1u);
}
MD2GBA_COLD uint32_t md2gba_read32_slow(Md2GbaBus *b, uint32_t address) {
    uint32_t a=md_addr(address);
    return ((uint32_t)md2gba_read16(b,a)<<16) | md2gba_read16(b,a+2u);
}

void md2gba_write8(Md2GbaBus *b, uint32_t address, uint8_t v) {
    uint32_t a=md_addr(address),si;
    if(a >= 0xFF0000u) { md2gba_wram_write8_offset(b,a & 0xFFFFu,v); return; }
    if(sram_index(b,a,&si)) { b->sram[si]=v; return; }
    /* Cartridge ROM is read-only on the Genesis bus.  Writes are ignored
       rather than counted as unsupported; SRAM overlays were handled above. */
    if(a < b->rom_size) return;
    if(a >= 0xA00000u && a < 0xA02000u) {
        uint16_t o=(uint16_t)(a-0xA00000u); b->z80_ram[o]=v;
        md2gba_audio_write_z80_ram(&b->audio,o,v); return;
    }
    if(a >= 0xA04000u && a <= 0xA04003u) {
        unsigned port=(unsigned)((a>>1)&1u);
        if((a&1u)==0u) b->ym2612_addr[port]=v; else b->ym2612_data[port]=v;
        b->ym2612_writes++;
        md2gba_audio_write_ym_port(&b->audio,(unsigned)(a-0xA04000u),v);
        return;
    }
    if(a >= 0xA10000u && a <= 0xA1001Fu && !(a & 1u)) return;
    switch(a) {
        case 0xA10003: b->pad1_data=v; return;
        case 0xA10005: b->pad2_data=v; return;
        case 0xA10009: b->pad1_control=v; return;
        case 0xA1000B: b->pad2_control=v; return;
        case 0xA1000D: b->expansion_control=v; return;
        case 0xA11100: {
            uint16_t old=b->z80_bus_request, nv=(v&1u)?0x0100u:0u;
            b->z80_bus_request=nv;
            if((old&0x0100u) && !(nv&0x0100u) && b->z80_release_sync)
                (void)b->z80_release_sync(b->z80_release_user,b,16u);
            return;
        }
        case 0xA11200: b->z80_reset=0; return;
        case 0xC00011: md2gba_audio_write_psg(&b->audio,v); return;
        case 0xA130F1: b->sram_control=v; return; /* standard SRAM mapping control */
        default: b->unsupported_writes++; return;
    }
}
void md2gba_write16_slow(Md2GbaBus *b, uint32_t address, uint16_t v) {
    uint32_t a=md_addr(address);
    if(a==0xC00000u) { md2gba_vdp_data16_direct_vdp(&b->vdp,v); return; }
    if(a==0xC00004u) { md2gba_vdp_control16_direct(b,v); return; }
    if(a==0xA11100u) {
        uint16_t old=b->z80_bus_request; b->z80_bus_request=v;
        if((old&0x0100u) && !(v&0x0100u) && b->z80_release_sync)
            (void)b->z80_release_sync(b->z80_release_user,b,16u);
        return;
    }
    if(a==0xA11200u) { b->z80_reset=v; return; }
    md2gba_write8(b,a,(uint8_t)(v>>8)); md2gba_write8(b,a+1u,(uint8_t)v);
}
MD2GBA_COLD void md2gba_write32_slow(Md2GbaBus *b, uint32_t address, uint32_t v) {
    uint32_t a=md_addr(address);
    if(a==0xC00004u) {
        /* A 68000 longword write to the VDP control port is two ordered
           16-bit control-port writes. */
        md2gba_vdp_control16_direct(b,(uint16_t)(v>>16));
        md2gba_vdp_control16_direct(b,(uint16_t)v);
        return;
    }
    if(a==0xC00000u) { md2gba_vdp_data16_direct_vdp(&b->vdp,(uint16_t)(v>>16)); md2gba_vdp_data16_direct_vdp(&b->vdp,(uint16_t)v); return; }
    if(a==0xA14000u && v==0x53454741u) { b->tmss_unlocked=1; return; }
    md2gba_write16(b,a,(uint16_t)(v>>16)); md2gba_write16(b,a+2u,(uint16_t)v);
}
