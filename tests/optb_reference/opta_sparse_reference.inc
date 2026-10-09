static unsigned opta_md2gba_gba_stage_dirty_cached_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c){
    unsigned n=0;
    if(md2gba_cache_has_source_mask(c)){
        unsigned wi;for(wi=0u;wi<64u;wi++){uint32_t bits=v->vram_dirty_tiles[wi]&c->source_mask[wi];unsigned bi=0u;while(bits){if(bits&1u){unsigned src=wi*32u+bi,slot=c->source_to_slot[src];if(slot!=0xFFFFu&&slot<c->used_slots){gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;}}bits>>=1;bi++;}}
    }else{
        unsigned slot;for(slot=1u;slot<c->used_slots;slot++){unsigned src=c->slot_to_source[slot];if(!md2gba_source_tile_dirty(v,src))continue;gba_convert_tile32_to_ram(md2gba_bg_tile_stage+slot*16u,&v->vram[(uint16_t)(src*32u)]);n++;}
    }
    return n;
}
static MD2GBA_ROM_COLD int opta_md2gba_hwq_add_tiles(const Md2GbaVdp*v,const Md2GbaBgTileCache*c,unsigned old_used,int patch_mode){
    unsigned slot;
    for(slot=1u;slot<c->used_slots;slot++){
        unsigned src=c->slot_to_source[slot];
        int need;
        if(src>=2048u)continue;
        need=patch_mode ? (slot>=old_used || md2gba_source_tile_dirty(v,src)) : md2gba_source_tile_dirty(v,src);
        if(!need)continue;
        if(md2gba_hwq.tile_count>=MD2GBA_HWQ_MAX_TILES)return -1;
        md2gba_hwq.tile_slot[md2gba_hwq.tile_count++]=(uint16_t)slot;
    }
    return 0;
}

