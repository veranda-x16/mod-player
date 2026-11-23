; (from Veranda's tests/sdk_play.s, with a wait for the system to take
; each button change: see Player)
; gate sdk: vr_play(script) for C test programs: the script (tests/check_ui.py's
; format: frame, x, y, flags (bit 0 the button, bit 6 y + 256); $FFFF ends
; it with the last frame) played
; through mouseSource with absolute positions, keys into the keyboard
; queue; at the last frame a VRAM dump and the end
.include "veranda.inc"
.include "macros.inc"
.include "x16hw.inc"

.export _vr_play, _vr_play_frame

.segment "CODE"

; a/x = the script
_vr_play:
	sta a0
	stx a0+1
	MoveW a0, a1            ; the end frame
@e:	ldy #0
	lda (a1),y
	iny
	and (a1),y
	cmp #$FF
	beq @e1
	AddVW 6, a1
	bra @e
@e1:	iny
	lda (a1),y
	sta endF
	iny
	lda (a1),y
	sta endF+1
	LoadW mouseSource, Player
	rts

Player:	; (x16-mod) Positions count at presses only. A press puts the pointer
	; where the script says and holds it there until the system has taken
	; the press (pressFlag's SET_MOUSE cleared); from then on, and with
	; the button up, the pointer stays where the system has it: opening a
	; sub menu moves it to the first item's middle, and a script that put
	; it back on the menu's title would close the sub menu (a press is
	; taken where the pointer is when the main loop gets to it, which an
	; AFLOW batch can delay by three frames). The script's frames wait
	; while a button change is not yet taken, and 4 frames after.
	lda pressFlag
	and #SET_MOUSE
	beq @h0
	lda #4
	sta hold
	jmp @out
@h0:	lda #$80                ; taken: follow the system's pointer
	sta follow
	lda hold
	beq @h1
	dec hold
	jmp @out
@h1:	IncW frame
	lda frame
	cmp endF
	bne @next
	lda frame+1
	cmp endF+1
	bne @next
	lda #1
	sta EMU_SAVE_ON_EXIT
	jmp $FFFF
@next:	ldy #0
	lda (a0),y
	tax
	iny
	lda (a0),y
	cmp #$FF
	beq @out
	cmp frame+1
	bcc @take
	bne @out
	cpx frame
	beq @take
	bcs @out
@take:	ldy #5
	lda (a0),y
	bmi @key
	and #1
	sta pb
	bne @pos
	lda #$80                ; button up: the position is the system's
	sta follow
	bra @adv
@pos:	stz follow
	ldy #2
	lda (a0),y
	sta px
	iny
	lda (a0),y
	sta px+1
	iny
	lda (a0),y
	sta py
	iny
	lda (a0),y              ; flags bit 6: y + 256
	and #$40
	asl
	rol
	rol
	sta py+1
	bra @adv
@key:	ldy #2
	lda (a0),y
	jsr k_kbdbuf_put
@adv:
	AddVW 6, a0
	bra @next
@out:
	bit follow
	bpl @own
	MoveW mouseXPos, r0
	MoveW mouseYPosW, r1
	lda pb
	sec
	rts
@own:	MoveW px, r0
	MoveW py, r1
	lda pb
	sec
	rts

.segment "DATA"
endF:	.word 0
frame:	.word $FFFF
_vr_play_frame = frame
px:	.word 320
py:	.word 240
pb:	.byte 0
hold:	.byte 0
follow:	.byte 0
