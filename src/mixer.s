; mixer.s -- the 65C02 hot path. Four voices, hard-panned Amiga stereo.
;
;   L = ch1 + ch4      R = ch2 + ch3
;
; Phase is 8.16 fixed point: 16 fraction bits (an 8-bit fraction gives ~13
; cents of pitch error at middle C -- audible) and 8 integer bits, which index
; a page-aligned 256-byte per-voice sample cache. The cache layer owns banking
; and looping; this loop never touches the bank register and never branches on
; sample state.
;
; Cache bytes are pre-scaled to (sample * volume) / 128, i.e. -64..63. Two of
; them sum to -128..126, which is exactly int8 -- so the mix needs no clamp,
; no overflow test and no headroom shift. Volume is applied during cache fill
; (it is constant across a tick), which keeps the volume table out of this
; loop entirely.
;
; Build variants:
;   default        -- render to the VERA FIFO; this is what gets cycle-measured
;   -DMIX_TO_BUF   -- render 128 frames into mixbuf for bit-exact comparison
;                     against host/host_mixer.c. Different store cost, so it is
;                     never the variant used for timing.

        .include "x16.inc"

        .export mix_block, mix_reset, mixer_set_volume, _mix_clear
        .import voltab
        .export cache0, cache1, cache2, cache3
        .exportzp v0f, v1f, v2f, v3f, s0f, s1f, s2f, s3f
.ifdef MIX_TO_BUF
        .export mixbuf
.endif

        .zeropage
; phase: 2 bytes fraction + 1 byte integer (cache index)
v0f:    .res 3
v1f:    .res 3
v2f:    .res 3
v3f:    .res 3
; step: same layout
s0f:    .res 3
s1f:    .res 3
s2f:    .res 3
s3f:    .res 3
mixtmp: .res 1

        .segment "ALIGNBSS"
cache0: .res 256
cache1: .res 256
cache2: .res 256
cache3: .res 256
.ifdef MIX_TO_BUF
mixbuf: .res 256                        ; 128 stereo frames
.endif

        .code

; ---------------------------------------------------------------------------
; One voice: advance the 8.16 phase and fetch its (pre-scaled) sample.
; Leaves the sample byte in A. Clobbers Y.
;
; 31 cycles of phase arithmetic + 4 for the fetch. The clc is unavoidable:
; the carry out of the previous voice's high byte is not ours to inherit.
; ---------------------------------------------------------------------------
.macro  VOICE ph, st, cache, volop
        lda ph                          ; 3   fraction low
        clc                             ; 2
        adc st                          ; 3
        sta ph                          ; 3
        lda ph+1                        ; 3   fraction high
        adc st+1                        ; 3
        sta ph+1                        ; 3
        lda ph+2                        ; 3   integer / cache index
        adc st+2                        ; 3
        sta ph+2                        ; 3
        tay                             ; 2
        lda cache,y                     ; 4   page-aligned: no page-cross penalty
        tay                             ; 2
volop:  lda voltab,y                    ; 4   high byte self-modified = volume
.endmacro

; ---------------------------------------------------------------------------
; mix_reset -- zero all four phases.
; ---------------------------------------------------------------------------
mix_reset:
        ldx #11
@1:     stz v0f,x
        dex
        bpl @1
        rts

; ---------------------------------------------------------------------------
; _mix_clear -- every phase and every step to 0 (mod_hooks_init, before
; mod_seq_init's first tick sets the first notes).
;
; Zero page is not cleared by any start-up code. A voice no note has reached
; yet still advances by its step, and chunk_prepare bounds the chunk by its
; headroom, so a stale step changed the chunk decisions -- never a sample of
; output (the voice is silent), but the chunk hash: under Veranda, whose zero
; page at $A9 holds what the loader left, DIZZYCRS hashed differently from
; the stand-alone build, whose zero page BASIC had left at 0. The golden
; hashes are the cold start, so this keeps them.
; ---------------------------------------------------------------------------
_mix_clear:
        ldx #24
@1:     stz v0f,x
        dex
        bpl @1
        rts

.ifdef MIX_TO_BUF

; ---------------------------------------------------------------------------
; mix_block -- render 128 stereo frames into mixbuf. X indexes bytes, so it
; doubles as the loop counter: the block ends when X wraps back to 0.
; ---------------------------------------------------------------------------
mix_block:
        ldx #0
mixloop:
        VOICE v0f, s0f, cache0, volop0
        sta mixtmp
        VOICE v3f, s3f, cache3, volop3
        clc
        adc mixtmp
        sta mixbuf,x
        inx
        VOICE v1f, s1f, cache1, volop1
        sta mixtmp
        VOICE v2f, s2f, cache2, volop2
        clc
        adc mixtmp
        sta mixbuf,x
        inx
        bne mixloop
        rts

.else

; ---------------------------------------------------------------------------
; mix_block -- render X stereo frames (X=0 means 256) into the VERA FIFO.
; Unconditional writes: VERA discards a write to a full FIFO, and polling
; would deadlock under -testbench where the FIFO never drains.
;
; X is the counter because a 16-bit count costs ~20 cycles per frame in
; decrement-and-test alone -- 4% of the entire budget at 16 kHz, for
; bookkeeping. Chunks longer than 256 frames are the caller's problem; the
; cache page runs out well before then anyway.
; ---------------------------------------------------------------------------
mix_block:
mixloop:
        VOICE v0f, s0f, cache0, volop0
        sta mixtmp
        VOICE v3f, s3f, cache3, volop3
        clc
        adc mixtmp
        sta AUDIO_DATA                  ; left
        VOICE v1f, s1f, cache1, volop1
        sta mixtmp
        VOICE v2f, s2f, cache2, volop2
        clc
        adc mixtmp
        sta AUDIO_DATA                  ; right
        dex
        bne mixloop
        rts

.endif

; ---------------------------------------------------------------------------
; mixer_set_volume -- A = volume 0..64, X = voice 0..3
;
; Patches the high byte of that voice's "lda voltab,y" to point at the right
; 256-byte volume page. A volume change therefore costs one store and nothing
; at mix time -- no table rebuild, no per-sample multiply.
; ---------------------------------------------------------------------------
; A = volume 0..64. The table holds every fourth level, so the page index is
; (volume + 2) >> 2 -- rounding to nearest, so 64 still reaches full scale.
; Four units in 64 is about 0.55 dB, and the samples underneath are 8-bit, so
; finer gradations are largely lost anyway. The 4 KB this frees is what makes
; the rest of the player fit.
mixer_set_volume:
        clc
        adc #2
        lsr
        lsr
        clc
        adc #>voltab
        pha
        lda volop_lo,x
        sta @patch+1
        lda volop_hi,x
        sta @patch+2
        pla
@patch: sta $FFFF
        rts

volop_lo: .byte <(volop0+2), <(volop1+2), <(volop2+2), <(volop3+2)
volop_hi: .byte >(volop0+2), >(volop1+2), >(volop2+2), >(volop3+2)

; C-visible aliases for the voice caches (cc65 spells "cache0" as "_cache0").
        .export _cache0, _cache1, _cache2, _cache3
_cache0 = cache0
_cache1 = cache1
_cache2 = cache2
_cache3 = cache3
