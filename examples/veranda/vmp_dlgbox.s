; vmp_dlgbox.s -- DoDlgBox for a box whose C routines (its DB_USR_ROUT,
; keyVector, otherPressVec, its icons) may close it with RstrFrmDialogue
; (examples/veranda/vmp_dlg.c). That call returns from DoDlgBox from inside
; the C routine's frame: the 6502 stack is put back by the kernel, cc65's C
; stack pointer is not; this keeps it on the 6502 stack around the call
; and puts it back. (Veranda Calc's chooser does the same in C; the
; Veranda SDK has it as vr_dlgbox in later versions.) Main RAM.
;
; MIT licence (see LICENSE).

.include "veranda.inc"
.importzp c_sp
.export _vmp_dlgbox

.segment "CODE"

; a/x = the box; a = DoDlgBox's result (r0L)
_vmp_dlgbox:
	sta r0L
	stx r0H
	lda c_sp
	pha
	lda c_sp+1
	pha
	jsr DoDlgBox
	pla
	sta c_sp+1
	pla
	sta c_sp
	lda r0L
	ldx #0
	rts
