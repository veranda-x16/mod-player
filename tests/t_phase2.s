; t_phase2.s -- Phase 2 gate: what does the 4-voice mixer actually cost, and
; does it produce exactly what the C model says it should?
;
; Two builds from this one source:
;   build/t_phase2.prg      renders to the FIFO; cycle-measured
;   build/t_phase2b.prg     -DMIX_TO_BUF, renders to mixbuf; bit-compared
;
; The buffer build uses "sta mixbuf,x" (5 cycles) where the FIFO build uses
; "sta AUDIO_DATA" (4), so only the FIFO build is ever used for timing.

        .include "x16.inc"
        .include "cycles.inc"

        .import mix_block, mix_reset, mixer_set_volume
        .import cache0, cache1, cache2, cache3
        .importzp v0f, v1f, v2f, v3f, s0f, s1f, s2f, s3f

        .export bench_null, bench_mix, setup_voices, run_mix
        .export cyc_result, set_volumes, vols

        .bss
cyc_result: .res 4
vols:       .res 4

        .rodata
; Representative 8.16 steps at 16021.7 Hz output (step = 7093789.2/(2*P*F)):
;   period 428 (C-2)  -> 0.5172   period 214        -> 1.0344
;   period 113 (top)  -> 1.9590   period 856 (C-1)  -> 0.2586
; Stored low byte first: frac_lo, frac_hi, int.
steps:  .byte $A7, $84, $00             ; v0  0.5172
        .byte $4E, $09, $01             ; v1  1.0344
        .byte $83, $F5, $01             ; v2  1.9590
        .byte $34, $42, $00             ; v3  0.2586

        .code

bench_null:
        CYC_START
        CYC_STOP cyc_result
        rts

; --- fill the caches with a recognisable pattern and load the steps ---------
setup_voices:
        jsr mix_reset
        ldx #0
@fill:  txa
        sta cache0,x                    ; ramp
        eor #$FF
        sta cache1,x                    ; inverse ramp
        txa
        asl
        sta cache2,x
        txa
        lsr
        sta cache3,x
        inx
        bne @fill

        ldx #11                         ; 4 voices x 3 step bytes
@st:    lda steps,x
        sta s0f,x
        dex
        bpl @st
        rts

; --- 256 frames through the mixer, measured --------------------------------
bench_mix:
        jsr setup_voices
        ldx #3
bmv:    lda #64
        sta vols,x
        dex
        bpl bmv
        jsr set_volumes
        ldx #0                          ; 0 -> 256 frames
        CYC_START
        jsr mix_block
        CYC_STOP cyc_result
        rts

; --- apply the four volumes the driver poked into vols ---------------------
set_volumes:
        ldx #0
sv1:    lda vols,x
        jsr mixer_set_volume
        inx
        cpx #4
        bne sv1
        rts

; --- correctness entry: caches/steps poked by the driver, then render ------
run_mix:
        ldx #0
        jmp mix_block
