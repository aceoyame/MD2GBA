#ifndef MD2GBA_PSC_M4A_H
#define MD2GBA_PSC_M4A_H
#include <stddef.h>
#include <stdint.h>

/* Clean-room reconstruction of the mono MusicPlayer2000 transport used by
   Phantasy Star Collection.  PSC starts in frequency mode 4: 224 PCM samples
   per VBlank, with a 1584-byte staging buffer and a seven-VBlank DMA period. */
#define MD2GBA_PSC_M4A_ID_NUMBER 0x68736D53u
#define MD2GBA_PSC_M4A_FREQ_MODE 4u
#define MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK 224u
#define MD2GBA_PSC_M4A_PCM_FREQ 13379u
#define MD2GBA_PSC_M4A_DIV_FREQ 627u
#if defined(MD2GBA_MAXMOD_GBA_AUDIO)
/* Maxmod's GBA integration mixes one frame while the previous frame is
   consumed and resets sound DMA from VBlank.  The stock 13 kHz Maxmod
   stereo workspace is 896 bytes; this PSIV-native path is mono, so two
   224-byte frame segments need 448 bytes. */
#define MD2GBA_PSC_M4A_PCM_BUFFER_SIZE (MD2GBA_PSC_M4A_SAMPLES_PER_VBLANK*2u)
#define MD2GBA_PSC_M4A_DMA_PERIOD 1u
#else
#define MD2GBA_PSC_M4A_PCM_BUFFER_SIZE 1584u
#define MD2GBA_PSC_M4A_DMA_PERIOD 7u
#endif

typedef void (*Md2GbaPscM4aFillFn)(void *user, int8_t *dst, size_t samples);

typedef struct Md2GbaPscM4a {
    /* Match the behaviorally-relevant prefix of PSC's MusicPlayer2000
       SoundInfo object.  Keeping these as explicit state makes the transport
       phase/locking rules visible rather than hiding them in a custom ring. */
    uint32_t ident;
    volatile uint8_t pcm_dma_counter;
    uint8_t reverb;
    uint8_t max_chans;
    uint8_t master_volume;
    uint8_t freq;
    uint8_t mode;
    uint8_t c15;
    uint8_t pcm_dma_period;
    uint8_t max_lines;
    uint8_t gap[3];
    int32_t pcm_samples_per_vblank;
    int32_t pcm_freq;
    int32_t div_freq;

    int8_t *pcm_buffer;
    volatile uint8_t started;
    volatile uint8_t pending_frame;
    uint8_t last_mix_slot;
    uint8_t reserved;
    uint16_t timer_reload;
    uint32_t vsyncs;
    uint32_t mixes;
    uint32_t dropped_frames;
    uint32_t dma_rewinds;
} Md2GbaPscM4a;

void md2gba_psc_m4a_init(Md2GbaPscM4a *s, int8_t *pcm_buffer);
void md2gba_psc_m4a_start(Md2GbaPscM4a *s);
void md2gba_psc_m4a_stop(Md2GbaPscM4a *s);
unsigned md2gba_psc_m4a_service(Md2GbaPscM4a *s, Md2GbaPscM4aFillFn fill, void *user);
void md2gba_psc_m4a_vsync(void);
Md2GbaPscM4a *md2gba_psc_m4a_active(void);
/* Pure helper mirroring SoundMain's PCM segment selection. */
unsigned md2gba_psc_m4a_mix_slot(uint8_t pcm_dma_counter);

#endif
