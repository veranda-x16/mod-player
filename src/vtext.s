; vtext.s -- direct VERA text output for the drawing hot path.
;
; conio's cputc costs roughly 110 cycles a character once PLOT, the cursor
; bookkeeping and the colour and reverse state are paid for, and cprintf
; costs thousands a call. The pattern view redraws two rows eight times a
; second while the 65C02 is also mixing four voices into the PCM FIFO, so
; every cycle spent drawing comes straight out of the audio budget.
;
; These routines set the VERA address once per call and stream through
; DATA0 with auto-increment. The 80x60 text screen lives at VRAM $1B000 and
; the KERNAL's map is 128 tiles wide, so cell (x,y) is at
; $1B000 + y*256 + x*2: the screen code, then the colour (bg<<4 | fg). That
; is the layout cc65's own cputc writes to, not the $00000 / 80-cells-a-row
; one an older VERA memory map would suggest.
;
; Arguments pass in globals, as mixer_api.s does: pushing them on cc65's
; parameter stack would cost more than the routines themselves. The one
; fastcall argument arrives in A (A/X for a pointer). Every call advances
; vt_x by the number of cells it wrote, so consecutive calls run along a line.
;
; C string literals are PETSCII on cc65's CBM targets. The PETSCII to screen
; code conversion here is the XOR-by-top-three-bits table conio uses, so
; anything that displayed correctly through cputs displays identically here,
; and vt_rvs ($00 or $80) is OR'ed into the screen code exactly as revers()
; does.
;
; The address is set afresh on every call: pump() runs between calls, and
; the KERNAL's mouse scan moves the pointer sprite through the same port.

        .include "x16.inc"

        .export _vt_x, _vt_y, _vt_col, _vt_rvs, _vt_w, _vt_cols
        .export _vt_put, _vt_puts, _vt_pad, _vt_fill, _vt_write, _vt_blit

SCREEN_M        = $B0           ; ADDR_M of row 0: the screen is at $1B000
SCREEN_H_INC1   = $11           ; ADDR_H: VRAM bit 16 set, auto-increment 1

        .zeropage
src:    .res 2                  ; string being written
cols:   .res 2                  ; vt_blit's per-cell colour array
cnt:    .res 1
chr:    .res 1

        .bss
_vt_x:    .res 1
_vt_y:    .res 1
_vt_col:  .res 1                ; bg<<4 | fg, as conio's colour byte
_vt_rvs:  .res 1                ; $80 for reverse video, else 0
_vt_w:    .res 1                ; width for vt_pad / vt_fill / vt_write / vt_blit
_vt_cols: .res 2                ; per-cell colours for vt_blit

        .rodata
; PETSCII -> screen code: XOR by the top three bits of the character.
pet2scr: .byte $80, $00, $40, $20, $40, $C0, $80, $80

        .code

; ---------------------------------------------------------------------------
; Point DATA0 at cell (vt_x, vt_y) with auto-increment 1. Clobbers A.
; ---------------------------------------------------------------------------
set_addr:
        stz VERA_CTRL                   ; port 0
        lda _vt_x
        asl                             ; two bytes per cell
        sta VERA_ADDR_L
        lda _vt_y
        clc
        adc #SCREEN_M                   ; 256 bytes per row
        sta VERA_ADDR_M
        lda #SCREEN_H_INC1
        sta VERA_ADDR_H
        rts

; Convert the PETSCII in A to a screen code with the reverse bit applied.
; Clobbers X, preserves Y.
.macro CONVERT
        sta chr
        lsr
        lsr
        lsr
        lsr
        lsr
        tax
        lda chr
        eor pet2scr,x
        ora _vt_rvs
.endmacro

; CONVERT, then write the cell: screen code and colour.
.macro EMIT
        CONVERT
        sta VERA_DATA0
        lda _vt_col
        sta VERA_DATA0
.endmacro

advance_w:                              ; vt_x += vt_w
        lda _vt_x
        clc
        adc _vt_w
        sta _vt_x
        rts

; ---------------------------------------------------------------------------
; void __fastcall__ vt_put(char c) -- one character. vt_x += 1
; ---------------------------------------------------------------------------
_vt_put:
        sta chr
        jsr set_addr
        lda chr
        EMIT
        inc _vt_x
        rts

; ---------------------------------------------------------------------------
; void __fastcall__ vt_puts(const char *s) -- NUL-terminated. vt_x += length
; ---------------------------------------------------------------------------
_vt_puts:
        sta src
        stx src+1
        jsr set_addr
        ldy #0
@loop:  lda (src),y
        beq @done
        EMIT
        iny
        bne @loop
@done:  tya
        clc
        adc _vt_x
        sta _vt_x
        rts

; ---------------------------------------------------------------------------
; void __fastcall__ vt_pad(const char *s) -- exactly vt_w cells: the string
; up to its NUL, then spaces. A longer string is CUT at vt_w, which is what
; keeps a long filename inside its panel. vt_x += vt_w
; ---------------------------------------------------------------------------
_vt_pad:
        sta src
        stx src+1
        jsr set_addr
        ldy #0
@copy:  cpy _vt_w
        beq @done
        lda (src),y
        beq @fill
        EMIT
        iny
        bra @copy
@fill:  lda #$20                        ; space: PETSCII and screen code agree
        ora _vt_rvs
        ldx _vt_col
@pad:   sta VERA_DATA0
        stx VERA_DATA0
        iny
        cpy _vt_w
        bne @pad
@done:  jmp advance_w

; ---------------------------------------------------------------------------
; void __fastcall__ vt_fill(char c) -- vt_w copies of c. vt_x += vt_w
; ---------------------------------------------------------------------------
_vt_fill:
        sta chr
        jsr set_addr
        lda chr
        CONVERT
        ldx _vt_col
        ldy _vt_w
        beq @done
@loop:  sta VERA_DATA0
        stx VERA_DATA0
        dey
        bne @loop
@done:  jmp advance_w

; ---------------------------------------------------------------------------
; void __fastcall__ vt_write(const char *s) -- vt_w characters, no NUL test,
; all in vt_col. vt_x += vt_w
; ---------------------------------------------------------------------------
_vt_write:
        sta src
        stx src+1
        jsr set_addr
        lda _vt_w
        beq @done
        sta cnt
        ldy #0
@loop:  lda (src),y
        EMIT
        iny
        dec cnt
        bne @loop
@done:  jmp advance_w

; ---------------------------------------------------------------------------
; void __fastcall__ vt_blit(const char *s) -- vt_w characters with a colour
; per cell from vt_cols[]. One call paints a whole pattern row. vt_x += vt_w
; ---------------------------------------------------------------------------
_vt_blit:
        sta src
        stx src+1
        lda _vt_cols
        sta cols
        lda _vt_cols+1
        sta cols+1
        jsr set_addr
        lda _vt_w
        beq @done
        sta cnt
        ldy #0
@loop:  lda (src),y
        CONVERT
        sta VERA_DATA0
        lda (cols),y
        sta VERA_DATA0
        iny
        dec cnt
        bne @loop
@done:  jmp advance_w
