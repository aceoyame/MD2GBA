/* P131 differential harness: run one generated page from randomized states.
 * Build twice (original page vs. transformed page) with -DPAGEFN=<symbol>,
 * then diff the per-trial hash streams.  Any divergence in CPU registers,
 * SR, PC, scheduler accounting, return code, WRAM, VDP, Z80 RAM or SRAM is
 * reported by the diff.
 * usage: harness ROM PCLIST TRIALS SEED
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "md2gba_aot68k.h"
#include "md2gba_bus.h"
#include "md2gba_aot_region.h"
#include "ps4_whole_region_pschybrid_internal.h"

int PAGEFN(Md2Gba68k*, Md2GbaBus*, const Md2GbaAotHooks*, ps4_whole_region_pschybrid_state*);

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= *b++; h *= 1099511628211ull; }
    return h;
}
static Md2GbaBus base, bus;

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
    uint8_t *rom = malloc(0x400000); size_t n = fread(rom, 1, 0x400000, f); fclose(f);
    md2gba_bus_init(&base, rom, n);
    uint32_t pcs[4096]; unsigned npc = 0;
    f = fopen(argv[2], "r"); if (!f) return 4;
    while (npc < 4096 && fscanf(f, "%x", &pcs[npc]) == 1) npc++;
    fclose(f);
    if (!npc) return 5;
    unsigned trials = (unsigned)strtoul(argv[3], 0, 10);
    rs = strtoull(argv[4], 0, 10) * 0x9E3779B97F4A7C15ull + 1;
    static const uint32_t lefts[] = {1, 7, 19, 40, 90, 200, 600, 2000, 20000};
    for (unsigned t = 0; t < trials; t++) {
        Md2Gba68k c; memset(&c, 0, sizeof c);
        bus = base;
        for (unsigned i = 0; i < sizeof bus.wram / 4; i++) ((uint32_t *)bus.wram)[i] = rnd();
        for (int i = 0; i < 8; i++) c.d[i] = rnd();
        for (int i = 0; i < 8; i++) {
            uint32_t k = rnd() & 7;   /* bias address registers toward real targets */
            c.a[i] = k < 4 ? (0xFFFF0000u | (rnd() & 0xFFFEu)) : k < 6 ? (rnd() & 0x2FFFFEu) : k == 6 ? (0x00FF0000u | (rnd() & 0xFFFEu)) : rnd();
        }
        c.a[7] = 0xFFFFFE00u - ((rnd() & 0xFFu) << 2);
        c.usp = rnd();
        c.sr = (uint16_t)(0x2700u | (rnd() & 0x1Fu));
        c.pc = pcs[t % npc];
        ps4_whole_region_pschybrid_state st; memset(&st, 0, sizeof st);
        st.left = lefts[rnd() % (sizeof lefts / sizeof lefts[0])];
        Md2GbaAotHooks hooks; memset(&hooks, 0, sizeof hooks);
        int rc = PAGEFN(&c, &bus, &hooks, &st);
        uint64_t h = 1469598103934665603ull;
        h = fnv(h, &c, sizeof c); h = fnv(h, &st, sizeof st); h = fnv(h, &rc, sizeof rc);
        h = fnv(h, bus.wram, sizeof bus.wram); h = fnv(h, &bus.vdp, sizeof bus.vdp);
        h = fnv(h, bus.z80_ram, sizeof bus.z80_ram); h = fnv(h, bus.sram, sizeof bus.sram);
        printf("%u %06X %u %d %u %016llx\n", t, pcs[t % npc], st.steps, rc, c.sr, (unsigned long long)h);
    }
    return 0;
}
