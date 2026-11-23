; vr_audio.s -- the library's AFLOW batches under Veranda (DECISIONS V52).
;
; intFirstVector (V41) is called first in every interrupt, before Veranda
; saves anything; the routine here serves AFLOW and leaves the rest to
; Veranda: on ISR AND IEN AND $08 (AFLOW's status stays set while masked,
; so ISR alone would recurse on the first nested VSYNC) it masks AFLOW,
; cli -- VSYNC goes on through Veranda's interrupt, the pointer, the keys,
; the processes, the clock -- runs one batch (mod_irq_batch keeps the RAM
; bank and cc65's zero page and has its own C stack), sei, unmasks AFLOW
; while the library is armed, and returns c set. Anything else: c clear.
;
; The kernel clears intFirstVector at an accessory's start and when it
; comes back (V41), and stops VERA's PCM where this routine goes away
; (V52): vr_audio_on again after GetFile of an accessory.

        .export _vr_audio_on, _vr_audio_off
.ifdef VR_CYCLES
        .export _vr_batch_cyc
        .bss
_vr_batch_cyc: .res 4           ; cycles spent in batches (the emulator's counter)
t0:     .res 4
.endif
        .import _mod_irq_batch, _mod_irq_armed, _mod_use_irq

VERA_IEN = $9F26
VERA_ISR = $9F27
IFV      = $0578                ; intFirstVector

        .code
_vr_audio_on:
        php
        sei
        lda #<Disp
        sta IFV
        lda #>Disp
        sta IFV+1
        lda #1
        sta _mod_use_irq
        lda _mod_irq_armed      ; playing: AFLOW on again
        beq @1
        lda VERA_IEN
        ora #$08
        sta VERA_IEN
@1:     plp
        rts

_vr_audio_off:
        php
        sei
        stz IFV+1               ; (the high byte first: the entry tests it)
        stz IFV
        stz _mod_use_irq
        lda VERA_IEN
        and #$F7
        sta VERA_IEN
        plp
        rts

Disp:   lda VERA_ISR
        and VERA_IEN
        and #$08
        bne @a
        clc
        rts
@a:     lda VERA_IEN
        and #$F7
        sta VERA_IEN
.ifdef VR_CYCLES
        ldx #0                  ; $9FB8 read first: it latches all four
@c0:    lda $9FB8,x
        sta t0,x
        inx
        cpx #4
        bne @c0
.endif
        cli
        jsr _mod_irq_batch
        sei
.ifdef VR_CYCLES
        lda $9FB8
        sec
        sbc t0
        sta t0
        lda $9FB9
        sbc t0+1
        sta t0+1
        lda $9FBA
        sbc t0+2
        sta t0+2
        lda $9FBB
        sbc t0+3
        sta t0+3
        clc
        ldx #0
@c1:    lda _vr_batch_cyc,x
        adc t0,x
        sta _vr_batch_cyc,x
        inx
        txa                     ; (inx/cpx would clobber the carry)
        eor #4
        bne @c1
.endif
        lda _mod_irq_armed
        beq @x
        lda VERA_IEN
        ora #$08
        sta VERA_IEN
@x:     sec
        rts
