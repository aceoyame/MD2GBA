# P142 PSCDEEPFAST resume checkpoint

Status: linked and focused-regression validated on 2026-09-25.

Final ROM: `out_p134/RUN_THIS_MD2GBA_P142_PSCDEEPFAST.gba`
SHA-256: `2a1f50db55f4b303c8d83c188e654da592c0338ce2fc5e33f1f8d4fecfe55333`
Size: 25,242,380 bytes.
IWRAM: 27,044 / 28,672; 1,628 free.

Base lineage: P141 PSCARMFAST -> P140 PSCACHEFAST -> P139A PREBUILDFAST.

P142 changes: ARM/IWRAM reversed-WRAM DMA worker + span classifier; ARM/IWRAM projected hot kernels and shared floor divisors; selective native-PC residency additionally on page 014d. P141 ARM mixer and page-01ab PC residency retained.

Cold AOT cache: 410/410 present. `ps4_whole_region_page_014d.o` is the P142-residency object. The copyrighted PSIV input is intentionally excluded from redistributable source/checkpoint packages; retrieve `ps4built.bin` from the user's Library. Expected SHA-256: 511f35cc11f88316f8b8940e28ab298bd75a4da193672a80172884d6eb913b6a.
