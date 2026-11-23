; t_chunk_cyc.s -- read the emulator's guest cycle counter, in order.
;
; $9FB8 SNAPSHOTS all four bytes when its low byte is read, so the four reads
; must happen low-to-high with nothing in between. Doing it in C needs the
; pointer to be volatile AND the compiler to honour the order; getting that
; wrong does not fail loudly, it just reports impossible cycle counts.
        .export _cyc_latch, _cyc_now
        .importzp sreg

EMU_CYC = $9FB8

_cyc_latch:
        sta EMU_CYC                     ; any write sets the origin
        rts

_cyc_now:                               ; unsigned long: A/X low, sreg high
        lda EMU_CYC                     ; this read takes the snapshot
        pha
        lda EMU_CYC+1
        tax
        lda EMU_CYC+2
        sta sreg
        lda EMU_CYC+3
        sta sreg+1
        pla
        rts
