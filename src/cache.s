; cache.s -- the banked-RAM sample cache.
;
; The mixer reads only from four page-aligned 256-byte pages in fixed RAM and
; never touches the bank register. Banking, the 8 KB window boundary and
; sample looping all live here.
;
; A read position is (bank, addr) with addr inside $A000-$BFFF, rather than a
; linear 24-bit offset -- that keeps a shift and mask off every access.
; Crossing $BFFF subtracts $2000 and bumps the bank.
;
; A refill copies 256 bytes in runs. Each run is bounded by whichever comes
; first: the end of the page, the end of the 8 KB window, or the loop end.
; Inside a run there are no tests: the destination is a self-modified
; "sta page,y" and the source is "lda (ptr),y" with ptr biased by -y, so one
; index drives both.
;
; An inactive voice is filled with silence rather than skipped. That is what
; lets the mixer stay branchless -- it always reads a page.

        .include "x16.inc"

        .export cache_init, cache_refill, cache_set_sample, cache_stop
        .export cache_consume
        .export c_bank, c_addr, c_ebank, c_eaddr, c_lbank, c_laddr
        .export c_loop, c_active, c_static
        .import cache0, cache1, cache2, cache3

        .zeropage
zsrc:    .res 2                 ; source pointer, biased by -Y
w_addr:  .res 2                 ; working copy of the current address
w_bank:  .res 1
w_eaddr: .res 2                 ; working copy of the end address
w_ebank: .res 1
w_need:  .res 2                 ; bytes still wanted for this page
w_run:   .res 2                 ; length of the current run
w_short: .res 1                 ; loop length when it is under a page, else 0
cn_len:  .res 1                 ; cache_consume fast path: loop length
cn_v:    .res 2                 ; ...and the offset being reduced modulo it

        .bss
c_bank:   .res 4
c_addr:   .res 8                ; lo[4] then hi[4]
c_ebank:  .res 4
c_eaddr:  .res 8
c_lbank:  .res 4
c_laddr:  .res 8
c_loop:   .res 4
c_active: .res 4
; Non-zero once a voice's page holds a loop whose length divides 256. The
; mixer's phase index wraps at 256, and 256 mod len == 0, so the wrap lands
; exactly where the loop continues: the page is then valid forever and the
; voice needs no refill and no consume at all.
c_static: .res 4
cv:       .res 1                ; voice being refilled
y_save:   .res 1

        .rodata
cache_hi: .byte >cache0, >cache1, >cache2, >cache3

        .code

cache_init:
        ldx #3
ci1:    stz c_active,x
        stz c_static,x
        stz c_loop,x
        dex
        bpl ci1
        rts

cache_stop:                             ; X = voice
        stz c_active,x
        stz c_static,x                  ; or the page would loop on forever:
        rts                             ; a static voice is never refilled

; ---------------------------------------------------------------------------
; cache_set_sample -- X = voice. Caller has already filled c_* for this voice
; (start in c_bank/c_addr, end in c_ebank/c_eaddr, loop point in
; c_lbank/c_laddr, c_loop). Marks it active.
; ---------------------------------------------------------------------------
cache_set_sample:
        stz c_static,x                  ; a new note starts before its loop
        lda #1
        sta c_active,x
        rts

; ---------------------------------------------------------------------------
; cache_refill -- X = voice. Fills that voice's 256-byte page.
; ---------------------------------------------------------------------------
cache_refill:
        stx cv
        lda cache_hi,x
        sta cr_wr+2                     ; destination page for this voice
        sta cr_zr+2
        sta cr_tl+2
        sta cd0+2                       ; ...and each unrolled store
        sta cd1+2
        sta cd2+2
        sta cd3+2
        sta cd4+2
        sta cd5+2
        sta cd6+2
        sta cd7+2
        sta cs0+2                       ; ...and the short-loop replicator
        sta cq0+2
        sta cs1+2
        sta cq1+2
        sta cs2+2
        sta cq2+2
        sta cs3+2
        sta cq3+2

        lda c_active,x
        bne cr_go

        ldy #0                          ; inactive: whole page is silence
        lda #0
cr_zr:  sta $FF00,y
        iny
        bne cr_zr
        rts

cr_go:  lda c_addr,x
        sta w_addr
        lda c_addr+4,x
        sta w_addr+1
        lda c_bank,x
        sta w_bank
        lda c_eaddr,x
        sta w_eaddr
        lda c_eaddr+4,x
        sta w_eaddr+1
        lda c_ebank,x
        sta w_ebank

        ; A loop shorter than the page is the expensive case: filling 256
        ; bytes from a 16-byte loop means wrapping it sixteen times, and each
        ; wrap pays the whole run-bounding machinery to copy sixteen bytes.
        ; Measured 33.1 cycles/byte against 14.4 for a long sample. Instead
        ; fill ONE iteration through the normal path and replicate it: the
        ; page content is periodic, so the rest needs no banked reads at all.
        stz w_short
        lda c_loop,x
        beq cr_len_full
        lda c_ebank,x                   ; only when the loop sits in one bank
        cmp c_lbank,x
        bne cr_len_full
        lda c_eaddr+4,x                 ; and spans less than one page
        sec
        sbc c_laddr+4,x
        bne cr_len_full
        lda c_eaddr,x
        sec
        sbc c_laddr,x
        beq cr_len_full                 ; zero-length loop: not our case
        sta w_short

        ; The replicated iteration is only a period of the page if the read
        ; position is already INSIDE the loop. A fresh trigger starts at the
        ; sample start, BEFORE loop_start, and that page has to be filled in
        ; full -- replicating there repeats the first bytes of the attack
        ; instead, which buzzes on every note-on.
        lda w_bank
        cmp c_lbank,x
        bne cr_not_in_loop
        lda w_addr+1
        cmp c_laddr+4,x
        bcc cr_not_in_loop
        bne cr_len_full
        lda w_addr
        cmp c_laddr,x
        bcs cr_len_full
cr_not_in_loop:
        stz w_short

cr_len_full:
        lda w_short
        bne cr_len_short
        stz w_need                      ; a full page: $0100
        lda #1
        sta w_need+1
        bra cr_len_done
cr_len_short:
        sta w_need                      ; just one loop iteration
        stz w_need+1
cr_len_done:
        ldy #0

        jsr at_end                      ; guard: a run of length 0 would spin
        bcc cr_run
        ldx cv
        lda c_loop,x
        bne cs_ok0                     ; cr_stop is out of branch
        jmp cr_stop                     ; range past the unrolled copy
cs_ok0:
        lda c_laddr,x
        sta w_addr
        lda c_laddr+4,x
        sta w_addr+1
        lda c_lbank,x
        sta w_bank

cr_run:
        lda w_bank
        sta RAM_BANK
        jsr run_length                  ; -> w_run (1..256)

        ; zsrc is biased by -Y so one index drives source and destination:
        ; "lda (zsrc),y" reads the right byte for every Y in the run.
        tya
        eor #$FF
        sec
        adc w_addr                      ; w_addr - Y
        sta zsrc
        lda w_addr+1
        adc #$FF
        sta zsrc+1

        ; Copy the run: (run & 7) singles, then (run >> 3) blocks of eight.
        ; Unrolled because the 5-cycle loop tail costs as much as the copy.
        ; Every store has its own operand, so every one needs its page byte
        ; patched at entry -- a .repeat block would emit eight unpatched
        ; literals and silently copy to the wrong page.
        lda w_run
        and #7
        beq cr_blocks
        tax
cr_rem: lda (zsrc),y
cr_wr:  sta $FF00,y
        iny
        dex
        bne cr_rem

cr_blocks:
        lda w_run+1                     ; w_run >> 3, max 32
        lsr
        lda w_run
        ror
        lsr
        lsr
        beq cr_copied
        tax
cr_blk:
        lda (zsrc),y
cd0:    sta $FF00,y
        iny
        lda (zsrc),y
cd1:    sta $FF00,y
        iny
        lda (zsrc),y
cd2:    sta $FF00,y
        iny
        lda (zsrc),y
cd3:    sta $FF00,y
        iny
        lda (zsrc),y
cd4:    sta $FF00,y
        iny
        lda (zsrc),y
cd5:    sta $FF00,y
        iny
        lda (zsrc),y
cd6:    sta $FF00,y
        iny
        lda (zsrc),y
cd7:    sta $FF00,y
        iny
        dex
        bne cr_blk
cr_copied:

        jsr advance                     ; w_addr/w_bank += w_run, handle window
        jsr sub_need                    ; w_need -= w_run
        jsr at_end
        bcc cr_more
        ldx cv
        lda c_loop,x
        bne cs_ok1                     ; cr_stop is out of branch
        jmp cr_stop                     ; range past the unrolled copy
cs_ok1:
        lda c_laddr,x                   ; loop back
        sta w_addr
        lda c_laddr+4,x
        sta w_addr+1
        lda c_lbank,x
        sta w_bank
cr_more:
        lda w_need
        ora w_need+1
        beq cm_done                     ; cr_run is out of branch range past
        jmp cr_run                      ; the unrolled copy
cm_done:
        lda w_short
        beq cm_ret

        ; w_short is only set when the position is already inside the loop, so
        ; a power-of-two length here means the filled page repeats seamlessly
        ; under the mixer's 8-bit wrap. Mark it and the player stops touching
        ; this voice entirely.
        tax
        dex
        txa
        and w_short
        bne cs_notpow                   ; len & (len-1) != 0 -> not a power of 2
        ldx cv
        lda #1
        sta c_static,x
cs_notpow:
        lda w_short

        ; Replicate the one iteration across the rest of the page. Source runs
        ; from 0, destination from the loop length, both to the page end --
        ; page[y] = page[y - len]. Unrolled: this is now the bulk of the work.
        ; Every operand below needs its page byte patched at entry: a .repeat
        ; block would emit unpatched literals and quietly read and write the
        ; wrong page. That exact bug cost an afternoon in the copy loop above.
        ldx #0
        ldy w_short
cr_rep:
cs0:    lda $FF00,x
cq0:    sta $FF00,y
        inx
        iny
        beq cm_ret
cs1:    lda $FF00,x
cq1:    sta $FF00,y
        inx
        iny
        beq cm_ret
cs2:    lda $FF00,x
cq2:    sta $FF00,y
        inx
        iny
        beq cm_ret
cs3:    lda $FF00,x
cq3:    sta $FF00,y
        inx
        iny
        bne cr_rep

cm_ret:
        ; Deliberately does NOT write the advanced position back. A refill
        ; only makes a page's worth of data visible; it does not consume it.
        ; The player advances the position with cache_consume once it knows
        ; how much the mixer actually used. Saving it here as well would
        ; double-count and skip 256 bytes of sample on every refill.
        rts

cr_stop:                                ; one-shot sample ran out
        stz c_active,x
        lda #0
cr_tl:  sta $FF00,y                     ; pad the rest of the page
        iny
        bne cr_tl
        rts

save_pos:
        ldx cv
        lda w_addr
        sta c_addr,x
        lda w_addr+1
        sta c_addr+4,x
        lda w_bank
        sta c_bank,x
        rts

; ---------------------------------------------------------------------------
; run_length -- w_run = min(w_need, $C000 - w_addr, end - w_addr if same bank)
; ---------------------------------------------------------------------------
run_length:
        sty y_save                      ; Y is the destination index; this
                                        ; routine uses it as scratch
        lda w_need                      ; start with what the page still wants
        sta w_run
        lda w_need+1
        sta w_run+1

        sec                             ; window: $C000 - w_addr
        lda #$00
        sbc w_addr
        tay
        lda #$C0
        sbc w_addr+1
        cmp w_run+1
        bcc rl_win
        bne rl_end
        cpy w_run
        bcs rl_end
rl_win: sty w_run
        sta w_run+1

rl_end: lda w_bank                      ; only clamp to the end in its own bank
        cmp w_ebank
        bne rl_done
        sec
        lda w_eaddr
        sbc w_addr
        tay
        lda w_eaddr+1
        sbc w_addr+1
        cmp w_run+1
        bcc rl_take
        bne rl_done
        cpy w_run
        bcs rl_done
rl_take: sty w_run
        sta w_run+1
rl_done: ldy y_save
        rts

; ---------------------------------------------------------------------------
; advance -- w_addr += w_run, wrapping past $BFFF into the next bank
; ---------------------------------------------------------------------------
advance:
        clc
        lda w_addr
        adc w_run
        sta w_addr
        lda w_addr+1
        adc w_run+1
        sta w_addr+1
        cmp #$C0
        bcc adv_done
        sbc #$20                        ; carry set: -$2000
        sta w_addr+1
        inc w_bank
adv_done:
        rts

sub_need:
        sec
        lda w_need
        sbc w_run
        sta w_need
        lda w_need+1
        sbc w_run+1
        sta w_need+1
        rts

; ---------------------------------------------------------------------------
; at_end -- carry set if w_addr/w_bank has reached the end position
; ---------------------------------------------------------------------------
at_end:
        lda w_bank
        cmp w_ebank
        bcc ae_no
        bne ae_yes
        lda w_addr+1
        cmp w_eaddr+1
        bcc ae_no
        bne ae_yes
        lda w_addr
        cmp w_eaddr
        bcc ae_no
ae_yes: sec
        rts
ae_no:  clc
        rts

; ---------------------------------------------------------------------------
; cache_consume -- X = voice, A = bytes the mixer has used from the page.
;
; Advances the read position without copying anything, honouring the window
; boundary and the loop point exactly as a refill would. The player calls this
; before topping a page up, so a page can be refilled BEFORE the mixer's 8-bit
; index wraps -- waiting for the wrap would mean the mixer reads the stale
; tail of the old page for the rest of the chunk.
; ---------------------------------------------------------------------------
cache_consume:
        stx cv
        sta w_need
        stz w_need+1
        ora w_need+1
        bne cn_go
        rts                             ; nothing consumed
cn_go:
        lda c_addr,x
        sta w_addr
        lda c_addr+4,x
        sta w_addr+1
        lda c_bank,x
        sta w_bank
        lda c_eaddr,x
        sta w_eaddr
        lda c_eaddr+4,x
        sta w_eaddr+1
        lda c_ebank,x
        sta w_ebank

        ; Fast path: a loop that lies inside ONE 256-byte page is advanced by
        ; arithmetic instead of by walking it. The general path below costs
        ; four subroutine calls per wrap, and a 20-byte loop wraps eleven
        ; times to advance the 224 bytes a page hands back -- ~1,300 cycles to
        ; compute what is really (pos - loop_start + n) mod len.
        ;
        ; Power-of-two loops never reach here: those pages are static and are
        ; never consumed at all. This is the odd-length remainder -- the 20s
        ; and 50s -- which is exactly the case cache_refill already calls the
        ; expensive one.
        ;
        ; The conditions below are the same ones cache_refill uses to set
        ; w_short, and must stay that way: a page filled by replication and a
        ; position advanced by modulo have to agree about where the loop is.
        lda c_loop,x
        beq cn_run                      ; one-shot: it can hit the end
        lda c_lbank,x
        cmp c_ebank,x                   ; loop confined to one bank...
        bne cn_run
        cmp w_bank                      ; ...and we are reading in that bank
        bne cn_run
        lda c_laddr+4,x
        cmp c_eaddr+4,x                 ; ...and to one page
        bne cn_run
        cmp w_addr+1                    ; ...and we are reading in that page
        bne cn_run
        ; Being in the loop's PAGE is not being in the loop. A short sample
        ; whose loop sits at its end has its attack in that same page, and a
        ; fresh note-on starts there -- reducing that position modulo the loop
        ; length would skip the attack and land somewhere arbitrary inside the
        ; loop. Same trap as the replication guard in cache_refill.
        lda w_addr
        cmp c_laddr,x
        bcc cn_run                      ; before the loop
        cmp c_eaddr,x
        bcs cn_run                      ; at or past the loop end
        lda c_eaddr,x
        sec
        sbc c_laddr,x                   ; len, 1..255
        beq cn_run                      ; zero-length loop: not our case
        sta cn_len

        lda w_addr
        sec
        sbc c_laddr,x                   ; offset into the loop, 0..len-1
        clc
        adc w_need                      ; + bytes consumed: up to 509, so 9 bits
        sta cn_v
        lda #0
        adc #0
        sta cn_v+1
cn_mod: lda cn_v+1
        bne cn_sub                      ; still over 255: certainly >= len
        lda cn_v
        cmp cn_len
        bcc cn_mod_done
cn_sub: lda cn_v
        sec
        sbc cn_len
        sta cn_v
        lda cn_v+1
        sbc #0
        sta cn_v+1
        bra cn_mod
cn_mod_done:
        ; laddr_lo + v cannot carry: v <= len-1 and laddr_lo + len = eaddr_lo,
        ; which is a byte because the loop end shares the loop start's page.
        lda c_laddr,x
        clc
        adc cn_v
        sta w_addr
        jmp save_pos

cn_run:
        jsr run_length                  ; bounded by window end and loop end
        jsr advance
        jsr sub_need
        jsr at_end
        bcc cn_more
        ldx cv
        lda c_loop,x
        beq cn_stop
        lda c_laddr,x                   ; loop back
        sta w_addr
        lda c_laddr+4,x
        sta w_addr+1
        lda c_lbank,x
        sta w_bank
cn_more:
        lda w_need
        ora w_need+1
        bne cn_run
        jmp save_pos

cn_stop:
        stz c_active,x                  ; one-shot ran out
        jmp save_pos

; cc65 spells the C symbol "c_static" as "_c_static".
        .export _c_static
_c_static = c_static

; Same for the read position, which tests/t_chunk.c hashes to prove two builds
; of the render loop take the identical path through the sample data.
        .export _c_bank, _c_addr
_c_bank = c_bank
_c_addr = c_addr
