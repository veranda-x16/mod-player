; Cycle measurement via the emulator's guest-readable counter.
;
; $9FB8-$9FBB behave differently for reads and writes (x16-emulator
; src/memory.c, emu_read/emu_write):
;   write $9FB8 -> latch clock_base = clockticks6502
;   read  $9FB8 -> snapshot (clockticks6502 - clock_base), return bits 0-7
;   read  $9FB9/A/B -> bits 8-15 / 16-23 / 24-31 of THAT snapshot
;
; Emulator-only. These writes do nothing on real hardware, but strip them
; from shipping builds anyway.
;
; CYC_START/CYC_STOP have a fixed overhead of their own; measure it once with
; an empty region (tests/t_phase1.s: bench_null) and subtract.

EMU_CYC = $9FB8

.macro CYC_START
        sta EMU_CYC             ; any write latches the origin
.endmacro

.macro CYC_STOP dest
        lda EMU_CYC             ; take the snapshot, read bits 0-7
        sta dest+0
        lda EMU_CYC+1
        sta dest+1
        lda EMU_CYC+2
        sta dest+2
        lda EMU_CYC+3
        sta dest+3
.endmacro
