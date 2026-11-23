; mixer_api.s -- the replay-engine / mixer boundary.
;
; The sequencer touches ONLY these calls. It never sees VERA, never sees a
; bank register, never sees a phase accumulator. That is what allows the
; mixer behind this line to be replaced (a 65C816 version, a PSG fallback)
; without touching MOD playback.
;
; Arguments are passed in globals rather than on cc65's parameter stack:
; pushing arguments costs more than the routines themselves on a 6502.

        .include "x16.inc"

        .import steptab_lo, steptab_hi, steptab_int
        .import cache_refill, cache_stop, cache_set_sample
        .import c_bank, c_addr, c_ebank, c_eaddr, c_lbank, c_laddr
        .import c_loop, c_active
        .import mixer_set_volume
        .importzp v0f, s0f

        .export _mixer_set_period, _mixer_set_volume_ch, _mixer_trigger
        .export _mixer_stop, _mixer_set_sample
        .export mx_ch, mx_period, mx_vol
        .export mx_bank, mx_addr, mx_ebank, mx_eaddr
        .export mx_lbank, mx_laddr, mx_loop
        .export _mod_tab_bank

        .zeropage
zp_t:   .res 2
idx:    .res 2

        .bss
mx_ch:     .res 1               ; voice 0..3
mx_period: .res 2               ; Amiga period
mx_vol:    .res 1               ; 0..64
mx_bank:   .res 1               ; sample start
mx_addr:   .res 2
mx_ebank:  .res 1               ; end (loop end, or sample end)
mx_eaddr:  .res 2
mx_lbank:  .res 1               ; loop start
mx_laddr:  .res 2
mx_loop:   .res 1

        .data
_mod_tab_bank: .byte 0          ; the RAM bank holding the step table (STEPTAB), 0: fixed RAM
                                ; (DATA, not BSS: the assembly tests' BSS is not cleared)
        .code

; ---------------------------------------------------------------------------
; ch*3 into X -- the phase and step words are three bytes each and contiguous,
; so one index reaches every voice's slot.
; ---------------------------------------------------------------------------
ch_x3:  lda mx_ch
        asl
        clc
        adc mx_ch
        tax
        rts

; ---------------------------------------------------------------------------
; _mixer_set_period -- mx_ch, mx_period.
;
; A table lookup, not a division: ProTracker effects produce arbitrary periods
; between notes, so the step for every period in 113..907 is precomputed by
; tools/gen_tables.py for the chosen output rate.
; ---------------------------------------------------------------------------
_mixer_set_period:
        ; clamp to the table's range, which is also Paula's range
        lda mx_period+1
        bne sp_hi_nonzero
        lda mx_period
        cmp #<STEP_PERIOD_MIN
        bcs sp_inrange
        lda #<STEP_PERIOD_MIN           ; below 113: clamp
        sta mx_period
        stz mx_period+1
        bra sp_inrange
sp_hi_nonzero:
        lda mx_period+1
        cmp #>STEP_PERIOD_MAX
        bcc sp_inrange
        bne sp_clamp_hi
        lda mx_period
        cmp #<STEP_PERIOD_MAX
        bcc sp_inrange
        beq sp_inrange
sp_clamp_hi:
        lda #<STEP_PERIOD_MAX
        sta mx_period
        lda #>STEP_PERIOD_MAX
        sta mx_period+1

sp_inrange:
        sec                             ; idx = period - STEP_PERIOD_MIN
        lda mx_period
        sbc #<STEP_PERIOD_MIN
        sta idx
        lda mx_period+1
        sbc #>STEP_PERIOD_MIN
        sta idx+1

        jsr ch_x3                       ; X = voice*3
        lda $00                         ; the step table may be in a RAM bank
        pha
        lda _mod_tab_bank
        beq @fixed
        sta $00
@fixed:
        clc
        lda #<steptab_lo
        adc idx
        sta zp_t
        lda #>steptab_lo
        adc idx+1
        sta zp_t+1
        lda (zp_t)
        sta s0f,x

        clc
        lda #<steptab_hi
        adc idx
        sta zp_t
        lda #>steptab_hi
        adc idx+1
        sta zp_t+1
        lda (zp_t)
        sta s0f+1,x

        clc
        lda #<steptab_int
        adc idx
        sta zp_t
        lda #>steptab_int
        adc idx+1
        sta zp_t+1
        lda (zp_t)
        sta s0f+2,x
        pla
        sta $00
        rts

; ---------------------------------------------------------------------------
; _mixer_set_volume_ch -- mx_ch, mx_vol (0..64)
; ---------------------------------------------------------------------------
_mixer_set_volume_ch:
        ldx mx_ch
        lda mx_vol
        cmp #65
        bcc svc_ok
        lda #64
svc_ok: jmp mixer_set_volume

; ---------------------------------------------------------------------------
; _mixer_set_sample -- mx_ch plus the mx_bank/addr/ebank/eaddr/lbank/laddr/loop
; block. Positions are (bank, addr in $A000-$BFFF), which is what the cache
; layer wants; the loader converts from file offsets once, here.
; ---------------------------------------------------------------------------
_mixer_set_sample:
        ldx mx_ch
        lda mx_bank
        sta c_bank,x
        lda mx_addr
        sta c_addr,x
        lda mx_addr+1
        sta c_addr+4,x
        lda mx_ebank
        sta c_ebank,x
        lda mx_eaddr
        sta c_eaddr,x
        lda mx_eaddr+1
        sta c_eaddr+4,x
        lda mx_lbank
        sta c_lbank,x
        lda mx_laddr
        sta c_laddr,x
        lda mx_laddr+1
        sta c_laddr+4,x
        lda mx_loop
        sta c_loop,x
        jmp cache_set_sample

; ---------------------------------------------------------------------------
; _mixer_trigger -- restart mx_ch from the top of its sample and prime the
; cache, so the first mixed frame after a note-on is already valid.
; ---------------------------------------------------------------------------
_mixer_trigger:
        jsr ch_x3
        stz v0f,x                       ; phase = 0
        stz v0f+1,x
        stz v0f+2,x
        ldx mx_ch
        jmp cache_refill

_mixer_stop:
        ldx mx_ch
        jmp cache_stop

; ---------------------------------------------------------------------------
; C-visible aliases. cc65 emits a C symbol "mx_ch" as assembly "_mx_ch", while
; the assembly-side tests refer to the bare names -- exporting both keeps each
; caller using the spelling natural to it.
; ---------------------------------------------------------------------------
        .export _mx_ch, _mx_period, _mx_vol
        .export _mx_bank, _mx_addr, _mx_ebank, _mx_eaddr
        .export _mx_lbank, _mx_laddr, _mx_loop
_mx_ch     = mx_ch
_mx_period = mx_period
_mx_vol    = mx_vol
_mx_bank   = mx_bank
_mx_addr   = mx_addr
_mx_ebank  = mx_ebank
_mx_eaddr  = mx_eaddr
_mx_lbank  = mx_lbank
_mx_laddr  = mx_laddr
_mx_loop   = mx_loop

; ---------------------------------------------------------------------------
; Thin C-callable wrappers. cc65 passes a single fastcall argument in A.
; ---------------------------------------------------------------------------
        .import mix_block, cache_refill, cache_consume
        .import audio_init, audio_start, audio_stop
        .export _mix_frames, _cache_refill_ch, _cache_consume_ch
        .export _audio_init_rate, _audio_start, _audio_stop, _audio_fifo_full
        .export _audio_fifo_empty, _audio_aflow
        .export _mix_phase

_mix_phase = v0f                        ; 4 voices x 3 bytes of 8.16 phase

_mix_frames:                            ; A = frames, 1..255
        tax
        jmp mix_block

_cache_refill_ch:                       ; A = voice
        tax
        jmp cache_refill

_cache_consume_ch:                      ; A = bytes; voice in mx_ch
        ldx mx_ch
        jmp cache_consume

_audio_init_rate:                       ; A = AUDIO_RATE value
        jmp audio_init

_audio_start:
        jmp audio_start

_audio_stop:
        jmp audio_stop

_audio_fifo_full:                       ; returns non-zero while the FIFO is full
        lda AUDIO_CTRL
        and #ACTRL_FIFO_FULL
        ldx #0
        rts

_audio_fifo_empty:                      ; non-zero once the FIFO has starved
        lda AUDIO_CTRL
        and #ACTRL_FIFO_EMPTY
        ldx #0
        rts

; AFLOW is set while the FIFO holds less than 1/4 (1024 of 4096 bytes). Read
; as a status bit rather than taken as an interrupt: it tells the player it
; may safely add 3072 bytes without any chance of the FIFO filling mid-frame.
_audio_aflow:
        lda VERA_ISR
        and #VERA_IRQ_AFLOW
        ldx #0
        rts

        .import cache_init
        .export _cache_init
_cache_init:
        jmp cache_init

; ---------------------------------------------------------------------------
; _chunk_prepare -- returns in A how many frames the mixer may run next.
;
; Replaces two four-iteration C loops that ran on every chunk (~250 times a
; second). cc65 recomputed "mix_phase[v*3+2]" with a runtime multiply per
; voice; here the three-byte stride is a four-entry table.
;
; For each voice: a static page needs nothing (its loop tiles the page, so the
; index may wrap freely). Otherwise, if the page is nearly used, consume what
; the mixer took and refill. Then the chunk is bounded by half the smallest
; remaining headroom -- the phase step is under 2, so half can never overrun.
; ---------------------------------------------------------------------------
CP_REFILL_AT = 32
CP_MAX_CHUNK = 64

        .import cache_consume, cache_refill, c_static
        .export _chunk_prepare

        .bss
cp_min: .res 1
cp_pi:  .res 1
cp_v:   .res 1

        .rodata
cp_off: .byte 2, 5, 8, 11               ; phase integer byte of each voice

        .code
_chunk_prepare:
        lda #255
        sta cp_min
        ldx #0
        jsr cp_voice
        ldx #1
        jsr cp_voice
        ldx #2
        jsr cp_voice
        ldx #3
        jsr cp_voice

        lda cp_min
        lsr                             ; step < 2, so half the headroom is safe
        beq cp_one
        cmp #CP_MAX_CHUNK
        bcc cp_done
        lda #CP_MAX_CHUNK
        bra cp_done
cp_one: lda #1
cp_done:
        ldx #0                          ; cc65 returns 8-bit values in A, X=0
        rts

cp_voice:                               ; X = voice
        stx cp_v
        lda c_static,x
        bne cp_ret                      ; nothing to do, and it must not bound
        ldy cp_off,x
        lda v0f,y
        sta cp_pi
        eor #$FF                        ; A = 255 - phase = headroom left
        cmp #CP_REFILL_AT
        bcs cp_upd

        ldx cp_v                        ; running low: consume then refill
        lda cp_pi
        jsr cache_consume
        ldx cp_v
        ldy cp_off,x
        lda #0
        sta v0f,y                       ; page restarts at index 0
        ldx cp_v
        jsr cache_refill
        ; A full page: headroom is maximal. The C this replaces re-read
        ; c_static in a second pass, so a voice the refill had just MADE
        ; static was skipped there. Contributing 255 is the same answer: the
        ; minimum starts at 255, so it can never be the one that wins.
        lda #255

cp_upd: cmp cp_min
        bcs cp_ret
        sta cp_min
cp_ret: rts
