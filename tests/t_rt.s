; t_rt.s -- Phase 1 real-time gate: how many cycles per stereo frame are
; actually available before the FIFO starves?
;
; Must run in a NON-testbench emulator: the FIFO only drains when an audio
; device exists (docs/environment.md). SDL_AUDIODRIVER=dummy is enough, and
; -warp is safe because the drain rate is driven by emulated CPU clocks, not
; wall-clock time.
;
; For each rate, sweep an artificial per-frame load upward. Each pass renders
; a 512-frame block (1024 bytes -- the AFLOW 1/4 refill floor), burning N loop
; iterations per frame, and reports measured cycles/frame plus the underrun
; count. The largest load with zero underruns is the real budget.

        .include "x16.inc"
        .include "cycles.inc"

        .import audio_init, audio_start, audio_stop
        .import audio_underruns, audio_rate_value

        .export _main

BLOCKS      = 255               ; 512-frame blocks per measurement
                                ; long enough that the FIFO's 2048-frame slack
                                ; cannot mask a ~1% shortfall
FRAMES_HI   = 2                 ; 512 frames = $200

        .zeropage
zp_burn:    .res 1
zp_blocks:  .res 1

        .bss
cyc:        .res 4
rate_idx:   .res 1
burn_idx:   .res 1

        .rodata
; AUDIO_RATE values: 8, 12, 16, 18, 21, 24, 32 kHz
rate_tab:   .byte 21, 32, 42, 48, 56, 64, 84
RATE_N      = 7
; burn iterations per frame; each is ~5 cycles, so this spans ~4..500 cycles
; Chosen to bracket every rate's theoretical budget. Measured cost per frame
; is about 20 + 5.1*burn cycles, so these span ~245..1010 cycles/frame.
burn_tab:   .byte 44, 48, 58, 62, 68, 72, 82, 86, 92, 96, 122, 126, 190, 194
BURN_N      = 14

        .code

_main:
        stz rate_idx
rate_loop:
        stz burn_idx
burn_loop:
        ldx rate_idx
        lda rate_tab,x
        jsr audio_init                  ; resets FIFO, clears underrun count
        ldx burn_idx
        lda burn_tab,x
        sta zp_burn

        jsr prime                       ; fill FIFO before starting playback
        jsr audio_start

        lda #BLOCKS
        sta zp_blocks
        CYC_START
@blk:   jsr render_block
        dec zp_blocks
        bne @blk
        CYC_STOP cyc

        jsr audio_stop
        jsr report

        inc burn_idx
        lda burn_idx
        cmp #BURN_N
        bne burn_loop
        inc rate_idx
        lda rate_idx
        cmp #RATE_N
        bne rate_loop
        rts

; --- prime the FIFO to full before playback starts --------------------------
prime:  lda AUDIO_CTRL
        bmi @done                       ; b7 = full
        stz AUDIO_DATA
        bra prime
@done:  rts

; --- one 512-frame block ----------------------------------------------------
; Writes silence (the mixer's output value is irrelevant here; its COST is
; what is being modelled). Checks FIFO-empty once per block: if the FIFO ever
; drains completely we starved.
render_block:
        lda AUDIO_CTRL
        and #ACTRL_FIFO_EMPTY
        beq @ok
        inc audio_underruns
        bne @ok
        inc audio_underruns+1
@ok:
        ldx #0
        ldy #FRAMES_HI
@frame:
        stz AUDIO_DATA                  ; left
        stz AUDIO_DATA                  ; right
        lda zp_burn
        beq @noburn
@burn:  dec
        bne @burn
@noburn:
        dex
        bne @frame
        dey
        bne @frame
        rts

; --- report one line: rate, burn, cycles(4), underruns(2), as hex -----------
report:
        ldx rate_idx
        lda rate_tab,x
        jsr puthex
        jsr putspc
        lda zp_burn
        jsr puthex
        jsr putspc
        lda cyc+3
        jsr puthex
        lda cyc+2
        jsr puthex
        lda cyc+1
        jsr puthex
        lda cyc+0
        jsr puthex
        jsr putspc
        lda audio_underruns+1
        jsr puthex
        lda audio_underruns+0
        jsr puthex
        lda #$0A
        sta $9FBB
        rts

putspc: lda #$20
        sta $9FBB
        rts

puthex: pha
        lsr
        lsr
        lsr
        lsr
        jsr putnib
        pla
        and #$0F
putnib: cmp #10
        bcc @dig
        clc
        adc #('a' - 10)
        bra @out
@dig:   clc
        adc #'0'
@out:   sta $9FBB
        rts
