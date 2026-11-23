; vera_audio.s -- VERA PCM FIFO path for x16-mod.
;
; The mixer never talks to VERA directly; this is the only module that does.
; 8-bit signed stereo, <left><right> byte order (X16 Reference ch.9).
;
; FIFO is 4 KB. AFLOW fires below 1/4 full and -- importantly -- can only be
; cleared by refilling to at least 1/4 (1024 bytes). A handler that writes
; less than that re-fires immediately, so 1024 bytes is the floor on block size.

        .include "x16.inc"

        .export audio_init, audio_start, audio_stop, audio_set_volume
        .export _audio_set_volume, pcm_volume, _pcm_volume
        .export audio_fill_poll, audio_write_block
        .export audio_underruns, audio_rate_value
        .exportzp zp_src

        .zeropage
zp_src: .res 2                  ; source pointer for block writes

        .bss
audio_rate_value:   .res 1      ; AUDIO_RATE value chosen at init
pcm_volume:         .res 1      ; 0..15, VERA's own PCM attenuator
audio_underruns:    .res 2      ; times the FIFO was seen empty during refill

        .code

; ---------------------------------------------------------------------------
; audio_init -- A = AUDIO_RATE value (see RATE_* in x16.inc)
;
; Reference ch.9: "set the sample rate at 0 to stop playback, fill the FIFO
; with some initial data and then set the desired sample rate." Playback is
; therefore NOT started here; call audio_start once the FIFO is primed.
; ---------------------------------------------------------------------------
audio_init:
        sta audio_rate_value
        lda pcm_volume                  ; default to full on first use
        bne @havevol
        lda #15
        sta pcm_volume
@havevol:
        lda audio_rate_value
        stz AUDIO_RATE                          ; stop before touching the FIFO
        lda #ACTRL_FIFO_RESET                   ; b7 write = FIFO reset
        sta AUDIO_CTRL
        lda pcm_volume                          ; 8-bit stereo at the set volume
        ora #ACTRL_STEREO
        sta AUDIO_CTRL
        stz audio_underruns
        stz audio_underruns+1
        rts

audio_start:
        lda audio_rate_value
        sta AUDIO_RATE
        rts

audio_stop:
        stz AUDIO_RATE
        rts

; ---------------------------------------------------------------------------
; audio_write_block -- write X bytes from (zp_src) into the FIFO, no polling.
;
; Unconditional: a write to a full FIFO is discarded by VERA. This is the
; form the mixer uses, and the only form safe under -testbench, where the
; FIFO never drains and a poll would deadlock (docs/environment.md).
; ---------------------------------------------------------------------------
audio_write_block:
        ldy #0
@1:     lda (zp_src),y
        sta AUDIO_DATA
        iny
        dex
        bne @1
        rts

; ---------------------------------------------------------------------------
; audio_fill_poll -- fill the FIFO from the 256-byte page at (zp_src) until
; full, looping the page. Real-time playback only; deadlocks under testbench.
;
; Counts underruns: if the FIFO reports empty on entry we already starved.
; ---------------------------------------------------------------------------
audio_fill_poll:
        lda AUDIO_CTRL
        and #ACTRL_FIFO_EMPTY
        beq @notempty
        inc audio_underruns
        bne @notempty
        inc audio_underruns+1
@notempty:
        ldy #0
@1:     lda AUDIO_CTRL
        bmi @done                       ; b7 = FIFO full
        lda (zp_src),y
        sta AUDIO_DATA
        iny
        bra @1
@done:  rts

; ---------------------------------------------------------------------------
; audio_set_volume -- A = 0..15, VERA's PCM attenuator (logarithmic).
;
; Bit 7 of AUDIO_CTRL is the FIFO RESET when written, so it must stay clear
; here: resetting the FIFO to change volume would drop everything already
; queued and click.
; ---------------------------------------------------------------------------
_audio_set_volume:
audio_set_volume:
        and #$0F
        sta pcm_volume
        ora #ACTRL_STEREO
        sta AUDIO_CTRL                          ; bit 7 clear: no FIFO reset
        rts

; cc65 spells the C symbol "pcm_volume" as "_pcm_volume".
_pcm_volume = pcm_volume
