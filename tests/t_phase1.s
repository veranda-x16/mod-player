; t_phase1.s -- Phase 1 measurements: what does feeding the FIFO actually cost?
;
; Poked in and driven by tests/run_phase1.py. Each entry point brackets a
; workload with the emulator's cycle counter and leaves a 32-bit result in
; cyc_result. bench_null measures the bracket itself so it can be subtracted.
;
; Every loop here writes to AUDIO_DATA unconditionally. Under -testbench the
; FIFO never drains, so polling for space would hang (docs/environment.md).

        .include "x16.inc"
        .include "cycles.inc"

        .import audio_init, audio_start, audio_stop
        .import audio_write_block, audio_fill_poll
        .import audio_underruns, audio_rate_value
        .importzp zp_src

        .export bench_null, bench_sta_only, bench_fill_simple
        .export bench_fill_unrolled, bench_fill_unrolled16
        .export cyc_result, srcbuf
        .export setup_16k, play_realtime

        .bss
cyc_result:     .res 4
frames:         .res 2

        .segment "ALIGNBSS"
; 256-byte source page, page-aligned via cfg/x16mod.cfg so that indexed loads
; never cross a page boundary and add a cycle to the measurement.
srcbuf: .res 256

        .code

; --- bracket overhead -------------------------------------------------------
bench_null:
        CYC_START
        CYC_STOP cyc_result
        rts

; --- pure FIFO write cost: 256 x "sta AUDIO_DATA", nothing else -------------
bench_sta_only:
        lda #$40
        ldx #0
        CYC_START
@1:     sta AUDIO_DATA
        inx
        bne @1
        CYC_STOP cyc_result
        rts

; --- the straightforward copy loop (audio_write_block) ----------------------
bench_fill_simple:
        lda #<srcbuf
        sta zp_src
        lda #>srcbuf
        sta zp_src+1
        ldx #0                          ; X=0 -> 256 iterations
        CYC_START
        jsr audio_write_block
        CYC_STOP cyc_result
        rts

; --- 8x unrolled, absolute,X addressing -------------------------------------
bench_fill_unrolled:
        ldx #0
        CYC_START
@1:     .repeat 8, i
        lda srcbuf+i,x
        sta AUDIO_DATA
        .endrepeat
        txa
        clc
        adc #8
        tax
        bne @1
        CYC_STOP cyc_result
        rts

; --- 16x unrolled -----------------------------------------------------------
bench_fill_unrolled16:
        ldx #0
        CYC_START
@1:     .repeat 16, i
        lda srcbuf+i,x
        sta AUDIO_DATA
        .endrepeat
        txa
        clc
        adc #16
        tax
        bne @1
        CYC_STOP cyc_result
        rts

; --- real-time playback (windowed emulator only) ----------------------------
; Fills forever from srcbuf, counting underruns. Used by the warp-mode run,
; never under testbench.
setup_16k:
        lda #RATE_16K
        jsr audio_init
        lda #<srcbuf
        sta zp_src
        lda #>srcbuf
        sta zp_src+1
        jsr audio_fill_poll             ; prime before starting playback
        jsr audio_start
        rts

play_realtime:
        lda #<srcbuf
        sta zp_src
        lda #>srcbuf
        sta zp_src+1
@loop:  jsr audio_fill_poll
        lda frames
        inc frames
        bne @loop
        inc frames+1
        lda frames+1
        cmp #$20                        ; ~8K refill passes, then stop
        bne @loop
        jsr audio_stop
        rts
