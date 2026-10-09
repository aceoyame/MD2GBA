static void phase121_ui_stage_tiles(const Md2GbaVdp*v,const Md2GbaGbaUiPanel*p,unsigned pw){
    uint16_t base=md2gba_vdp_plane_a_base(v);unsigned ry,py,op,slot_base=md2gba_ui_slot_base(p);
    unsigned src_px=(p->w-2u)*8u,out_px=MD2GBA_UI_OUT_INNER*8u;
    /* DDA resampling avoids ARM EABI division helpers.  Start the error term
       half a source step in for center-ish nearest-neighbour sampling. */
    for(ry=1u;ry+1u<p->h;ry++)for(py=0u;py<8u;py++){
        unsigned sp=0u,err=src_px>>1;
        for(op=0u;op<out_px;op++){
            unsigned sx=p->x+1u+(sp>>3),ot=op>>3,px=op&7u;
            unsigned slot=slot_base+(ry-1u)*MD2GBA_UI_OUT_INNER+ot;
            uint8_t*d=(uint8_t*)(void*)(md2gba_bg_tile_stage+slot*16u);
            uint16_t m=vram_word(v,(unsigned)base+2u*((p->y+ry)*pw+sx));
            uint8_t pix=md2gba_ui_source_pixel(v,m,sp&7u,py);
            if(!(px&1u))d[py*4u+(px>>1)]=pix;
            else d[py*4u+(px>>1)]|=(uint8_t)(pix<<4);
            err+=src_px;
            if(err>=224u){err-=224u;sp++;if(err>=224u){err-=224u;sp++;}}
            if(sp>=src_px)sp=src_px-1u;
        }
    }
}
