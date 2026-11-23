; mod_irq.s -- the library's interrupt-driven batch, and AFLOW on and off.
;
; A host that feeds the FIFO from VERA's AFLOW interrupt (Veranda) has a
; dispatcher of its own, which, on AFLOW (ISR AND IEN AND $08 -- AFLOW's
; status stays set while it is masked, so ISR alone would recurse):
;
;   masks AFLOW in IEN, cli, jsr _mod_irq_batch, sei,
;   unmasks AFLOW again only while _mod_irq_armed is non-zero.
;
; With interrupts on, VSYNC keeps running the host's own interrupt work
; during the ~44 ms a batch takes. The batch interrupts arbitrary code --
; C code among it -- so it keeps cc65's 26 bytes of runtime zero page
; (c_sp, sreg, regsave, ptr1-4, tmp1-4, regbank) and the RAM bank, and runs
; the C render loop on a C stack of its own.
;
; The engine's own zero page (mixer, cache, mixer API) belongs to the
; batch: the foreground touches it only through mod_play/mod_stop, which
; disarm AFLOW first.

        .export _mod_irq_batch, _mod_aflow_on, _mod_aflow_off
        .export _mod_irq_armed, _mod_use_irq, _mod_cstack
        .import _mod_render_batch
        .importzp c_sp

VERA_IEN = $9F26
ZPSIZE   = 26
STACK    = 256                  ; (measured: see the player's VMP_DEBUG build)

        .bss
_mod_irq_armed: .res 1
_mod_use_irq:   .res 1
zsave:  .res ZPSIZE
cstack: .res STACK
_mod_cstack = cstack

        .code
_mod_irq_batch:
        lda $00                         ; the interrupted code's RAM bank
        pha
        ldx #ZPSIZE - 1
@s:     lda c_sp,x
        sta zsave,x
        dex
        bpl @s
        lda #<(cstack + STACK)
        sta c_sp
        lda #>(cstack + STACK)
        sta c_sp+1
        jsr _mod_render_batch
        ldx #ZPSIZE - 1
@r:     lda zsave,x
        sta c_sp,x
        dex
        bpl @r
        pla
        sta $00
        rts

; IEN's AFLOW bit, read-modify-written with interrupts off: the host's
; dispatcher writes IEN too.
_mod_aflow_on:
        php
        sei
        lda VERA_IEN
        ora #$08
        sta VERA_IEN
        plp
        rts

_mod_aflow_off:
        php
        sei
        lda VERA_IEN
        and #$F7
        sta VERA_IEN
        plp
        rts
