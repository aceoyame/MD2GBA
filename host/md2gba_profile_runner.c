#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_bus.h"
#include "md2gba_aot68k.h"
#include "md2gba_interp68k.h"
#include "md2gba_cycles.h"
#include "md2gba_timing.h"
#include "md2gba_interp_opclass.h"
#include "md2gba_z80.h"

typedef struct PcSlot { uint32_t pc; uint64_t count; uint64_t vdp_data,vdp_control,dma,ym,psg,z80; uint16_t opcode; uint8_t used; } PcSlot;
typedef struct EdgeSlot { uint32_t src,dst; uint64_t count; uint8_t kind,used; } EdgeSlot;
typedef struct VdpEventSlot { uint32_t pc; uint64_t count; uint16_t addr; uint8_t type,target,code,used; } VdpEventSlot;
typedef struct AudioEventSlot { uint32_t pc; uint64_t count; uint16_t key; uint8_t cpu,type,a,b,used; } AudioEventSlot;
typedef struct VdpValueSlot { uint32_t pc; uint64_t count; uint16_t addr,value; uint8_t type,target,code,pending,used; } VdpValueSlot;
typedef struct VdpFrameSlot {
    uint64_t count;
    uint16_t a_base,b_base,sat_base,hscroll_base,max_a,max_b;
    uint8_t width,height,display,reg11,reg12,reg17,reg18,used;
} VdpFrameSlot;
typedef struct VdpTileSetSlot {
    uint64_t count;
    uint32_t refs[64];
    uint16_t a_base,b_base;
    uint8_t width,height,reg11,reg12,reg17,reg18,used;
} VdpTileSetSlot;
static PcSlot *pcs; static size_t pc_cap=(1u<<20), pc_used;
static EdgeSlot *edges; static size_t edge_cap=(1u<<21), edge_used;
static VdpEventSlot *vdp_events; static size_t vdp_cap=(1u<<18), vdp_used;
static AudioEventSlot *audio_events; static size_t audio_cap=(1u<<18), audio_used;
static VdpValueSlot *vdp_values; static size_t vdp_value_cap=(1u<<19), vdp_value_used;
static VdpFrameSlot *vdp_frames; static size_t vdp_frame_cap=(1u<<14), vdp_frame_used;
static VdpTileSetSlot *vdp_tile_sets; static size_t vdp_tile_set_cap=(1u<<14), vdp_tile_set_used;
static uint32_t profile_actor_pc; static uint8_t profile_actor_cpu; /* 0=68K,1=Z80 */
static FILE *ym_trace_f;
static uint64_t op_counts[65536];
static uint32_t hash32(uint32_t x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x; }
static PcSlot* pc_add(uint32_t pc,uint16_t op){ size_t i=hash32(pc)&(pc_cap-1u); while(pcs[i].used&&pcs[i].pc!=pc)i=(i+1u)&(pc_cap-1u); if(!pcs[i].used){pcs[i].used=1;pcs[i].pc=pc;pcs[i].opcode=op;pc_used++;} pcs[i].count++; return &pcs[i]; }
static void edge_add(uint32_t s,uint32_t d,uint8_t kind){ uint32_t h=hash32(s^(d*0x9e3779b9u)^kind); size_t i=h&(edge_cap-1u); while(edges[i].used&&(edges[i].src!=s||edges[i].dst!=d||edges[i].kind!=kind))i=(i+1u)&(edge_cap-1u); if(!edges[i].used){edges[i].used=1;edges[i].src=s;edges[i].dst=d;edges[i].kind=kind;edge_used++;}edges[i].count++; }
static void vdp_event_add(uint32_t pc,uint8_t type,uint8_t target,uint8_t code,uint16_t addr){uint32_t h=hash32(pc^((uint32_t)addr<<8)^((uint32_t)type<<3)^((uint32_t)target<<15)^((uint32_t)code<<22));size_t i=h&(vdp_cap-1u);while(vdp_events[i].used&&(vdp_events[i].pc!=pc||vdp_events[i].type!=type||vdp_events[i].target!=target||vdp_events[i].code!=code||vdp_events[i].addr!=addr))i=(i+1u)&(vdp_cap-1u);if(!vdp_events[i].used){vdp_events[i].used=1;vdp_events[i].pc=pc;vdp_events[i].type=type;vdp_events[i].target=target;vdp_events[i].code=code;vdp_events[i].addr=addr;vdp_used++;}vdp_events[i].count++;}
static void vdp_value_add(uint32_t pc,uint8_t type,uint8_t target,uint8_t code,uint8_t pending,uint16_t addr,uint16_t value){uint32_t h=hash32(pc^((uint32_t)addr<<5)^((uint32_t)value<<13)^((uint32_t)type<<1)^((uint32_t)target<<24)^((uint32_t)code<<27)^pending);size_t i=h&(vdp_value_cap-1u);while(vdp_values[i].used&&(vdp_values[i].pc!=pc||vdp_values[i].type!=type||vdp_values[i].target!=target||vdp_values[i].code!=code||vdp_values[i].pending!=pending||vdp_values[i].addr!=addr||vdp_values[i].value!=value))i=(i+1u)&(vdp_value_cap-1u);if(!vdp_values[i].used){vdp_values[i].used=1;vdp_values[i].pc=pc;vdp_values[i].type=type;vdp_values[i].target=target;vdp_values[i].code=code;vdp_values[i].pending=pending;vdp_values[i].addr=addr;vdp_values[i].value=value;vdp_value_used++;}vdp_values[i].count++;}
static unsigned profile_vdp_size_cells(unsigned code){ switch(code&3u){case 0u:return 32u;case 1u:return 64u;case 3u:return 128u;default:return 32u;} }
static uint16_t profile_vdp_word(const Md2GbaVdp*v,unsigned a){ return (uint16_t)(((uint16_t)v->vram[(uint16_t)a]<<8)|v->vram[(uint16_t)(a+1u)]); }
static unsigned profile_vdp_collect_tiles(const Md2GbaVdp*v,uint16_t base,unsigned w,unsigned h,uint32_t refs[64]){
    unsigned max=0u; if(w>128u||h>128u)return 2047u;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){unsigned t=profile_vdp_word(v,(unsigned)base+2u*(y*w+x))&0x7ffu;refs[t>>5]|=1u<<(t&31u);if(t>max)max=t;}
    return max;
}
static uint32_t profile_refs_hash(const uint32_t refs[64]){uint32_t h=0x811c9dc5u;for(unsigned i=0;i<64u;i++){h^=refs[i];h*=0x01000193u;h^=refs[i]>>16;}return hash32(h);}
static void vdp_tile_set_add(unsigned w,unsigned h,uint16_t a,uint16_t b,const Md2GbaVdp*v,const uint32_t refs[64]){
    uint32_t key=profile_refs_hash(refs)^((uint32_t)w<<1)^((uint32_t)h<<9)^((uint32_t)a<<3)^((uint32_t)b<<5)^((uint32_t)v->regs[11]<<19)^((uint32_t)v->regs[12]<<25);
    size_t i=hash32(key)&(vdp_tile_set_cap-1u);
    while(vdp_tile_sets[i].used && !(vdp_tile_sets[i].width==w&&vdp_tile_sets[i].height==h&&vdp_tile_sets[i].a_base==a&&vdp_tile_sets[i].b_base==b&&vdp_tile_sets[i].reg11==v->regs[11]&&vdp_tile_sets[i].reg12==v->regs[12]&&vdp_tile_sets[i].reg17==v->regs[17]&&vdp_tile_sets[i].reg18==v->regs[18]&&!memcmp(vdp_tile_sets[i].refs,refs,sizeof(vdp_tile_sets[i].refs))))i=(i+1u)&(vdp_tile_set_cap-1u);
    if(!vdp_tile_sets[i].used){vdp_tile_sets[i].used=1u;vdp_tile_sets[i].width=(uint8_t)w;vdp_tile_sets[i].height=(uint8_t)h;vdp_tile_sets[i].a_base=a;vdp_tile_sets[i].b_base=b;vdp_tile_sets[i].reg11=v->regs[11];vdp_tile_sets[i].reg12=v->regs[12];vdp_tile_sets[i].reg17=v->regs[17];vdp_tile_sets[i].reg18=v->regs[18];memcpy(vdp_tile_sets[i].refs,refs,sizeof(vdp_tile_sets[i].refs));vdp_tile_set_used++;}
    vdp_tile_sets[i].count++;
}
static void vdp_frame_add(const Md2GbaVdp*v){
    unsigned w=profile_vdp_size_cells(v->regs[16]&3u),h=profile_vdp_size_cells((v->regs[16]>>4)&3u);
    uint16_t a=(uint16_t)((v->regs[2]&0x38u)<<10),b=(uint16_t)((v->regs[4]&7u)<<13);
    unsigned mask=((v->regs[12]&0x81u)==0x81u)?0x7eu:0x7fu;
    uint16_t sat=(uint16_t)((v->regs[5]&mask)<<9),hs=(uint16_t)((v->regs[13]&0x3fu)<<10);
    uint32_t refs[64]={0};
    unsigned ma=profile_vdp_collect_tiles(v,a,w,h,refs),mb=profile_vdp_collect_tiles(v,b,w,h,refs);
    if(v->regs[1]&0x40u)vdp_tile_set_add(w,h,a,b,v,refs);
    uint32_t key=(uint32_t)w^((uint32_t)h<<8)^((uint32_t)a<<1)^((uint32_t)b<<3)^((uint32_t)sat<<5)^((uint32_t)hs<<7)^((uint32_t)v->regs[11]<<17)^((uint32_t)v->regs[12]<<23)^((uint32_t)ma<<9)^((uint32_t)mb<<21);
    size_t i=hash32(key)&(vdp_frame_cap-1u);
    while(vdp_frames[i].used && !(vdp_frames[i].width==w&&vdp_frames[i].height==h&&vdp_frames[i].a_base==a&&vdp_frames[i].b_base==b&&vdp_frames[i].sat_base==sat&&vdp_frames[i].hscroll_base==hs&&vdp_frames[i].display==!!(v->regs[1]&0x40u)&&vdp_frames[i].reg11==v->regs[11]&&vdp_frames[i].reg12==v->regs[12]&&vdp_frames[i].reg17==v->regs[17]&&vdp_frames[i].reg18==v->regs[18]&&vdp_frames[i].max_a==ma&&vdp_frames[i].max_b==mb)) i=(i+1u)&(vdp_frame_cap-1u);
    if(!vdp_frames[i].used){vdp_frames[i].used=1u;vdp_frames[i].width=(uint8_t)w;vdp_frames[i].height=(uint8_t)h;vdp_frames[i].a_base=a;vdp_frames[i].b_base=b;vdp_frames[i].sat_base=sat;vdp_frames[i].hscroll_base=hs;vdp_frames[i].display=(uint8_t)!!(v->regs[1]&0x40u);vdp_frames[i].reg11=v->regs[11];vdp_frames[i].reg12=v->regs[12];vdp_frames[i].reg17=v->regs[17];vdp_frames[i].reg18=v->regs[18];vdp_frames[i].max_a=(uint16_t)ma;vdp_frames[i].max_b=(uint16_t)mb;vdp_frame_used++;}
    vdp_frames[i].count++;
}
static void audio_event_add(uint8_t cpu,uint32_t pc,uint8_t type,uint16_t key,uint8_t a,uint8_t b){uint32_t h=hash32(pc^((uint32_t)key<<7)^((uint32_t)type<<21)^((uint32_t)cpu<<29)^((uint32_t)a<<1)^((uint32_t)b<<13));size_t i=h&(audio_cap-1u);while(audio_events[i].used&&(audio_events[i].cpu!=cpu||audio_events[i].pc!=pc||audio_events[i].type!=type||audio_events[i].key!=key||audio_events[i].a!=a||audio_events[i].b!=b))i=(i+1u)&(audio_cap-1u);if(!audio_events[i].used){audio_events[i].used=1;audio_events[i].cpu=cpu;audio_events[i].pc=pc;audio_events[i].type=type;audio_events[i].key=key;audio_events[i].a=a;audio_events[i].b=b;audio_used++;}audio_events[i].count++;}
static void trace_ym(void*u,uint8_t port,uint8_t reg,uint8_t value){(void)u;audio_event_add(profile_actor_cpu,profile_actor_pc,1u,(uint16_t)(((uint16_t)port<<8)|reg),reg,value);if(ym_trace_f){uint8_t q[3]={port,reg,value};(void)fwrite(q,1,3,ym_trace_f);}}
static uint8_t trace_ym_status(void*u){(void)u;return 0u;}
static void trace_psg(void*u,uint8_t value){(void)u;audio_event_add(profile_actor_cpu,profile_actor_pc,2u,value,value,0u);}
static void trace_z80_write(void*u,uint16_t off,uint8_t value){(void)u;audio_event_add(0u,profile_actor_pc,3u,off,(uint8_t)off,value);}
static int cmp_pc(const void*a,const void*b){const PcSlot*x=a,*y=b; if(x->count<y->count)return 1;if(x->count>y->count)return -1;return x->pc<y->pc?-1:x->pc>y->pc;}
static int cmp_edge(const void*a,const void*b){const EdgeSlot*x=a,*y=b;if(x->count<y->count)return 1;if(x->count>y->count)return -1;if(x->src!=y->src)return x->src<y->src?-1:1;return x->dst<y->dst?-1:x->dst>y->dst;}
static uint8_t edge_kind(uint8_t cl,uint32_t src,uint32_t dst){(void)src;(void)dst; switch(cl){case MD68KI_JSR:return 2;case MD68KI_JMP:return 3;case MD68KI_RTS:return 4;case MD68KI_RTE:return 5;case MD68KI_BSR:return 6;case MD68KI_BRA:return 7;case MD68KI_BCC:case MD68KI_DBCC:return 8;case MD68KI_TRAP:return 9;default:return 1;}}
static uint32_t rng_state=1; static uint32_t rng32(void){rng_state=rng_state*1664525u+1013904223u;return rng_state;}

/* P148 host-only profiling save state.  The file is intentionally tied to this
   runner ABI: sizeof guards reject incompatible builds. Host pointers are never
   persisted.  These states are profiling corpus artifacts, not emulator saves. */
typedef struct ProfileStateHeader {
    char magic[8];
    uint32_t version,cpu_size,timing_size,bus_size,z80_size;
    uint64_t rom_size,rom_fnv64,absolute_frame,z80_clock_num;
} ProfileStateHeader;
static uint64_t fnv1a64(const uint8_t *p,size_t n){uint64_t h=1469598103934665603ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
static int save_state(const char *path,const Md2Gba68k *cpu,const Md2GbaBus *bus,const Md2GbaTiming *timing,const Md2GbaZ80 *z80,uint64_t z80_clock_num,uint64_t rom_hash){
    FILE *f=fopen(path,"wb"); if(!f)return 0;
    ProfileStateHeader h={{'M','2','G','S','T','A','T','E'},2u,sizeof(*cpu),sizeof(*timing),sizeof(*bus),sizeof(*z80),bus->rom_size,rom_hash,timing->vblanks,z80_clock_num};
    Md2GbaBus b=*bus;
    b.rom=NULL;b.rom_reversed=NULL;b.pad_poll=NULL;b.pad_poll_user=NULL;b.z80_release_sync=NULL;b.z80_release_user=NULL;
    memset(&b.audio.sink,0,sizeof(b.audio.sink));
    int ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(cpu,1,sizeof(*cpu),f)==sizeof(*cpu)&&fwrite(timing,1,sizeof(*timing),f)==sizeof(*timing)&&fwrite(&b,1,sizeof(b),f)==sizeof(b)&&fwrite(z80,1,sizeof(*z80),f)==sizeof(*z80);
    fclose(f);return ok;
}
static int load_state(const char *path,Md2Gba68k *cpu,Md2GbaBus *bus,Md2GbaTiming *timing,Md2GbaZ80 *z80,uint64_t *z80_clock_num,const uint8_t *rom,size_t rom_size,uint64_t rom_hash){
    FILE*f=fopen(path,"rb");if(!f)return 0; ProfileStateHeader h; Md2GbaBus b;
    int ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&memcmp(h.magic,"M2GSTATE",8)==0&&h.version==2u&&h.cpu_size==sizeof(*cpu)&&h.timing_size==sizeof(*timing)&&h.bus_size==sizeof(*bus)&&h.z80_size==sizeof(*z80)&&h.rom_size==rom_size&&h.rom_fnv64==rom_hash;
    if(ok)ok=fread(cpu,1,sizeof(*cpu),f)==sizeof(*cpu)&&fread(timing,1,sizeof(*timing),f)==sizeof(*timing)&&fread(&b,1,sizeof(b),f)==sizeof(b)&&fread(z80,1,sizeof(*z80),f)==sizeof(*z80); fclose(f); if(!ok)return 0;
    b.rom=rom;b.rom_reversed=NULL;b.rom_size=rom_size;b.pad_poll=NULL;b.pad_poll_user=NULL;b.z80_release_sync=NULL;b.z80_release_user=NULL;memset(&b.audio.sink,0,sizeof(b.audio.sink));*bus=b;*z80_clock_num=h.z80_clock_num;return 1;
}

static uint16_t input_for_frame(uint64_t f,int policy){
 /* Generic exploration policies.  These deliberately know nothing about a title. */
 if(policy==0)return 0;
 if(policy==1){
   if(f>=90&&f<94)return MD2GBA_PAD_START; if(f<150)return 0;
   uint32_t r=(uint32_t)(f/30u)*0x9e3779b9u+rng_state; uint16_t m=0;
   if(r&1)m|=MD2GBA_PAD_RIGHT; else if(r&2)m|=MD2GBA_PAD_LEFT;
   if(r&4)m|=MD2GBA_PAD_A; if(r&8)m|=MD2GBA_PAD_B; if(r&16)m|=MD2GBA_PAD_C; return m;
 }
 if(policy==2)return (uint16_t)(rng32()&0xffu);
 if(policy==3){
   uint64_t phase=(f/240u)&3u; uint16_t m=(phase==0?MD2GBA_PAD_RIGHT:phase==1?MD2GBA_PAD_LEFT:phase==2?MD2GBA_PAD_DOWN:MD2GBA_PAD_UP);
   if((f%53u)<4u)m|=MD2GBA_PAD_A; if((f%89u)<4u)m|=MD2GBA_PAD_B; if(f>=75&&f<79)m|=MD2GBA_PAD_START; return m;
 }
 if(policy==4){
   uint16_t m=0; if((f%180u)<3u)m|=MD2GBA_PAD_START; if((f%47u)<5u)m|=MD2GBA_PAD_A;
   if(((f/30u)&1u)==0)m|=MD2GBA_PAD_DOWN; else m|=MD2GBA_PAD_UP; return m;
 }
 if(policy==5){uint16_t m=MD2GBA_PAD_RIGHT;if((f%71u)<6u)m|=MD2GBA_PAD_A;if((f%113u)<4u)m|=MD2GBA_PAD_B;if(f>=90&&f<94)m|=MD2GBA_PAD_START;return m;}
 if(policy==6){uint16_t m=MD2GBA_PAD_LEFT;if((f%67u)<7u)m|=MD2GBA_PAD_C;if((f%131u)<4u)m|=MD2GBA_PAD_A;if(f>=90&&f<94)m|=MD2GBA_PAD_START;return m;}
 static uint16_t burst=0; if((f%24u)==0){uint32_t r=rng32();burst=(uint16_t)(r&0x7fu);if((r&0x100u)!=0)burst|=MD2GBA_PAD_RIGHT;if((r&0x200u)!=0)burst|=MD2GBA_PAD_LEFT;} if(f>=90&&f<94)burst|=MD2GBA_PAD_START; return burst;
}
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"usage: %s ROM OUT_PREFIX [frames=1800] [max_steps=100000000] [policy=1] [seed=1] [state_in=-] [state_dir=-] [snapshot_every=0]\n",argv[0]);return 2;}
 const char*rp=argv[1],*pre=argv[2]; uint64_t requested_frames=argc>3?strtoull(argv[3],0,0):1800ull; uint64_t max_steps=argc>4?strtoull(argv[4],0,0):100000000ull; int policy=argc>5?atoi(argv[5]):1; rng_state=argc>6?(uint32_t)strtoul(argv[6],0,0):1u;
 const char*state_in=argc>7?argv[7]:"-";const char*state_dir=argc>8?argv[8]:"-";uint64_t snapshot_every=argc>9?strtoull(argv[9],0,0):0ull;
 FILE*f=fopen(rp,"rb");if(!f){perror(rp);return 2;}fseek(f,0,SEEK_END);long ln=ftell(f);fseek(f,0,SEEK_SET);if(ln<8){fprintf(stderr,"bad rom\n");return 2;}uint8_t*rom=malloc((size_t)ln);if(!rom)return 2;if(fread(rom,1,(size_t)ln,f)!=(size_t)ln){return 2;}fclose(f);uint64_t rom_hash=fnv1a64(rom,(size_t)ln);
 pcs=calloc(pc_cap,sizeof(*pcs));edges=calloc(edge_cap,sizeof(*edges));vdp_events=calloc(vdp_cap,sizeof(*vdp_events));audio_events=calloc(audio_cap,sizeof(*audio_events));vdp_values=calloc(vdp_value_cap,sizeof(*vdp_values));vdp_frames=calloc(vdp_frame_cap,sizeof(*vdp_frames));vdp_tile_sets=calloc(vdp_tile_set_cap,sizeof(*vdp_tile_sets));if(!pcs||!edges||!vdp_events||!audio_events||!vdp_values||!vdp_frames||!vdp_tile_sets)return 2;
 Md2GbaBus bus;Md2Gba68k cpu;Md2GbaTiming timing;Md2GbaZ80 zcpu;uint64_t z80_clock_num=0;int resumed=0;
 if(state_in[0]&&strcmp(state_in,"-")!=0){if(!load_state(state_in,&cpu,&bus,&timing,&zcpu,&z80_clock_num,rom,(size_t)ln,rom_hash)){fprintf(stderr,"could not load compatible state: %s\n",state_in);return 3;}resumed=1;}
 else {md2gba_bus_init(&bus,rom,(size_t)ln);md68k_reset_context(&cpu);cpu.a[7]=md2gba_read32(&bus,0)&0xffffffu;cpu.pc=md2gba_read32(&bus,4)&0xffffffu;md2gba_timing_init(&timing,0,1);md2gba_timing_enable_cycles(&timing,262,224,488);md2gba_z80_init(&zcpu);}
 Md2GbaAudioSink sink={0};sink.ym2612_write=trace_ym;sink.ym2612_status=trace_ym_status;sink.psg_write=trace_psg;sink.z80_ram_write=trace_z80_write;md2gba_bus_set_audio_sink(&bus,&sink);
 uint64_t start_frames=timing.vblanks,target_frames=start_frames+requested_frames,next_snapshot=snapshot_every?snapshot_every:UINT64_MAX,snapshot_count=0;
 char ymtrace_path[1024];snprintf(ymtrace_path,sizeof(ymtrace_path),"%s.ymraw.bin",pre);ym_trace_f=fopen(ymtrace_path,"wb");
 char states_tsv[1024];FILE*so=NULL;if(state_dir[0]&&strcmp(state_dir,"-")!=0&&snapshot_every){snprintf(states_tsv,sizeof(states_tsv),"%s.states.tsv",pre);so=fopen(states_tsv,"w");if(so)fprintf(so,"path\trel_frame\tabs_frame\tpc\n");}
 uint64_t steps=0;int stop=0,last_r=1;uint32_t fail_pc=0;uint16_t fail_op=0;uint64_t vdp_data0=0,vdp_ctl0=0,dma0=0;
 while(steps<max_steps && timing.vblanks<target_frames){
  uint32_t pc=cpu.pc&0xffffffu;if(pc&1u){stop=-1;fail_pc=pc;break;}uint16_t op=md2gba_read16(&bus,pc);uint8_t cl=md68k_interp_opclass[op];PcSlot*ps=pc_add(pc,op);op_counts[op]++;uint64_t oldv=timing.vblanks;uint32_t old_data=bus.vdp.data_writes,old_ctl=bus.vdp.control_writes,old_dma=bus.vdp.dma_transfers,old_ym=bus.audio.ym_data_writes,old_psg=bus.audio.psg_writes,old_z80=bus.audio.z80_ram_writes;
  bus.vdp.trace_count=0u;profile_actor_cpu=0u;profile_actor_pc=pc;int r=md68k_interp_step(&cpu,&bus);steps++;if(r<=0){stop=r? -2:-3;fail_pc=pc;fail_op=op;last_r=r;break;}uint32_t dst=cpu.pc&0xffffffu;edge_add(pc,dst,edge_kind(cl,pc,dst));
  uint32_t cyc=md68k_estimate_cycles(&bus,pc,dst);(void)md2gba_timing_after_step(&cpu,&bus,pc,(uint32_t)steps,&timing);
  for(unsigned ti=0;ti<bus.vdp.trace_count && ti<32u;ti++){vdp_event_add(pc,bus.vdp.trace_type[ti],bus.vdp.trace_target[ti],bus.vdp.trace_code[ti],bus.vdp.trace_addr[ti]);vdp_value_add(pc,bus.vdp.trace_type[ti],bus.vdp.trace_target[ti],bus.vdp.trace_code[ti],bus.vdp.trace_pending[ti],bus.vdp.trace_addr[ti],bus.vdp.trace_value[ti]);}
  /* Genesis clocks derive from the same master: 68K=MCLK/7, Z80=MCLK/15.
     Accumulate the exact 7/15 T-state ratio offline so sound-CPU profiling
     follows the 68K timeline without host-wall-clock dependence. */
  z80_clock_num+=(uint64_t)cyc*7ull;unsigned zbudget=(unsigned)(z80_clock_num/15ull);z80_clock_num%=15ull;if(zbudget){profile_actor_cpu=1u;profile_actor_pc=zcpu.pc;(void)md2gba_z80_run(&zcpu,&bus,zbudget);profile_actor_cpu=0u;profile_actor_pc=pc;}
  uint32_t dd=bus.vdp.data_writes-old_data,dc=bus.vdp.control_writes-old_ctl,dm=bus.vdp.dma_transfers-old_dma,dy=bus.audio.ym_data_writes-old_ym,dp=bus.audio.psg_writes-old_psg,dz=bus.audio.z80_ram_writes-old_z80;ps->vdp_data+=dd;ps->vdp_control+=dc;ps->dma+=dm;ps->ym+=dy;ps->psg+=dp;ps->z80+=dz;vdp_data0+=dd;vdp_ctl0+=dc;dma0+=dm;
  if(timing.vblanks!=oldv){uint64_t rel=timing.vblanks-start_frames;vdp_frame_add(&bus.vdp);md2gba_z80_raise_irq(&zcpu);md2gba_bus_set_pad(&bus,0,input_for_frame(timing.vblanks,policy));if(so&&rel>=next_snapshot){char sp[1024];snprintf(sp,sizeof(sp),"%s/state_%04llu_f%010llu.m2gs",state_dir,(unsigned long long)snapshot_count,(unsigned long long)rel);if(save_state(sp,&cpu,&bus,&timing,&zcpu,z80_clock_num,rom_hash)){fprintf(so,"%s\t%llu\t%u\t%06X\n",sp,(unsigned long long)rel,timing.vblanks,cpu.pc&0xffffffu);fflush(so);snapshot_count++;}next_snapshot+=snapshot_every;}}
 }
 if(ym_trace_f){fclose(ym_trace_f);ym_trace_f=NULL;}
 if(so){/* Always preserve the furthest clean state so successful campaigns can deepen the corpus. */ if(!stop){uint64_t rel=timing.vblanks-start_frames;char sp[1024];snprintf(sp,sizeof(sp),"%s/state_%04llu_final_f%010llu.m2gs",state_dir,(unsigned long long)snapshot_count,(unsigned long long)rel);if(save_state(sp,&cpu,&bus,&timing,&zcpu,z80_clock_num,rom_hash))fprintf(so,"%s\t%llu\t%u\t%06X\n",sp,(unsigned long long)rel,timing.vblanks,cpu.pc&0xffffffu);}fclose(so);}
 char path[1024];snprintf(path,sizeof(path),"%s.pcs.tsv",pre);FILE*po=fopen(path,"w");PcSlot*pa=malloc(pc_used*sizeof(*pa));size_t pn=0;for(size_t i=0;i<pc_cap;i++)if(pcs[i].used)pa[pn++]=pcs[i];qsort(pa,pn,sizeof(*pa),cmp_pc);fprintf(po,"pc\tcount\topcode\topclass\tvdp_data\tvdp_control\tdma\tym\tpsg\tz80\n");for(size_t i=0;i<pn;i++)fprintf(po,"%06X\t%llu\t%04X\t%u\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",pa[i].pc,(unsigned long long)pa[i].count,pa[i].opcode,(unsigned)md68k_interp_opclass[pa[i].opcode],(unsigned long long)pa[i].vdp_data,(unsigned long long)pa[i].vdp_control,(unsigned long long)pa[i].dma,(unsigned long long)pa[i].ym,(unsigned long long)pa[i].psg,(unsigned long long)pa[i].z80);fclose(po);
 snprintf(path,sizeof(path),"%s.edges.tsv",pre);FILE*eo=fopen(path,"w");EdgeSlot*ea=malloc(edge_used*sizeof(*ea));size_t en=0;for(size_t i=0;i<edge_cap;i++)if(edges[i].used)ea[en++]=edges[i];qsort(ea,en,sizeof(*ea),cmp_edge);fprintf(eo,"src\tdst\tcount\tkind\n");for(size_t i=0;i<en;i++)fprintf(eo,"%06X\t%06X\t%llu\t%u\n",ea[i].src,ea[i].dst,(unsigned long long)ea[i].count,(unsigned)ea[i].kind);fclose(eo);
 snprintf(path,sizeof(path),"%s.vdp.tsv",pre);FILE*vo=fopen(path,"w");fprintf(vo,"pc\ttype\ttarget\tcode\taddress\tcount\n");for(size_t i=0;i<vdp_cap;i++)if(vdp_events[i].used)fprintf(vo,"%06X\t%u\t%u\t%u\t%04X\t%llu\n",vdp_events[i].pc,(unsigned)vdp_events[i].type,(unsigned)vdp_events[i].target,(unsigned)vdp_events[i].code,vdp_events[i].addr,(unsigned long long)vdp_events[i].count);fclose(vo);
 snprintf(path,sizeof(path),"%s.vdpvalues.tsv",pre);FILE*vv=fopen(path,"w");fprintf(vv,"pc\ttype\ttarget\tcode\tpending\taddress\tvalue\tcount\n");for(size_t i=0;i<vdp_value_cap;i++)if(vdp_values[i].used)fprintf(vv,"%06X\t%u\t%u\t%u\t%u\t%04X\t%04X\t%llu\n",vdp_values[i].pc,(unsigned)vdp_values[i].type,(unsigned)vdp_values[i].target,(unsigned)vdp_values[i].code,(unsigned)vdp_values[i].pending,vdp_values[i].addr,vdp_values[i].value,(unsigned long long)vdp_values[i].count);fclose(vv);
 snprintf(path,sizeof(path),"%s.vdpframes.tsv",pre);FILE*vf=fopen(path,"w");fprintf(vf,"width\theight\tdisplay\ta_base\tb_base\tsat_base\thscroll_base\treg11\treg12\treg17\treg18\tmax_a\tmax_b\tcount\n");for(size_t i=0;i<vdp_frame_cap;i++)if(vdp_frames[i].used)fprintf(vf,"%u\t%u\t%u\t%04X\t%04X\t%04X\t%04X\t%02X\t%02X\t%02X\t%02X\t%u\t%u\t%llu\n",(unsigned)vdp_frames[i].width,(unsigned)vdp_frames[i].height,(unsigned)vdp_frames[i].display,vdp_frames[i].a_base,vdp_frames[i].b_base,vdp_frames[i].sat_base,vdp_frames[i].hscroll_base,(unsigned)vdp_frames[i].reg11,(unsigned)vdp_frames[i].reg12,(unsigned)vdp_frames[i].reg17,(unsigned)vdp_frames[i].reg18,(unsigned)vdp_frames[i].max_a,(unsigned)vdp_frames[i].max_b,(unsigned long long)vdp_frames[i].count);fclose(vf);
 snprintf(path,sizeof(path),"%s.vdptilesets.tsv",pre);FILE*tf=fopen(path,"w");fprintf(tf,"width\theight\ta_base\tb_base\treg11\treg12\treg17\treg18\trefs_hex\tcount\n");for(size_t i=0;i<vdp_tile_set_cap;i++)if(vdp_tile_sets[i].used){fprintf(tf,"%u\t%u\t%04X\t%04X\t%02X\t%02X\t%02X\t%02X\t",(unsigned)vdp_tile_sets[i].width,(unsigned)vdp_tile_sets[i].height,vdp_tile_sets[i].a_base,vdp_tile_sets[i].b_base,(unsigned)vdp_tile_sets[i].reg11,(unsigned)vdp_tile_sets[i].reg12,(unsigned)vdp_tile_sets[i].reg17,(unsigned)vdp_tile_sets[i].reg18);for(unsigned w=0;w<64u;w++)fprintf(tf,"%08X",vdp_tile_sets[i].refs[w]);fprintf(tf,"\t%llu\n",(unsigned long long)vdp_tile_sets[i].count);}fclose(tf);
 snprintf(path,sizeof(path),"%s.audio.tsv",pre);FILE*ao=fopen(path,"w");fprintf(ao,"cpu\tpc\ttype\tkey\ta\tb\tcount\n");for(size_t i=0;i<audio_cap;i++)if(audio_events[i].used)fprintf(ao,"%u\t%06X\t%u\t%04X\t%02X\t%02X\t%llu\n",(unsigned)audio_events[i].cpu,audio_events[i].pc,(unsigned)audio_events[i].type,audio_events[i].key,(unsigned)audio_events[i].a,(unsigned)audio_events[i].b,(unsigned long long)audio_events[i].count);fclose(ao);
 snprintf(path,sizeof(path),"%s.z80.tsv",pre);FILE*zo=fopen(path,"w");fprintf(zo,"pc\tcount\n");for(unsigned i=0;i<0x2000u;i++)if(md2gba_z80_profile[i])fprintf(zo,"%04X\t%u\n",i,md2gba_z80_profile[i]);fclose(zo);
 snprintf(path,sizeof(path),"%s.z80ram.bin",pre);FILE*zr=fopen(path,"wb");if(zr){fwrite(bus.z80_ram,1,sizeof(bus.z80_ram),zr);fclose(zr);}
 snprintf(path,sizeof(path),"%s.summary.json",pre);FILE*jo=fopen(path,"w");fprintf(jo,"{\n  \"steps\": %llu,\n  \"frames\": %llu,\n  \"start_frame\": %llu,\n  \"end_frame\": %u,\n  \"resumed_state\": %s,\n  \"snapshots\": %llu,\n  \"unique_pcs\": %zu,\n  \"unique_edges\": %zu,\n  \"stop_code\": %d,\n  \"interp_result\": %d,\n  \"fail_pc\": \"%06X\",\n  \"fail_opcode\": \"%04X\",\n  \"vdp_data_writes\": %llu,\n  \"vdp_control_writes\": %llu,\n  \"vdp_dma_transfers\": %llu,\n  \"unsupported_reads\": %u,\n  \"unsupported_writes\": %u,\n  \"ym_writes\": %u,\n  \"psg_writes\": %u,\n  \"z80_ram_writes\": %u,\n  \"z80_executed\": %u,\n  \"z80_unsupported\": %u,\n  \"vdp_event_signatures\": %zu,\n  \"vdp_value_signatures\": %zu,\n  \"audio_event_signatures\": %zu,\n  \"vdp_frame_signatures\": %zu,\n  \"vdp_tile_set_signatures\": %zu\n}\n",(unsigned long long)steps,(unsigned long long)(timing.vblanks-start_frames),(unsigned long long)start_frames,timing.vblanks,resumed?"true":"false",(unsigned long long)snapshot_count,pn,en,stop,last_r,fail_pc,fail_op,(unsigned long long)vdp_data0,(unsigned long long)vdp_ctl0,(unsigned long long)dma0,bus.unsupported_reads,bus.unsupported_writes,bus.audio.ym_data_writes,bus.audio.psg_writes,bus.audio.z80_ram_writes,zcpu.executed,zcpu.unsupported,vdp_used,vdp_value_used,audio_used,vdp_frame_used,vdp_tile_set_used);fclose(jo);
 printf("steps=%llu frames=%llu start=%llu resumed=%d pcs=%zu edges=%zu stop=%d fail=%06X/%04X states=%llu vdp=%llu/%llu dma=%llu z80=%u/%u vdp_sigs=%zu vdp_values=%zu audio_sigs=%zu vdp_frames=%zu vdp_tilesets=%zu\n",(unsigned long long)steps,(unsigned long long)(timing.vblanks-start_frames),(unsigned long long)start_frames,resumed,pn,en,stop,fail_pc,fail_op,(unsigned long long)snapshot_count,(unsigned long long)vdp_data0,(unsigned long long)vdp_ctl0,(unsigned long long)dma0,zcpu.executed,zcpu.unsupported,vdp_used,vdp_value_used,audio_used,vdp_frame_used,vdp_tile_set_used);return stop?1:0;
}
