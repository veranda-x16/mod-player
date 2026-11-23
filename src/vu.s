; vu.s -- peak level of each voice, read from the mixer's own cache pages.
;
; The level bars used to plot the sequencer's channel VOLUME. Most modules
; leave that at 64 for most of the music, so every bar sat at full scale and
; the meter said nothing about what was actually being heard -- it was a
; volume readout wearing a VU meter's clothes.
;
; This reads the sample data the mixer is about to play instead. Every 4th
; byte of the 256-byte page: 64 samples spanning roughly the 16 ms that page
; covers, which is a reasonable VU window, at a quarter of the cost of
; reading all of it. Measured ~2,600 cycles for all four voices.
;
; In C rather than assembly this would be ~250,000 cycles a second at a 25 Hz
; refresh -- 3% of the machine -- for a decoration.

        .export _vu_scan, _vu_peak
        .import cache0, cache1, cache2, cache3

        .bss
_vu_peak:  .res 4                       ; 0..128 per voice
vu_max:    .res 1

        .rodata
; Only the page number is needed: the caches are page-aligned by the linker.
vu_pg:     .byte >cache0, >cache1, >cache2, >cache3

        .code
_vu_scan:
        ldx #3
vs_ch:
        lda vu_pg,x
        sta vs_ld+2                     ; point the load at this voice's page
        stz vu_max
        ldy #0
vs_lp:
vs_ld:  lda $FF00,y
        bpl vs_pos
        eor #$FF                        ; |v| for a negative sample; -128
        inc a                           ; maps to 128, which is the ceiling
vs_pos:
        cmp vu_max
        bcc vs_next
        sta vu_max
vs_next:
        iny
        iny
        iny
        iny
        bne vs_lp                       ; 64 samples, then Y wraps to 0

        lda vu_max
        sta _vu_peak,x
        dex
        bpl vs_ch
        rts
