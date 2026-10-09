#!/usr/bin/env python3
from pathlib import Path
import argparse, csv

ap=argparse.ArgumentParser()
ap.add_argument('generated_c',type=Path)
ap.add_argument('csv',type=Path)
a=ap.parse_args()
rows=list(csv.DictReader(a.csv.open()))
# The rawcache batching assumes consecutive trace records are an exact state chain.
by={}
for r in rows: by.setdefault(r['asset'].lower(),[]).append(r)
for asset,rs in by.items():
    for i,(x,y) in enumerate(zip(rs,rs[1:])):
        fields=[('post_a0','pre_a0'),('post_a1','pre_a1'),('post_sr','pre_sr')]+[(f'post_d{j}',f'pre_d{j}') for j in range(7)]
        for p,q in fields:
            if x[p].lower()!=y[q].lower():
                raise SystemExit(f'non-contiguous Kos trace {asset} rec {i}: {p}={x[p]} != {q}={y[q]}')

p=a.generated_c
s=p.read_text()
needle='int md2gba_profile_predecode_kos(Md2Gba68k*c,Md2GbaBus*b,uint32_t left,uint32_t*cost,uint32_t*steps){'
pos=s.find(needle)
if pos<0: raise SystemExit('function not found')
# Existing generated function is the final item in the file; replace it with the P116 implementation.
head=s[:pos]
body=r'''/* P116 RAWCACHE: publish the immutable pre-expanded Kosinski output directly
   from ROM and consume as many complete command records as fit before the next
   P96 scheduler deadline.  The P112 path already skipped command decoding one
   command at a time; this variant also removes repeated profile re-entry and
   uses wide reversed-WRAM stores.  Guest cycle/step accounting is unchanged. */
#if defined(MD2GBA_GBA_IWRAM)
#define KOS_HOT __attribute__((section(".iwram.kos_rawcache"),noinline))
#else
#define KOS_HOT
#endif

/* P134 architectural-address compatibility for 24-bit trace snapshots. */
static inline uint32_t kos_arch_restore(uint32_t olda,uint32_t bus24){return (olda&0xFF000000u)|(bus24&0xFFFFFFu);}

static inline void kos_raw_store(Md2GbaBus*b,uint32_t dst,const uint8_t*src,unsigned n){
 unsigned o=(unsigned)(dst&0xFFFFu);
 /* All generated records are verified WRAM output and never wrap guest WRAM. */
 while(n && (o&3u)){
  if(n>=2u && !(o&1u)){
   uint16_t v=(uint16_t)(((uint16_t)src[0]<<8)|src[1]);
   md2gba_wram_write16_offset(b,o,v); src+=2u;o+=2u;n-=2u;
  }else{md2gba_wram_write8_offset(b,o,*src++);o++;n--;}
 }
 while(n>=4u){
  uint32_t v=((uint32_t)src[0]<<24)|((uint32_t)src[1]<<16)|((uint32_t)src[2]<<8)|src[3];
  md2gba_wram_write32_offset(b,o,v);src+=4u;o+=4u;n-=4u;
 }
 if(n>=2u){uint16_t v=(uint16_t)(((uint16_t)src[0]<<8)|src[1]);md2gba_wram_write16_offset(b,o,v);src+=2u;o+=2u;n-=2u;}
 if(n)md2gba_wram_write8_offset(b,o,*src);
}

KOS_HOT int md2gba_profile_predecode_kos(Md2Gba68k*c,Md2GbaBus*b,uint32_t left,uint32_t*cost,uint32_t*steps){
 const KosRec*r=0;const KosAsset*a=0;unsigned ai=kos_asset,ix=kos_index,k,i,nrun=0u;
 uint32_t cy=0u,st=0u;
 if(ai<(sizeof(kos_assets)/sizeof(kos_assets[0]))){a=&kos_assets[ai];if(ix<a->n&&kos_match(&a->r[ix],c))r=&a->r[ix];}
 if(!r){
  if(ai<(sizeof(kos_assets)/sizeof(kos_assets[0]))){a=&kos_assets[ai];for(k=ix;k<a->n&&k<ix+4u;k++)if(kos_match(&a->r[k],c)){ix=k;r=&a->r[k];break;}}
  if(!r){for(ai=0u;ai<(sizeof(kos_assets)/sizeof(kos_assets[0]));ai++){a=&kos_assets[ai];if(a->n&&kos_match(&a->r[0],c)){ix=0u;r=&a->r[0];break;}}}
  if(!r){kos_asset=0xFFFFu;kos_index=0u;return 0;}
  kos_asset=(uint16_t)ai;kos_index=(uint16_t)ix;
 }
 /* Preserve the P112 fallback contract for the first record exactly. */
 if(!r->fast || left<=r->cycles){kos_index=(uint16_t)(ix+1u);return 0;}

 /* Raw-cache replay to the next exact machine deadline.  Every next record is
    a generated exact post->pre continuation; the generator-side patch validates
    the complete chain before this source is emitted. */
 for(k=ix;k<a->n;k++){
  const KosRec*q=&a->r[k];
  uint32_t ncy;
  if(!q->fast)break;
  ncy=cy+(uint32_t)q->cycles;
  if(ncy>=left)break; /* strict P96 seam: exact boundary stays canonical */
  if((q->pre_a1&0xFF0000u)!=0xFF0000u || q->outn>255u || ((q->pre_a1+q->outn)&0xFFFFFFu)<q->pre_a1)break;
  if(q->outn)kos_raw_store(b,q->pre_a1,kos_bytes+q->data_off,q->outn);
  cy=ncy;st+=(uint32_t)q->steps;nrun++;
  r=q;
 }
 if(!nrun)return 0; /* defensive; first record admission above guarantees one */

 c->a[0]=kos_arch_restore(c->a[0],r->post_a0);c->a[1]=kos_arch_restore(c->a[1],r->post_a1);
 for(i=0;i<7u;i++)c->d[i]=r->post_d[i];c->sr=r->post_sr;
 kos_index=(uint16_t)(ix+nrun);
 *cost=cy;*steps=st;return 1;
}
'''
p.write_text(head+body)
print(f'P116 rawcache patch: {len(rows)} records / {len(by)} assets; exact continuation chain verified')
