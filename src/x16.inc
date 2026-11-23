; Commander X16 hardware definitions used by x16-mod.
; Reference: ~/Projects/x16-docs, chapters 08 (memory map) and 09 (VERA).

RAM_BANK        = $00           ; 3-cycle bank switch: sta RAM_BANK
ROM_BANK        = $01
BANK_WIN        = $A000         ; 8 KB banked window $A000-$BFFF
BANK_WIN_END    = $C000
BANK_WIN_SIZE   = $2000

VERA_ADDR_L     = $9F20
VERA_ADDR_M     = $9F21
VERA_ADDR_H     = $9F22
VERA_DATA0      = $9F23
VERA_DATA1      = $9F24
VERA_CTRL       = $9F25
VERA_IEN        = $9F26
VERA_ISR        = $9F27

AUDIO_CTRL      = $9F3B         ; b7 full(r)/reset(w) b6 empty(r) b5 16bit b4 stereo b0-3 vol
AUDIO_RATE      = $9F3C         ; 0=stop, R*381.4697 Hz, >128 invalid
AUDIO_DATA      = $9F3D         ; write-only, one byte into the 4 KB FIFO

ACTRL_FIFO_FULL  = $80
ACTRL_FIFO_EMPTY = $40
ACTRL_FIFO_RESET = $80
ACTRL_16BIT      = $20
ACTRL_STEREO     = $10

VERA_IRQ_AFLOW  = $08

; AUDIO_RATE values for the rates this project cares about.
RATE_8K         = 21            ;  8010.9 Hz
RATE_12K        = 32            ; 12207.0 Hz
RATE_16K        = 42            ; 16021.7 Hz
RATE_18K        = 48            ; 18310.5 Hz
RATE_21K        = 56            ; 21362.3 Hz
RATE_24K        = 64            ; 24414.1 Hz
RATE_32K        = 84            ; 32043.5 Hz

; ProTracker period range. Paula clamps here, and tools/gen_tables.py builds
; the period->step table over exactly this span.
STEP_PERIOD_MIN = 113
STEP_PERIOD_MAX = 907
